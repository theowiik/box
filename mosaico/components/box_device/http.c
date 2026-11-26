#include "box_http.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "box/api.h"
#include "box_runtime.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "box_http";

/* An animation at the per-event limit, base64'd, plus the JSON around it. */
#define MAX_BODY (BOX_IMAGE_MAX_BYTES / 3 * 4 + 64 * 1024)
#define RECV_RETRIES 3
#define AUTH_HEADER_MAX 192

static const char *status_line(int status)
{
    switch (status) {
    case 200:
        return "200 OK";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 413:
        return "413 Content Too Large";
    case 422:
        return "422 Unprocessable Content";
    case 503:
        return "503 Service Unavailable";
    default:
        return "500 Internal Server Error";
    }
}

/* Send a JSON body from box/api.h (consumed) with the given status. */
static esp_err_t send_json(httpd_req_t *req, int status, char *body)
{
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_sendstr(req, body);
    box_api_free(body);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *message)
{
    return send_json(req, status, box_api_error(message));
}

static esp_err_t send_ok(httpd_req_t *req)
{
    return send_json(req, 200, box_api_ok());
}

/* Constant-time, so a wrong token cannot be guessed byte by byte. */
static bool same_secret(const char *a, const char *b)
{
    const size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la != lb);
    for (size_t i = 0; i < la; ++i) {
        diff |= (unsigned char)(a[i] ^ b[lb ? i % lb : 0]);
    }
    return diff == 0;
}

/* True when the request may write; otherwise the 401 has been sent. */
static bool authorized(httpd_req_t *req)
{
    const char *token = CONFIG_BOX_TOKEN;
    if (!token[0]) {
        return true;
    }
    char header[AUTH_HEADER_MAX] = "";
    const bool present =
        httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK;
    const char *presented = strchr(header, ' ');
    if (!present || !presented || strncasecmp(header, "Bearer ", 7) != 0) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        send_error(req, 401, "Bearer token required");
        return false;
    }
    if (!same_secret(presented + 1, token)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        send_error(req, 401, "Invalid token");
        return false;
    }
    return true;
}

/* Read the whole body. Returns NULL with the error response already sent. */
static char *read_body(httpd_req_t *req, size_t *out_len)
{
    const size_t len = req->content_len;
    if (len == 0) {
        send_error(req, 400, "empty body; send a JSON event");
        return NULL;
    }
    if (len > MAX_BODY) {
        send_error(req, 413, "body too large");
        return NULL;
    }
    char *body = malloc(len + 1); /* Large bodies land in PSRAM. */
    if (!body) {
        send_error(req, 503, "out of memory for the request body");
        return NULL;
    }
    size_t got = 0;
    int retries = 0;
    while (got < len) {
        const int n = httpd_req_recv(req, body + got, len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && retries++ < RECV_RETRIES) {
            continue;
        }
        if (n <= 0) {
            free(body);
            ESP_LOGW(TAG, "body read failed after %u of %u bytes", (unsigned)got, (unsigned)len);
            return NULL; /* The connection is gone; nothing to answer. */
        }
        got += (size_t)n;
    }
    body[len] = '\0';
    *out_len = len;
    return body;
}

static esp_err_t post_event(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    size_t len;
    char *body = read_body(req, &len);
    if (!body) {
        return ESP_OK;
    }
    box_event_t event;
    box_error_t err;
    bool ok = box_event_parse_take(body, len, &event, &err); /* frees body */
    box_accepted_t accepted;
    if (ok) {
        ok = box_runtime_submit(&event, false, &accepted, &err);
        box_event_clear(&event);
    }
    if (!ok) {
        return send_error(req, err.status, err.message);
    }
    return send_json(req, 200, box_api_accepted(&accepted));
}

static esp_err_t delete_events(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    box_runtime_clear_queue();
    return send_ok(req);
}

static esp_err_t delete_ambient(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    box_runtime_clear_ambient();
    return send_ok(req);
}

/* Status and capabilities both read the same snapshot. */
static esp_err_t send_snapshot(httpd_req_t *req, bool status)
{
    box_panel_state_t *state = malloc(sizeof(*state));
    if (!state) {
        return send_error(req, 503, "out of memory");
    }
    box_api_info_t info;
    box_runtime_state(state, &info);
    char *body = status ? box_api_status(state, &info) : box_api_display(&info);
    free(state);
    return send_json(req, 200, body);
}

static esp_err_t get_status(httpd_req_t *req)
{
    return send_snapshot(req, true);
}

static esp_err_t get_display(httpd_req_t *req)
{
    return send_snapshot(req, false);
}

static esp_err_t patch_display(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    size_t len;
    char *body = read_body(req, &len);
    if (!body) {
        return ESP_OK;
    }
    bool has_brightness, has_style;
    float brightness = 0;
    box_style_t style = BOX_STYLE_SMOOTH;
    box_error_t err;
    const bool valid = box_api_parse_settings(body, len, &has_brightness, &brightness,
                                              &has_style, &style, &err);
    free(body);
    if (!valid) {
        return send_error(req, err.status, err.message);
    }
    if ((has_brightness && box_runtime_set_brightness(brightness) != ESP_OK) ||
        (has_style && box_runtime_set_style(style) != ESP_OK)) {
        return send_error(req, 503, "the box is still starting");
    }
    return get_display(req);
}

static esp_err_t get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req,
        "box\n"
        "POST   /api/events   {\"source\":\"me\",\"content\":{\"type\":\"text\",\"text\":\"hello\"}}\n"
        "GET    /api/status   what is on screen\n"
        "GET    /api/display  what this display can do\n"
        "PATCH  /api/display  {\"brightness\":0.6,\"style\":\"led\"}\n"
        "DELETE /api/events   drop the queue; DELETE /api/ambient: the idle rotation\n");
}

esp_err_t box_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_BOX_HTTP_PORT;
    config.stack_size = 8192;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "start server");

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = get_root},
        {.uri = "/api/events", .method = HTTP_POST, .handler = post_event},
        {.uri = "/api/events", .method = HTTP_DELETE, .handler = delete_events},
        {.uri = "/api/ambient", .method = HTTP_DELETE, .handler = delete_ambient},
        {.uri = "/api/status", .method = HTTP_GET, .handler = get_status},
        {.uri = "/api/display", .method = HTTP_GET, .handler = get_display},
        {.uri = "/api/display", .method = HTTP_PATCH, .handler = patch_display},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG, "route %s",
                            routes[i].uri);
    }
    ESP_LOGI(TAG, "serving on port %d%s", CONFIG_BOX_HTTP_PORT,
             CONFIG_BOX_TOKEN[0] ? " (token required for writes)" : "");
    return ESP_OK;
}
