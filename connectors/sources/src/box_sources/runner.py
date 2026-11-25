"""Polls sources on their own timers and posts what they produce to the box.

Each source runs on its own thread: a feed timing out on a dead network must
not hold up the weather. Nothing here depends on the box being up — a poll
whose event cannot be delivered is logged and the next poll tries again, so
the service rides out box reboots and Wi-Fi drops without a restart.
"""

import logging
import threading
from typing import Protocol

from .client import Box, BoxError
from .events import Event

logger = logging.getLogger(__name__)

#: Floor on any source's interval: a typo must not become a hot loop.
MIN_INTERVAL = 1.0


class Source(Protocol):
    """Something polled for content. Structural, no base class."""

    #: Identifies the source on the box: its ambient card, colour, log lines.
    name: str
    #: Seconds between polls.
    interval: float

    def poll(self) -> Event | None:
        """Something to show, or None for nothing new. May block."""
        ...


def poll_once(box: Box, source: Source) -> bool:
    """Poll one source and deliver its event. True if something was sent."""
    try:
        event = source.poll()
    except Exception:
        # One broken source must not stop the others or kill its loop.
        logger.exception("source %r failed to poll", source.name)
        return False
    if event is None:
        return False
    try:
        reply = box.send(event)
    except BoxError as exc:
        logger.warning("%s: %s", source.name, exc)
        return False
    logger.info("%s -> %s (%s)", source.name, reply.get("priority"), reply.get("id"))
    return True


def run(box: Box, sources: list[Source], stop: threading.Event) -> None:
    """Poll every source until `stop` is set. Each polls at once, then on its timer."""

    def loop(source: Source) -> None:
        interval = max(source.interval, MIN_INTERVAL)
        while not stop.is_set():
            poll_once(box, source)
            stop.wait(interval)

    threads = [
        threading.Thread(target=loop, args=(s,), name=f"source-{s.name}", daemon=True)
        for s in sources
    ]
    if sources:
        logger.info(
            "posting to %s: %s",
            box.url,
            ", ".join(f"{s.name} every {s.interval:.0f}s" for s in sources),
        )
    for thread in threads:
        thread.start()
    stop.wait()
    for thread in threads:
        thread.join(timeout=5)
