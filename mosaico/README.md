# mosaico

The box on an [ESP-Mosaico](https://mosaico.ai/en): the portable core in
[`../core`](../core) plus this board's screen, Wi-Fi, HTTP API and clock.

## Building

It builds against an [esp-mosaico-vibe](https://github.com/esp-mosaico/esp-mosaico-vibe)
workspace, which supplies the board support package, ESP-Iris (USB transport,
screenshots, logs, Recovery) and the install tooling. Set that workspace up
per its README, including its pinned ESP-IDF, then in the repo's `.env`:

```ini
MOSAICO_WORKSPACE=/path/to/esp-mosaico-vibe
MOSAICO_SHELL=/path/to/wrapper   # only if ESP-IDF isn't loaded in your shell
```

`mise run firmware` builds; `mise run install` builds and installs over USB with the
workspace's `iris system-update`, which keeps the Mosaico's Vibe Mode,
bootloader and partition layout intact. (`iris app-update` does not work at the
workspace's pinned ESP-IDF: its full build fails the bootloader size check.)

The app replaces whatever Idea the board was running. To go back to Vibe Mode,
hold the AI button while powering on.

## Configuration

Build options are in `menuconfig` under *Box*, or as defaults in a
gitignored `sdkconfig.local.defaults` next to this file:

| Option | Default | |
| --- | --- | --- |
| `CONFIG_BOX_HOSTNAME` | `box` | Answers at `<hostname>.local` |
| `CONFIG_BOX_HTTP_PORT` | `80` | |
| `CONFIG_BOX_TOKEN` | empty | Require `Authorization: Bearer <token>` on writes |
| `CONFIG_BOX_STYLE` | `smooth` | Style at boot: `smooth` or `led` (`PATCH /api/display` changes it live) |
| `CONFIG_BOX_LED_GRID` | `96` | LEDs per side in the `led` style; a divisor of 480 fills the screen |
| `CONFIG_BOX_TIMEZONE` | Central European | POSIX TZ for the idle clock |
| `CONFIG_BOX_NTP_SERVER` | `pool.ntp.org` | |
| `CONFIG_BOX_FPS` | `25` | Render rate; keep it a whole number of 10 ms ticks |

A build keeps the options it was configured with: after changing them, delete
`build/sdkconfig` (or `build/`) so the defaults apply again.

Wi-Fi and the boot brightness come from the Mosaico's Vibe Mode settings,
read-only: set them up there. The box never stores credentials of its own.

## How it works

```
HTTP task ──► box_event_parse ──┐
                                ├─► runtime lock ─► scheduler ─► engine
Wi-Fi events (status cards) ────┘                                  │
                                                                   ▼
render task (25 fps): box_render ─► 480×480 RGB frame, only when it changed
                      box_show ───► mosaico_display: RGB565, changed rectangle ─► presenter (TE sync)
ESP-Iris task: iris screenshot ◄── the same screen image
```

- **`core/`** has the behaviour: the event contract, the scheduler, the
  scenes and their two styles (`smooth`: Inter text and fitted images at full
  resolution; `led`: square LEDs and a 5×7 font), and the engine, which
  renders only when something changed. It builds and is tested on any PC.
- **`box_device`** is the ESP-IDF side. `runtime.c` owns the core and the
  render task behind one lock, and feeds the idle clock from network time;
  `mosaico_display.c` implements the core's display interface on the CO5300
  through `esp_display_present`; `net.c` joins Wi-Fi, announces `box.local`
  (and ESP-Iris) over mDNS and starts NTP; `http.c` is the API.

Measured on the board: a full-frame push takes about 24 ms on the 40 MHz QSPI
bus, so the tear-synced presenter keeps up with 25 fps. Text and still images
are drawn once and cost nothing after that. `GET /api/status` reports the live
`fps.drawn` and `fps.shown`.

## Limits

| | |
| --- | --- |
| Queue | 32 waiting events (one per source) |
| Ambient rotation | 16 sources |
| One image | 4 MiB of RGB frames (small images are fine: they are scaled up) |
| All images held at once | 12 MiB |
| Text | 512 characters, title 80; Latin-1 plus common punctuation |

Over a limit is a 503 or 422 with a reason, never a crash.
