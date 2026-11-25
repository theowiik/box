"""A fake box: the device's HTTP API, enough to test the client and runner."""

import http.server
import json
import os
import threading

import pytest


class FakeBox:
    def __init__(self) -> None:
        self.events: list[dict] = []
        self.token = ""
        self.status = 200  # answer POST /api/events with this
        self.width = self.height = 32
        self.max_frames_bytes = 4 * 1024 * 1024
        self.cleared = 0


@pytest.fixture
def box():
    state = FakeBox()

    class Handler(http.server.BaseHTTPRequestHandler):
        def _reply(self, status: int, body: dict) -> None:
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _authorized(self) -> bool:
            if (
                state.token
                and self.headers.get("Authorization") != f"Bearer {state.token}"
            ):
                self._reply(401, {"detail": "Bearer token required"})
                return False
            return True

        def do_GET(self):
            if self.path == "/api/display":
                self._reply(
                    200,
                    {
                        "width": state.width,
                        "height": state.height,
                        "max_frames_bytes": state.max_frames_bytes,
                    },
                )
            else:
                self._reply(404, {"detail": "not found"})

        def do_POST(self):
            if not self._authorized():
                return
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            if state.status != 200:
                self._reply(state.status, {"detail": "nope"})
                return
            state.events.append(body)
            priority = body.get("priority", "normal")
            self._reply(
                200,
                {
                    "id": "abc",
                    "source": body.get("source", "api"),
                    "priority": priority,
                    "duration": body.get("duration", 8),
                    "queued": 0,
                },
            )

        def do_DELETE(self):
            if self._authorized():
                state.cleared += 1
                self._reply(200, {"status": "ok"})

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state.url = f"http://127.0.0.1:{server.server_port}"
    yield state
    server.shutdown()


@pytest.fixture(autouse=True)
def clean_env(monkeypatch):
    """Tests never pick up a developer's real BOX_* settings."""
    for key in [k for k in os.environ if k.startswith("BOX_")]:
        monkeypatch.delenv(key)
