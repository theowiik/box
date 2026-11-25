# box-sources

Weather, RSS/Atom feeds and the built-in visuals for the [box](../../README.md).
They run here — on any machine that stays on — and POST to the box, which only
receives and draws.

```bash
uv run box-sources run                 # weather and feeds, until stopped
uv run box-sources visual beer         # rainbow, life, gradient, beer
uv run box-sources send "hello" --priority alert
```

Standard library only: any machine with Python and uv can run it. If the one
running it goes down, start it on another; feed state is the only thing it
keeps (which items were already shown), in `BOX_STATE_DIR`.

| Setting | Default | |
| --- | --- | --- |
| `BOX_URL` | `http://box.local` | Or the box's IP |
| `BOX_TOKEN` | none | If the firmware requires one |
| `BOX_CITY` | Stockholm | The (mock) weather |
| `BOX_FEEDS` | on | `0` for weather only |
| `BOX_STATE_DIR` | `~/.local/state/box` | Seen feed items |

Environment variables, or a `.env` at the repo root.

## Running it as a service

[`deploy/box-sources.service`](deploy/box-sources.service) is a systemd user
unit. It expects the repo at `~/git/box`; adjust `WorkingDirectory` otherwise.

```bash
mkdir -p ~/.config/systemd/user
cp deploy/box-sources.service ~/.config/systemd/user/
systemctl --user enable --now box-sources
journalctl --user -u box-sources -f
```

To keep it running while logged out: `loginctl enable-linger $USER`.

## Adding a source

A source is anything with a `name`, an `interval` in seconds and a `poll()`
that returns an event dict (see `events.py`) or `None`. Add it to `_sources()`
in `cli.py`. A feed is one line in `FEEDS` in `feeds.py`.

The box keeps one pending event per source and refreshes ambient cards in
place, so a source can poll freely: repeats replace, never pile up.
