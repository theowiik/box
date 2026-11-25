# firmware

The box as an [ESP-Mosaico](https://mosaico.ai/en) application: an HTTP API on
Wi-Fi, the scheduler, and a 96×96 LED panel drawn on the 480×480 screen.

## Building

It builds against an [esp-mosaico-vibe](https://github.com/esp-mosaico/esp-mosaico-vibe)
workspace, which supplies the board support package, ESP-Iris (USB transport,
screenshots, logs, Recovery) and the install tooling. Set that workspace up
per its README, including its pinned ESP-IDF, then in the repo's `.env`:

```ini
MOSAICO_WORKSPACE=/path/to/esp-mosaico-vibe
MOSAICO_SHELL=/path/to/wrapper   # only if ESP-IDF isn't loaded in your shell
```

`make firmware` builds; `make install` builds and installs over USB with the
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
| `CONFIG_BOX_PANEL_SIZE` | `96` | LED dots per side; a divisor of 480 fills the screen |
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
render task (25 fps): tick ─► render 96×96 frame ─► box_led_update ─► presenter ─► CO5300
                                                     (changed cells)   (TE sync)
ESP-Iris task: iris screenshot ◄── copy of the LED screen image
```

- **`box_core`** is portable C (cJSON is its only dependency) and has the
  whole behaviour: the event contract (`event.c`), the scheduler, scenes, the
  crossfading engine and the LED renderer. `host/` builds it on a PC and runs
  its tests under AddressSanitizer: `make test-firmware`.
- **`box_device`** is the ESP-IDF side. `runtime.c` owns the core objects and
  the render task, behind one lock; `display.c` presents through
  `esp_display_present` with tear-effect sync, and serves `iris screenshot`
  from the same image it pushes; `net.c` joins Wi-Fi and announces `box.local`
  (and ESP-Iris) over mDNS; `http.c` is the API.
- **The LED look** turns each logical pixel into an antialiased dot on a 5 px
  grid that fills the screen, with a faint glow for unlit dots. Only changed
  cells are redrawn and pushed, so still text costs nothing after it appears.
- **Text** is laid out once per scene (`box_text_layout`): word-wrapped at the
  largest whole multiple of the 5×7 font that fits, never scrolled.

Measured on the board: a full-frame push takes about 24 ms on the 40 MHz QSPI
bus, so the tear-synced presenter delivers every frame at 25 fps, and
full-panel animations at their own rate. `GET /api/display` reports
the live numbers as `drawn_fps` and `shown_fps`.

## Limits

| | |
| --- | --- |
| Queue | 32 waiting events (one per source) |
| Ambient rotation | 16 sources |
| One animation | 4 MiB of pixels (~150 frames at 96×96; smaller frames are scaled up) |
| All animations held at once | 12 MiB |
| Text | 512 characters; letters (shown as capitals), digits, common punctuation and `ÅÄÖÜÉ°`, anything else as `?` |

Over a limit is a 503 or 422 with a reason, never a crash.
