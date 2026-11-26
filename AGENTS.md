# AGENTS.md

Guidance for coding agents (and humans) working in this repository.

## Commands

`mise.toml` is the front door: `mise tasks` lists them, and it pins Go
(`mise install`). It loads machine settings from `.env` (see `.env.example`):
`MOSAICO_WORKSPACE` for firmware builds, `MOSAICO_SHELL` if ESP-IDF is not
loaded in the shell, `BOX_URL` if `box.local` does not resolve.

- `mise run check` — the core's tests on the host (ASan/UBSan) + box-services' gofmt, vet, tests
- `mise run test-core` / `mise run test-services` — one side of that
- `mise run firmware` / `mise run install` — build / build and install on the board (USB)
- `mise run logs`, `mise run screenshot`, `mise run status` — observe the running box
- `mise run send -- --title T TEXT`, `mise run demo -- beer`, `mise run gif -- FILE` (arguments after `--` go to box-services)
- `mise run server` — box-services in Docker (on the always-on server)
- One Go test: `cd connectors/box-services && go test ./internal/sources -run TestFeedLifecycle`

## Project Structure

```
core/                     # The box's logic, portable C (no ESP-IDF); tests in core/tests
├── include/box/
│   ├── event.h           # The API's event contract: JSON -> validated scene
│   ├── scheduler.h       # Priorities, queue, coalescing, ambient rotation
│   ├── scene.h           # Content (clear/color/text/image/clock), drawn per style
│   ├── engine.h          # Renders only on change; crossfades from what was shown
│   ├── font.h            # Smooth text: Inter via stb_truetype, wrap-and-fit cards
│   ├── text.h, led.h     # The led style: 5x7 bitmap font, square LED blocks
│   ├── api.h             # The HTTP API's JSON (status, display, settings)
│   ├── box.h, display.h  # The whole box minus I/O; the display interface
├── fonts/, third_party/  # Inter (OFL, subset; fonts/embed.py), stb_truetype
mosaico/                  # The ESP-Mosaico target (ESP-IDF)
├── main/main.c           # Boot: Recovery support, runtime, network, HTTP
└── components/
    ├── box_device/       # runtime (lock, render task), display, Wi-Fi/mDNS/NTP, HTTP
    └── app_config/       # Vibe Mode settings, read-only (Wi-Fi, brightness)
connectors/box-services/  # Go, stdlib only: CLI + always-on service (weather, feeds,
                          #   visuals, GIFs); Dockerfile
compose.yaml              # box-services on the server
```

## Rules

- **The API describes content, not pixels.** Senders say text/title/image/
  colour; the box lays out, scales and styles. Keep new features in that
  shape, and keep the box dumb: anything that needs the internet or opinions
  about content belongs in box-services.
- **The core stays portable.** `core/` includes no ESP-IDF, FreeRTOS or
  hardware headers; its CMake build compiles it on a PC and fails if it does.
  ESP-IDF and hardware calls belong in `mosaico/components/box_device`.
- **Time is a parameter.** Scheduler and engine take `now`; tests drive time.
- **Text never scrolls.** Both styles wrap and size text to fit, keeping
  words whole when any size allows; only text too long for the smallest size
  is cut with an ellipsis.
- **Render only on change.** A scene's version changes only when its look
  does; the engine returns NULL otherwise, and the display pushes only the
  changed rectangle. A still screen costs nothing.
- **One lock, explicit ownership.** The scheduler and engine are single-
  threaded; `runtime.c` serializes every call. Scenes are refcounted.
- **The render task owns the display bus.** Brightness is a panel command on
  the same QSPI bus as pixels; sending it from another task mid-transfer hung
  the bus and reset the board. Anything that talks to the panel goes through
  the render loop (`box_request_brightness` / `box_show`).
- **Bounded memory.** Queue (32), ambient rotation (16), one image upload
  (4 MiB) and all image data together (12 MiB) are capped; over a cap is a
  503, never a crash.
- **Device operations only through `mosaico.py`** (`mise run install` etc.),
  following the esp-mosaico-vibe workspace's AGENTS.md: no `idf.py flash`, no
  esptool writes, no partition changes. `iris app-update` fails here (its full
  build trips a bootloader size check at the pinned ESP-IDF); use
  `system-update`, which `mise run install` does. After changing Kconfig defaults,
  delete `mosaico/build/sdkconfig`.
- **Acceptance needs live evidence:** `mise run status` over Wi-Fi and a
  `mise run screenshot`, not just a successful upload.
- **box-services is standard-library Go** so it builds anywhere into one
  static binary; keep it that way.
- **No personal machine names** in anything committed: say "a server that
  stays on", not a hostname.
