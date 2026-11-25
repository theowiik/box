"""Talk to the box over the network, with nothing but the standard library.

A convenience, not a required SDK: the box takes one JSON shape on one
endpoint, and `curl` works just as well.
"""

import base64
import json
import os
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from . import visuals
from .events import Event

#: Where the box answers unless BOX_URL says otherwise (it advertises itself
#: over mDNS as box.local).
DEFAULT_URL = "http://box.local"

#: A service must never hang because the box is off or rebooting.
TIMEOUT = 10.0

#: Visuals were drawn for small panels; they render at most this many dots a
#: side (a whole fraction of the panel) and the box scales them up.
VISUAL_MAX_SIDE = 48

#: Assumed when the box does not say how large one upload may be.
DEFAULT_MAX_FRAMES_BYTES = 2 * 1024 * 1024


class BoxError(RuntimeError):
    """The box could not be reached, or refused the request."""


def load_dotenv(start: Path | None = None) -> None:
    """Fill in `BOX_*` values from the nearest `.env` at or above `start`.

    Bounded by the repo root, so a stray `.env` in a home directory never
    configures the box. A real environment variable always wins.
    """
    folder = (start or Path.cwd()).resolve()
    for candidate in (folder, *folder.parents):
        path = candidate / ".env"
        if path.is_file():
            break
        if (candidate / ".git").exists():
            return
    else:
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip().strip("'\"")
        if key.startswith("BOX_"):
            os.environ.setdefault(key, value)


def box_url(explicit: str | None = None) -> str:
    """`--url`, then `BOX_URL`, then box.local."""
    return (explicit or os.getenv("BOX_URL") or DEFAULT_URL).rstrip("/")


class Box:
    def __init__(self, url: str | None = None, token: str | None = None) -> None:
        self.url = box_url(url)
        self.token = token if token is not None else os.getenv("BOX_TOKEN", "")
        self._panel: dict[str, Any] | None = None

    def _request(self, method: str, path: str, body: Any = None) -> Any:
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + path, data=data, method=method)
        if data is not None:
            request.add_header("Content-Type", "application/json")
        if self.token:
            request.add_header("Authorization", f"Bearer {self.token}")
        try:
            with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
                return json.loads(response.read() or b"null")
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode(errors="replace")
            try:
                detail = json.loads(detail).get("detail", detail)
            except (ValueError, AttributeError):
                pass
            if exc.code == 401:
                raise BoxError("the box wants a token: set BOX_TOKEN") from exc
            raise BoxError(f"the box refused it ({exc.code}): {detail}") from exc
        except (OSError, ValueError) as exc:
            # URLError, timeouts and refused connections are all OSErrors.
            raise BoxError(
                f"could not reach the box at {self.url}: {exc}. "
                "Is it on and on Wi-Fi? Set BOX_URL if box.local does not resolve."
            ) from exc

    def state(self) -> dict[str, Any]:
        """What the panel shows right now, and its size."""
        return self._request("GET", "/api/display")

    def panel_size(self) -> tuple[int, int]:
        """The panel's (width, height) in LED dots. Asked once, then cached."""
        panel = self._panel_info()
        return (int(panel["width"]), int(panel["height"]))

    def max_frames_bytes(self) -> int:
        """The most animation data the box takes in one event."""
        return int(self._panel_info().get("max_frames_bytes", DEFAULT_MAX_FRAMES_BYTES))

    def _panel_info(self) -> dict[str, Any]:
        if self._panel is None:
            self._panel = self.state()
        return self._panel

    def send(self, event: Event) -> dict[str, Any]:
        """POST an event, rendering a `visual` scene into frames first."""
        if event.get("scene", {}).get("kind") == "visual":
            event = {**event, "scene": self.render_visual(event["scene"]["name"])}
        return self._request("POST", "/api/events", event)

    def render_visual(self, name: str) -> dict[str, Any]:
        """A built-in visual as a `frames` scene the box will accept.

        Rendered at a whole fraction of the panel, at most VISUAL_MAX_SIDE dots a
        side and small enough for one upload; the box scales it to the panel.
        """
        if name not in visuals.VISUALS:
            raise ValueError(f"unknown visual {name!r}; try {sorted(visuals.VISUALS)}")
        panel_w, panel_h = self.panel_size()
        limit = self.max_frames_bytes()
        step = max(1, -(-max(panel_w, panel_h) // VISUAL_MAX_SIDE))  # ceiling
        while True:
            width, height = max(1, panel_w // step), max(1, panel_h // step)
            data = visuals.frames(name, width, height)
            if len(data) <= limit or (width == 1 and height == 1):
                break
            step += 1
        visual = visuals.VISUALS[name]
        return {
            "kind": "frames",
            "width": width,
            "height": height,
            "fps": visual.fps,
            "loop": visual.loop,
            "data": base64.b64encode(data).decode(),
        }

    def clear(self) -> None:
        """Drop queued events."""
        self._request("DELETE", "/api/events")
