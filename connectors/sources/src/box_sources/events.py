"""Events, as the JSON the box takes. Plain dicts: there is no SDK to keep in sync.

The box's whole API is one shape (see the repo README):

    {"source": "weather", "priority": "ambient", "duration": 12,
     "scene": {"kind": "text", "text": "22°C SUNNY", "color": "#FFD54F"}}

`visual` scenes exist only on this side: the box no longer renders visuals
itself, so the client expands them into `frames` before sending.
"""

from typing import Any

Event = dict[str, Any]

PRIORITIES = ("ambient", "normal", "alert", "alarm")

#: The box refuses text longer than this many characters.
MAX_TEXT = 512


def event(
    source: str,
    scene: dict[str, Any],
    *,
    priority: str = "normal",
    duration: float | None = None,
) -> Event:
    if priority not in PRIORITIES:
        raise ValueError(f"priority must be one of {PRIORITIES}, got {priority!r}")
    out: Event = {"source": source, "priority": priority, "scene": scene}
    if duration is not None:
        out["duration"] = duration
    return out


def text(
    source: str,
    text: str,
    *,
    color: str | None = None,
    priority: str = "normal",
    duration: float | None = None,
) -> Event:
    """Text on the panel. Leave `color` out to let the box style the source."""
    if len(text) > MAX_TEXT:
        text = text[: MAX_TEXT - 3] + "..."
    scene: dict[str, Any] = {"kind": "text", "text": text}
    if color is not None:
        scene["color"] = color
    return event(source, scene, priority=priority, duration=duration)


def visual(
    source: str,
    name: str,
    *,
    priority: str = "normal",
    duration: float | None = None,
) -> Event:
    """A built-in visual by name; rendered here and sent as frames."""
    return event(
        source, {"kind": "visual", "name": name}, priority=priority, duration=duration
    )
