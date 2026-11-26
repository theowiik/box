# AGENTS.md

Guidance for coding agents (and humans) working in this repository.

## Setup

A machine needs Docker, git and mise; everything else is pinned in
`toolchain/versions` and fetched by `mise run setup` (any task does it on
first use):

- **The ESP-Mosaico workspace** (`esp-mosaico-vibe`: board support, ESP-Iris,
  `mosaico.py`, docs) is checked out in **`.deps/esp-mosaico-vibe`**
  (gitignored; `toolchain/fetch`). Read it from there: its `AGENTS.md` and
  `docs/` for how the board and device tooling work,
  `submodule/esp-mosaico-bsp` for the board's drivers,
  `submodule/esp-mosaico-utils/ESP-Iris` for the USB/Wi-Fi device link.
  Install, log and screenshot runs leave their logs in its `.codex-runs/`.
  Never edit it: change the pin instead.
- **ESP-IDF and Go** are in the dev image (`toolchain/Dockerfile`), with
  ESP-IDF at `/opt/esp/idf` inside it. Read IDF sources with
  `toolchain/run sh -c 'grep -rn … $IDF_PATH/components/…'`.

Run tools through mise or `toolchain/run CMD` (for example
`toolchain/run idf.py --version`), never from the host: the host may have no
ESP-IDF or Go, or the wrong versions. To move a pin, edit
`toolchain/versions`: the next task fetches it and rebuilds the image.

## Commands

`mise.toml` is the front door: `mise tasks` lists them. mise only launches:
every task runs in the dev container, started by `toolchain/run`. The
container keeps running between tasks (`docker exec` into it) because the
Mosaico tools' device gateway must outlive each command; `mise run shell`
opens a shell in it. `.env` (see `.env.example`) holds `BOX_*` settings, e.g.
`BOX_URL` if `box.local` does not resolve.

- `mise run setup` — fetch the workspace, build the dev image, print versions

- `mise run check` — the core's tests on the host (ASan/UBSan) + box-essentials' gofmt, vet, tests
- `mise run test-core` / `mise run test-essentials` — one side of that
- `mise run web` — the box in a browser on :8080 (no hardware); send to it with `BOX_URL=http://localhost:8080`
- `mise run firmware` / `mise run install` — build / build and install on the board (USB)
- `mise run logs`, `mise run screenshot`, `mise run status` — observe the running box
- `mise run send -- --title T TEXT`, `mise run demo -- beer`, `mise run gif -- FILE` (arguments after `--` go to box-essentials)
- `mise run server` — box-essentials in Docker (on the always-on server); its settings page is on :8090
- `mise run essentials` — the same here, in the foreground (page on localhost:8090)
- One Go test: `cd connectors/box-essentials && go test ./internal/sources -run TestFeedLifecycle`

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
displays/                 # What the box draws on: each one is the core + a screen + the API
├── web/                  # The box in a browser (C: tiny HTTP server, page.html canvas)
└── mosaico/              # The ESP-Mosaico (ESP-IDF)
    ├── main/main.c       # Boot: Recovery support, runtime, network, HTTP
    └── components/
        ├── box_device/   # runtime (lock, render task), display, Wi-Fi/mDNS/NTP, HTTP
        └── app_config/   # Vibe Mode settings, read-only (Wi-Fi, brightness)
connectors/box-essentials/ # Go, stdlib only: the always-on basics (weather, feeds) and their
                          #   settings page (internal/web), plus the CLI (visuals, GIFs); Dockerfile
compose.yaml              # box-essentials on the server
toolchain/                # The dev container: versions (the pins), Dockerfile, fetch, run (what mise calls)
.deps/                    # Fetched, gitignored: esp-mosaico-vibe (the Mosaico workspace)
```

## Rules

- **The API describes content, not pixels.** Senders say text/title/image/
  colour; the box lays out, scales and styles. Keep new features in that
  shape, and keep the box dumb: anything that needs the internet or opinions
  about content belongs in box-essentials.
- **Displays are interchangeable.** Anything a display needs from the core
  goes through `box.h`/`display.h`/`api.h`; if a feature only works on one
  display, it belongs in that display. Check new core behaviour on
  `mise run web` as well as the board.
- **The core stays portable.** `core/` includes no ESP-IDF, FreeRTOS or
  hardware headers; its CMake build compiles it on a PC and fails if it does.
  ESP-IDF and hardware calls belong in `displays/mosaico/components/box_device`.
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
  delete `displays/mosaico/build/sdkconfig`.
- **Acceptance needs live evidence:** `mise run status` over Wi-Fi and a
  `mise run screenshot`, not just a successful upload.
- **box-essentials is standard-library Go** so it builds anywhere into one
  static binary; keep it that way.
- **No personal machine names** in anything committed: say "a server that
  stays on", not a hostname.
