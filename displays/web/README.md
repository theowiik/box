# Web page display

The box in a browser: the portable core ([`../../core`](../../core)) with a
small single-threaded HTTP server. It serves the same API as any box, and
its screen is a canvas on a web page. Use it to try the box without
hardware, to develop a connector, or to check a change to the core.

```bash
mise run web                       # http://localhost:8080
mise run web -- --style led        # options: --port, --size (pixels a side), --style
BOX_URL=http://localhost:8080 mise run send -- --title Hi hello
```

The page polls `GET /frame?since=N`, which returns the screen as raw RGB once
it is newer than version N (headers `X-Frame`, `X-Size`, `X-Brightness`), and
has a small form to send text. Brightness dims the canvas. The clock uses
this machine's local time.

| | |
| --- | --- |
| `main.c` | The display (frames into a buffer), the API routes, the loop |
| `page.html` | The screen and the test form, embedded into the binary at build time |
