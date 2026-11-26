# box-essentials

The basics the [box](../../README.md) shows when nothing else is on, from a
server that stays on: the **weather** where you are (from
[Open-Meteo](https://open-meteo.com), no key needed) and **feeds** (RSS or
Atom). It has a **settings page** for them. The same Go program (standard
library only) sends one-offs from the command line. The clock is not here: the
box keeps time itself, so it shows the time even when this is down.

```bash
box-essentials run                          # weather, feeds and the settings page, until stopped
box-essentials send --title Hi "hello"      # some text now
box-essentials visual beer                  # rainbow, life, gradient, beer
box-essentials gif party.gif                # a GIF
box-essentials status                       # what is on screen
box-essentials display --style led          # what the box can do; change settings
box-essentials clear --ambient              # drop the queue and the idle rotation
```

From the repo root: `mise run send -- …`, `mise run demo -- beer`, `mise run gif -- party.gif`.
One-off commands take `--source`, `--priority`, `--duration` and `--style`,
before or after their argument.

## The settings page

`run` serves it on **:8090** (http://the-server:8090). There you:

- pick the weather's place (search by name), Celsius or Fahrenheit, and how often to check;
- add, change or turn off feeds: the title on screen, the address, whether
  something new shows its title or plays a visual, its priority;
- set where the box is, if `box.local` does not resolve;
- see what the box is showing and how each source's last check went, change
  the box's style and brightness, and send it a message.

Saving starts the sources over at once, so a new place shows its weather
right away. The settings are kept in `settings.json` in the state directory;
a fresh install shows the weather in Stockholm and follows En Stor Stark
Review. The page has no login: it is for your own network. It refuses
requests made from other sites' pages, so a web page elsewhere cannot drive
the box through your browser.

## On a server that stays on

`run` is what the server does, in Docker:

```bash
docker compose up -d --build    # from the repo root, or: mise run server
docker compose logs -f box-essentials
```

The image is a static binary on distroless (about 9 MB). Compose uses the host
network so `box.local` is found over mDNS (and the page is on the server's
port 8090), and keeps the settings and feed history in a named volume.
Without avahi (as in a container), the client asks the network for
`box.local` itself.

| Setting | Default | |
| --- | --- | --- |
| `BOX_URL` | `http://box.local` | Or the box's IP; the page can set it too |
| `BOX_TOKEN` | none | If the firmware requires one |
| `BOX_WEB_ADDR` | `:8090` | Where the settings page listens |
| `BOX_STATE_DIR` | `~/.local/state/box` (`/state` in Docker) | The settings and feed history |

Environment variables, or a `.env` at the repo root.

## Layout

| | |
| --- | --- |
| `main.go`, `serve.go` | The command line; `run` |
| `internal/client` | The box's API: events, status, settings; the mDNS lookup |
| `internal/settings` | What `run` is set to do, validated and kept on disk |
| `internal/service` | Runs the sources the settings ask for; starts them over on a change |
| `internal/web` | The settings page (`index.html`, embedded) and its JSON API |
| `internal/sources` | Weather and feeds, and the runner that polls them |
| `internal/visuals` | The built-in animations, rendered to RGB frames |
| `internal/gifs` | GIF to RGB frames, sized for the box |

A new kind of source implements `sources.Source` (a name, an interval, a
`Poll`), gets a section in `settings.Settings`, and is turned on in
`service.Sources`; the page edits the settings as JSON. The box keeps one
pending event per source and refreshes ambient cards in place, so sources can
poll freely: repeats replace, never pile up.
