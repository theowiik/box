"""`box-sources`: run the box's weather and feeds, or send something once.

    box-sources run                 # weather and feeds, until stopped
    box-sources visual beer         # play a built-in visual now
    box-sources send "hello"        # show some text now

Configuration is BOX_* environment variables, or a `.env` at the repo root:
BOX_URL (default http://box.local), BOX_TOKEN, BOX_CITY, BOX_STATE_DIR and
BOX_FEEDS=0 to leave the feeds out.
"""

import argparse
import logging
import os
import signal
import sys
import threading
from pathlib import Path

from . import events, runner, visuals
from .client import Box, BoxError, load_dotenv
from .feeds import FEEDS, FeedSource
from .weather import MockWeather


def _state_dir() -> Path:
    """Where feed seen-state lives: BOX_STATE_DIR, else the XDG state dir."""
    explicit = os.getenv("BOX_STATE_DIR")
    if explicit:
        return Path(explicit).expanduser()
    base = os.getenv("XDG_STATE_HOME") or Path.home() / ".local" / "state"
    return Path(base) / "box"


def _sources() -> list[runner.Source]:
    sources: list[runner.Source] = [MockWeather(os.getenv("BOX_CITY") or "Stockholm")]
    if os.getenv("BOX_FEEDS", "1") != "0":
        state = _state_dir()
        sources += [FeedSource(config, state) for config in FEEDS]
    return sources


def _run(box: Box, _: argparse.Namespace) -> int:
    stop = threading.Event()
    # systemd stops services with SIGTERM; treat it like Ctrl-C.
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    try:
        runner.run(box, _sources(), stop)
    except KeyboardInterrupt:
        stop.set()
    return 0


def _visual(box: Box, args: argparse.Namespace) -> int:
    event = events.visual(
        args.source, args.name, priority=args.priority, duration=args.duration
    )
    reply = box.send(event)
    print(f"{args.name}: {reply['priority']} for {reply['duration']:g}s")
    return 0


def _send(box: Box, args: argparse.Namespace) -> int:
    event = events.text(
        args.source,
        args.text,
        color=args.color,
        priority=args.priority,
        duration=args.duration,
    )
    reply = box.send(event)
    queued = reply["queued"]
    print(f"sent: {reply['priority']} for {reply['duration']:g}s, {queued} queued")
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="box-sources", description=__doc__.split("\n")[0]
    )
    parser.add_argument(
        "--url", help="the box (default: BOX_URL, else http://box.local)"
    )
    parser.add_argument("--token", help="bearer token (default: BOX_TOKEN)")
    parser.add_argument("-v", "--verbose", action="store_true")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("run", help="poll weather and feeds, posting to the box")

    def once(name: str, help: str) -> argparse.ArgumentParser:
        sub = commands.add_parser(name, help=help)
        sub.add_argument(
            "--source", default="cli", help="who it is from (default: cli)"
        )
        sub.add_argument("--priority", default="normal", choices=events.PRIORITIES)
        sub.add_argument("--duration", type=float, help="seconds on the panel")
        return sub

    visual = once("visual", "play a built-in visual now")
    visual.add_argument("name", choices=sorted(visuals.VISUALS))
    send = once("send", "show some text now")
    send.add_argument("text")
    send.add_argument("--color", help="#RRGGBB (default: styled by the box)")
    return parser


def main(argv: list[str] | None = None) -> int:
    load_dotenv()
    args = _parser().parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s",
    )
    box = Box(args.url, args.token)
    command = {"run": _run, "visual": _visual, "send": _send}[args.command]
    try:
        return command(box, args)
    except BoxError as exc:
        print(f"box-sources: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
