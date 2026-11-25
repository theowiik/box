# AGENTS.md

Guidance for coding agents (and humans) working in this repository.

## Commands

The root Makefile is the front door. Machine settings live in `.env` (see
`.env.example`): `MOSAICO_WORKSPACE` for firmware builds, `MOSAICO_SHELL` if
ESP-IDF is not loaded in the shell, `BOX_URL` if `box.local` does not resolve.

- `make check` — firmware core tests on the host (ASan/UBSan) + connectors' lint and tests
- `make test-firmware` — just the core tests
- `make firmware` / `make install` — build / build and install on the board (USB)
- `make logs`, `make screenshot`, `make status` — observe the running box
- `make send TEXT=...`, `make demo VISUAL=beer`, `make gif GIF=...`, `make sources`
- In a connector: `uv run pytest tests/test_feeds.py::test_a_new_item_is_shown_once`

## Project Structure

Monorepo. `firmware/` is the box; `connectors/` are separate uv projects that
talk to it over HTTP from any machine.

```
Makefile, .env.example
firmware/                        # The box: ESP-IDF app for the ESP-Mosaico
├── CMakeLists.txt               # Builds against $MOSAICO_WORKSPACE (esp-mosaico-vibe)
├── partitions.csv               # The Mosaico template's table; never reshape it
├── sdkconfig*.defaults          # Template defaults + Wi-Fi/HTTP additions
├── main/main.c                  # Boot: Recovery support, runtime, network, HTTP
├── components/
│   ├── box_core/                # PORTABLE C — no ESP-IDF headers; cJSON only
│   │   ├── include/box/
│   │   │   ├── event.h          # The event contract: JSON -> validated event
│   │   │   ├── scheduler.h      # Priorities, queue, coalescing, ambient rotation
│   │   │   ├── scene.h          # blank/solid/text/frames; refcounted
│   │   │   ├── engine.h         # Active scene + crossfade -> logical frame
│   │   │   ├── led.h            # Logical frame -> LED dots on screen, dirty rect
│   │   │   └── frame.h, text.h  # Pixels, 5x7 font, UTF-8
│   │   └── src/font_table.inc   # Generated from the original Python font
│   ├── box_device/              # Everything ESP-IDF/FreeRTOS/hardware
│   │   ├── runtime.c            # Owns the core + render task; the lock
│   │   ├── display.c            # esp_display_present (TE sync), iris screenshot
│   │   ├── net.c                # Wi-Fi from Vibe Mode creds, mDNS box.local
│   │   └── http.c               # The HTTP API
│   └── app_config/              # Template's Vibe Mode settings reader (+brightness)
└── host/                        # Host build of box_core + tests (FetchContent cJSON)
connectors/
├── sources/                     # box-sources: weather, feeds, visuals (stdlib only)
│   ├── src/box_sources/         # client, events, runner, feeds, weather, visuals/
│   └── deploy/box-sources.service   # systemd --user unit
└── gifcast/                     # GIF -> frames
```

## Rules

- **The core stays portable.** `box_core` includes no ESP-IDF, FreeRTOS or
  hardware headers; the host build in `firmware/host` compiles it on a PC and
  fails if it does. Hardware and ESP-IDF calls belong in `box_device`.
- **Time is a parameter.** Scheduler and engine take `now`; tests drive time.
- **Text never scrolls.** `box_text_layout` wraps it and picks the largest
  font scale that fits, keeping words whole when any size allows it; only text
  too long for scale 1 is cut with `...`. Keep it that way.
- **One lock, explicit ownership.** The scheduler and engine are single-
  threaded; `runtime.c` serializes every call. Scenes are refcounted because
  the engine keeps the outgoing scene for its crossfade after the scheduler
  frees the event.
- **The render task owns the display bus.** Brightness is a panel command on
  the same QSPI bus as pixels; sending it from another task mid-transfer hung
  the bus and reset the board. Anything that talks to the panel goes through
  the render task (see `pending_brightness`).
- **Bounded memory.** Queue (32), ambient rotation (16), one upload (4 MiB)
  and all animation data together (12 MiB) are capped; over a cap is a 503, never
  a crash.
- **Device operations only through `mosaico.py`** (`make install` etc.),
  following the esp-mosaico-vibe workspace's AGENTS.md: no `idf.py flash`, no
  esptool writes, no partition changes. `iris app-update` fails here (its full
  build trips a bootloader size check at the pinned ESP-IDF); use
  `system-update`, which `make install` does.
- **Acceptance needs live evidence:** `make status` over Wi-Fi and a
  `make screenshot`, not just a successful upload.
- **Connectors send what happened, the box decides how it looks.** Source
  colours live in `box_core/src/event.c`. `box-sources` uses only the standard
  library so it runs on any machine with Python.
