# box-services

Everything that talks to the [box](../../README.md) and stays off it, as one Go
program (standard library only):

```bash
box-services run                          # weather and feeds, until stopped
box-services send --title Hi "hello"      # some text now
box-services visual beer                  # rainbow, life, gradient, beer
box-services gif party.gif                # a GIF
box-services status                       # what is on screen
box-services display --style led          # what the box can do; change settings
box-services clear --ambient              # drop the queue and the idle rotation
```

From the repo root: `mise run send -- …`, `mise run demo -- beer`, `mise run gif -- party.gif`.
One-off commands take `--source`, `--priority`, `--duration` and `--style`,
before or after their argument.

## On a server that stays on

`run` is what the server does, in Docker:

```bash
docker compose up -d --build    # from the repo root, or: mise run server
docker compose logs -f box-services
```

The image is a static binary on distroless (about 9 MB). Compose uses the host
network so `box.local` is found over mDNS, and keeps feed history in a named
volume. Without avahi (as in a container), the client asks the network for
`box.local` itself.

| Setting | Default | |
| --- | --- | --- |
| `BOX_URL` | `http://box.local` | Or the box's IP |
| `BOX_TOKEN` | none | If the firmware requires one |
| `BOX_CITY` | Stockholm | The (mock) weather |
| `BOX_FEEDS` | on | `0` for weather only |
| `BOX_STATE_DIR` | `~/.local/state/box` (`/state` in Docker) | Which feed items were already shown |

Environment variables, or a `.env` at the repo root.

## Layout

| | |
| --- | --- |
| `main.go` | The command line |
| `internal/client` | The box's API: events, status, settings; the mDNS lookup |
| `internal/sources` | Weather and feeds, and the runner that polls them |
| `internal/visuals` | The built-in animations, rendered to RGB frames |
| `internal/gifs` | GIF to RGB frames, sized for the box |

A new source implements `sources.Source` (a name, an interval, a `Poll`) and
goes in the list in `main.go`; a new feed is one line in `sources.Feeds`. The
box keeps one pending event per source and refreshes ambient cards in place,
so sources can poll freely: repeats replace, never pile up.
