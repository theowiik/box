import re
from pathlib import Path

from box_sources.weather import BUCKET_SECONDS, MockWeather

#: The firmware's font, generated from the original Python font: the glyphs
#: the box can actually draw.
FONT_TABLE = (
    Path(__file__).resolve().parents[3]
    / "firmware/components/box_core/src/font_table.inc"
)


def glyphs() -> set[str]:
    text = FONT_TABLE.read_text(encoding="utf-8")
    return {chr(int(cp, 16)) for cp in re.findall(r"\{0x([0-9A-F]{4}),", text)}


#: A timestamp aligned to the start of a bucket, so the whole bucket is ahead.
ALIGNED = 1_700_000_000.0 - (1_700_000_000.0 % BUCKET_SECONDS)


def test_weather_is_an_ambient_card_in_its_condition_colour():
    weather = MockWeather("Stockholm", dwell=15.0)
    event = weather.poll()
    assert event["source"] == "weather"
    assert event["priority"] == "ambient"
    assert event["duration"] == 15.0
    assert event["scene"]["kind"] == "text"
    assert event["scene"]["color"].startswith("#")


def test_weather_is_stable_within_a_bucket():
    weather = MockWeather("Stockholm")
    assert weather.get(ALIGNED) == weather.get(ALIGNED + BUCKET_SECONDS - 1)


def test_weather_changes_between_buckets():
    weather = MockWeather("Stockholm")
    samples = {
        weather.get(ALIGNED + i * BUCKET_SECONDS).display_text for i in range(20)
    }
    assert len(samples) > 1


def test_cities_differ():
    assert MockWeather("Stockholm").get(ALIGNED) != MockWeather("Oslo").get(ALIGNED)


def test_every_condition_renders_in_the_boxs_font():
    """No weather string may need the box's '?' fallback glyph."""
    font = glyphs()
    assert "A" in font and "°" in font
    weather = MockWeather("Stockholm")
    for i in range(500):
        text = weather.get(1_700_000_000.0 + i * BUCKET_SECONDS).display_text
        for char in text:
            assert char.upper() in font, f"{char!r} in {text!r} has no glyph"
