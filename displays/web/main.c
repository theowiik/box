// The box in a browser: the same core and the same HTTP API as on any other
// display, with the screen drawn on a web page. For trying the box without
// hardware, testing connectors, and keeping the core honest about being
// generic.
//
//   box-web [--port 8080] [--size 480] [--style smooth|led]
//
// Then open http://localhost:8080 for the screen, and send events to
// http://localhost:8080/api/events as you would to a real box.
//
// One thread: a loop that waits for a request or the next frame, whichever
// comes first. Requests are handled one at a time, which is plenty here.
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "box/api.h"
#include "box/box.h"
#include "box/event.h"
#include "page.inc" /* page_html, page_html_len: page.html */

#define FPS 25
#define MAX_HEADER 8192
/* An image upload is base64 (4/3 of its bytes) inside a little JSON. */
#define MAX_BODY (BOX_IMAGE_MAX_BYTES / 3 * 4 + 64 * 1024)
#define IMAGE_BUDGET (12u * 1024u * 1024u)
static const box_rgb_t CLOCK_COLOR = {0xFF, 0x8A, 0x3D};

/* The display: frames land in `screen`, which GET /frame serves. */
static struct {
    int size;
    uint8_t *screen;
    uint64_t version; /* bumped per new frame; the page asks for newer ones */
    float brightness;
    uint64_t shown;
} web;

static bool web_show(void *ctx, const box_frame_t *frame)
{
    memcpy(web.screen, frame->pixels, box_frame_bytes(frame));
    web.version++;
    web.shown++;
    return true;
}

static bool web_set_brightness(void *ctx, float brightness)
{
    web.brightness = brightness;
    web.version++; /* the page dims the canvas: it needs to hear about it */
    return true;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool local_time(void *ctx, int *hour, int *minute)
{
    const time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    *hour = tm.tm_hour;
    *minute = tm.tm_min;
    return true;
}

static void log_line(void *ctx, const char *line)
{
    fprintf(stderr, "box: %s\n", line);
}

/* --- HTTP: just enough of it ------------------------------------------- */

static bool write_all(int fd, const void *data, size_t len)
{
    const char *p = data;
    while (len > 0) {
        const ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static const char *reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 422: return "Unprocessable Entity";
    case 503: return "Service Unavailable";
    }
    return "Internal Server Error";
}

static void respond(int fd, int status, const char *type, const void *body, size_t len,
                    const char *extra_headers)
{
    char head[512];
    const int n = snprintf(head, sizeof(head),
                           "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n",
                           status, reason(status), type, len, extra_headers ? extra_headers : "");
    if (write_all(fd, head, (size_t)n) && len > 0) {
        write_all(fd, body, len);
    }
}

/* Sends and frees a JSON body from box_api_*. */
static void respond_json(int fd, int status, char *json)
{
    if (!json) {
        static const char oom[] = "{\"detail\":\"out of memory\"}";
        respond(fd, 503, "application/json", oom, sizeof(oom) - 1, NULL);
        return;
    }
    respond(fd, status, "application/json", json, strlen(json), NULL);
    box_api_free(json);
}

static void respond_error(int fd, int status, const char *message)
{
    respond_json(fd, status, box_api_error(message));
}

typedef struct {
    char method[8];
    char path[256];
    char *body; /* NUL-terminated; malloc'd */
    size_t body_len;
} request_t;

/* Reads one request. False (after answering, if there is anyone to answer)
 * when it is unreadable. */
static bool read_request(int fd, request_t *req)
{
    char head[MAX_HEADER + 1];
    size_t have = 0;
    char *end = NULL;
    while (!end) {
        if (have == MAX_HEADER) {
            respond_error(fd, 400, "headers too large");
            return false;
        }
        const ssize_t n = read(fd, head + have, MAX_HEADER - have);
        if (n <= 0) {
            return false;
        }
        have += (size_t)n;
        head[have] = '\0';
        end = strstr(head, "\r\n\r\n");
    }
    *end = '\0';
    char *body_start = end + 4;
    const size_t extra = have - (size_t)(body_start - head);

    if (sscanf(head, "%7s %255s", req->method, req->path) != 2) {
        respond_error(fd, 400, "bad request line");
        return false;
    }
    size_t length = 0;
    for (char *line = strstr(head, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        if (strncasecmp(line + 2, "Content-Length:", 15) == 0) {
            length = strtoul(line + 17, NULL, 10);
        }
    }
    if (length > MAX_BODY) {
        respond_error(fd, 413, "too large for the box");
        return false;
    }
    req->body = malloc(length + 1);
    if (!req->body) {
        respond_error(fd, 503, "out of memory");
        return false;
    }
    const size_t first = extra < length ? extra : length;
    memcpy(req->body, body_start, first);
    req->body_len = first;
    while (req->body_len < length) {
        const ssize_t n = read(fd, req->body + req->body_len, length - req->body_len);
        if (n <= 0) {
            free(req->body);
            return false;
        }
        req->body_len += (size_t)n;
    }
    req->body[req->body_len] = '\0';
    return true;
}

/* --- the routes: the same API as every box ------------------------------ */

static box_t *the_box;

static void snapshot(int fd, bool status)
{
    box_panel_state_t *state = malloc(sizeof(*state));
    if (!state) {
        respond_error(fd, 503, "out of memory");
        return;
    }
    box_scheduler_state(box_scheduler(the_box), state);
    const box_api_info_t info = {
        .width = web.size,
        .height = web.size,
        .shape = "square",
        .grid = box_grid(the_box),
        .fps = FPS,
        .style = box_style_name(box_style(the_box)),
        .brightness = box_brightness(the_box),
    };
    respond_json(fd, 200, status ? box_api_status(state, &info) : box_api_display(&info));
    free(state);
}

/* GET /frame?since=N: the screen as raw RGB once it is newer than version N
 * (204 if not), with its version and brightness in headers. */
static void frame(int fd, const char *query)
{
    const char *since = query ? strstr(query, "since=") : NULL;
    if (since && strtoull(since + 6, NULL, 10) == web.version) {
        respond(fd, 204, "text/plain", NULL, 0, NULL);
        return;
    }
    char headers[160];
    snprintf(headers, sizeof(headers), "X-Frame: %llu\r\nX-Size: %d\r\nX-Brightness: %.2f\r\n",
             (unsigned long long)web.version, web.size, web.brightness);
    respond(fd, 200, "application/octet-stream", web.screen, (size_t)web.size * web.size * 3,
            headers);
}

static void handle(int fd, request_t *req)
{
    char *query = strchr(req->path, '?');
    if (query) {
        *query++ = '\0';
    }
    const char *m = req->method, *p = req->path;
    const double now = now_s();
    if (!strcmp(p, "/") && !strcmp(m, "GET")) {
        respond(fd, 200, "text/html; charset=utf-8", page_html, page_html_len, NULL);
    } else if (!strcmp(p, "/frame") && !strcmp(m, "GET")) {
        frame(fd, query);
    } else if (!strcmp(p, "/api/events") && !strcmp(m, "POST")) {
        box_event_t event;
        box_error_t err;
        box_accepted_t accepted;
        char *body = req->body;
        req->body = NULL; /* parse_take owns it now */
        bool ok = box_event_parse_take(body, req->body_len, &event, &err);
        if (ok) {
            ok = box_scheduler_submit(box_scheduler(the_box), &event, false, now, &accepted, &err);
            box_event_clear(&event);
        }
        if (ok) {
            respond_json(fd, 200, box_api_accepted(&accepted));
        } else {
            respond_error(fd, err.status, err.message);
        }
    } else if (!strcmp(p, "/api/events") && !strcmp(m, "DELETE")) {
        box_scheduler_clear(box_scheduler(the_box), now);
        respond_json(fd, 200, box_api_ok());
    } else if (!strcmp(p, "/api/ambient") && !strcmp(m, "DELETE")) {
        box_scheduler_clear_ambient(box_scheduler(the_box), now);
        respond_json(fd, 200, box_api_ok());
    } else if (!strcmp(p, "/api/status") && !strcmp(m, "GET")) {
        snapshot(fd, true);
    } else if (!strcmp(p, "/api/display") && !strcmp(m, "GET")) {
        snapshot(fd, false);
    } else if (!strcmp(p, "/api/display") && !strcmp(m, "PATCH")) {
        bool has_brightness, has_style;
        float brightness;
        box_style_t style;
        box_error_t err;
        if (!box_api_parse_settings(req->body, req->body_len, &has_brightness, &brightness,
                                    &has_style, &style, &err)) {
            respond_error(fd, err.status, err.message);
            return;
        }
        if (has_brightness) {
            box_request_brightness(the_box, brightness);
        }
        if (has_style) {
            box_set_style(the_box, style);
        }
        snapshot(fd, false);
    } else if (!strncmp(p, "/api/", 5)) {
        respond_error(fd, 404, "no such endpoint");
    } else {
        respond(fd, 404, "text/plain", "not found\n", 10, NULL);
    }
}

/* --- main ---------------------------------------------------------------- */

static volatile sig_atomic_t stopping;
static void on_signal(int sig) { stopping = 1; }

int main(int argc, char **argv)
{
    int port = 8080;
    web.size = 480;
    box_style_t style = BOX_STYLE_SMOOTH;
    for (int i = 1; i < argc; i++) {
        const char *next = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(argv[i], "--port") && next) {
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--size") && next) {
            web.size = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--style") && next && box_style_parse(next, &style)) {
            i++;
        } else {
            fprintf(stderr, "usage: box-web [--port 8080] [--size 480] [--style smooth|led]\n");
            return 2;
        }
    }
    if (port <= 0 || port > 65535 || web.size < 64 || web.size > 2048) {
        fprintf(stderr, "box-web: port 1-65535, size 64-2048\n");
        return 2;
    }

    web.screen = calloc((size_t)web.size * web.size, 3);
    const box_display_t display = {
        .width = web.size,
        .height = web.size,
        .show = web_show,
        .set_brightness = web_set_brightness,
    };
    box_images_set_budget(IMAGE_BUDGET);
    the_box = web.screen ? box_new(&display, 96, (uint32_t)time(NULL)) : NULL;
    if (!the_box) {
        fprintf(stderr, "box-web: out of memory\n");
        return 1;
    }
    box_scheduler_set_log(box_scheduler(the_box), log_line, NULL);
    box_request_brightness(the_box, 1.0f);
    box_set_style(the_box, style);
    box_scene_set_clock(local_time, NULL); /* glances keep this time too */
    box_scene_t *clock = box_scene_new_clock(local_time, NULL, CLOCK_COLOR);
    box_set_idle(the_box, clock, now_s());
    box_scene_release(clock);

    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(listener, 16) < 0) {
        fprintf(stderr, "box-web: port %d: %s\n", port, strerror(errno));
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    fprintf(stderr, "box-web: the screen is at http://localhost:%d, the API at /api/\n", port);

    double next_frame = now_s();
    while (!stopping) {
        const double now = now_s();
        if (now >= next_frame) {
            box_show(the_box, box_render(the_box, now));
            next_frame = now + 1.0 / FPS;
        }
        const int wait_ms = (int)((next_frame - now_s()) * 1000.0) + 1;
        struct pollfd pfd = {.fd = listener, .events = POLLIN};
        if (poll(&pfd, 1, wait_ms > 0 ? wait_ms : 0) <= 0) {
            continue;
        }
        const int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            continue;
        }
        /* A client that stalls mid-request must not freeze the screen. */
        const struct timeval timeout = {.tv_sec = 5};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        request_t req = {0};
        if (read_request(fd, &req)) {
            handle(fd, &req);
            free(req.body);
        }
        close(fd);
    }
    box_free(the_box);
    free(web.screen);
    fprintf(stderr, "box-web: stopped\n");
    return 0;
}
