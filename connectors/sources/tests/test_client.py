import base64
import threading

import pytest

from box_sources import events, runner, visuals
from box_sources.client import Box, BoxError, box_url, load_dotenv


def test_url_defaults_to_box_local():
    assert box_url() == "http://box.local"


def test_url_from_env_then_explicit(monkeypatch):
    monkeypatch.setenv("BOX_URL", "http://192.168.0.228/")
    assert box_url() == "http://192.168.0.228"
    assert box_url("http://other:8080/") == "http://other:8080"


def test_dotenv_fills_in_but_never_overrides(tmp_path, monkeypatch):
    (tmp_path / ".git").mkdir()
    (tmp_path / ".env").write_text('# the box\nBOX_URL="http://from-file"\nOTHER=x\n')
    sub = tmp_path / "connectors" / "sources"
    sub.mkdir(parents=True)
    load_dotenv(sub)
    assert box_url() == "http://from-file"
    monkeypatch.setenv("BOX_URL", "http://from-env")
    load_dotenv(sub)
    assert box_url() == "http://from-env"


def test_send_text(box):
    reply = Box(box.url).send(events.text("me", "hello", priority="alert"))
    assert reply["priority"] == "alert"
    assert box.events == [
        {
            "source": "me",
            "priority": "alert",
            "scene": {"kind": "text", "text": "hello"},
        }
    ]


def test_long_text_is_trimmed_to_what_the_box_takes():
    event = events.text("me", "x" * 600)
    assert len(event["scene"]["text"]) == events.MAX_TEXT


def test_visuals_are_rendered_here_at_a_small_panel_size(box):
    box.width, box.height = 16, 8
    Box(box.url).send(events.visual("essr", "beer"))
    scene = box.events[0]["scene"]
    assert scene["kind"] == "frames"
    assert (scene["width"], scene["height"]) == (16, 8)
    assert scene["loop"] is False
    data = base64.b64decode(scene["data"])
    assert data == visuals.frames("beer", 16, 8)


def test_visuals_on_a_large_panel_render_at_a_fraction_of_it(box):
    box.width = box.height = 96
    Box(box.url).send(events.visual("cli", "gradient"))
    scene = box.events[0]["scene"]
    assert (scene["width"], scene["height"]) == (48, 48)


def test_visuals_shrink_to_fit_the_boxs_upload_limit(box):
    box.width = box.height = 48
    box.max_frames_bytes = 120 * 24 * 24 * 3  # gradient's 120 frames at 24x24
    Box(box.url).send(events.visual("cli", "gradient"))
    scene = box.events[0]["scene"]
    assert (scene["width"], scene["height"]) == (24, 24)
    assert len(base64.b64decode(scene["data"])) <= box.max_frames_bytes


def test_token_is_sent_when_set(box):
    box.token = "s3cret"
    with pytest.raises(BoxError, match="token"):
        Box(box.url, token="").send(events.text("me", "x"))
    Box(box.url, token="s3cret").send(events.text("me", "x"))
    assert len(box.events) == 1


def test_a_refusal_carries_the_boxs_reason(box):
    box.status = 422
    with pytest.raises(BoxError, match="422.*nope"):
        Box(box.url).send(events.text("me", "x"))


def test_an_unreachable_box_is_a_clear_error():
    with pytest.raises(BoxError, match="could not reach"):
        Box("http://127.0.0.1:9").send(events.text("me", "x"))


def test_bad_priority_is_caught_before_sending():
    with pytest.raises(ValueError):
        events.text("me", "x", priority="urgent")


# -- the runner ----------------------------------------------------------------


class Counter:
    """A source that has something new on every poll."""

    name = "counter"
    interval = 0.05

    def __init__(self) -> None:
        self.n = 0

    def poll(self):
        self.n += 1
        return events.text(self.name, str(self.n))


class Broken:
    name = "broken"
    interval = 0.05

    def poll(self):
        raise RuntimeError("boom")


def test_poll_once_delivers_and_survives_failures(box):
    assert runner.poll_once(Box(box.url), Counter())
    assert not runner.poll_once(Box(box.url), Broken())
    assert not runner.poll_once(Box("http://127.0.0.1:9"), Counter())
    assert len(box.events) == 1


def test_run_polls_every_source_until_stopped(box, monkeypatch):
    # The real floor is a second; the test should not take several.
    monkeypatch.setattr(runner, "MIN_INTERVAL", 0.0)
    stop = threading.Event()
    counter = Counter()
    thread = threading.Thread(
        target=runner.run, args=(Box(box.url), [counter, Broken()], stop)
    )
    thread.start()
    threading.Timer(0.3, stop.set).start()
    thread.join(timeout=5)
    assert not thread.is_alive()
    assert counter.n >= 2, "a broken source must not stop the others"
    assert len(box.events) == counter.n
