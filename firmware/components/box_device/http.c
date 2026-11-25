#include "box_http.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "box_runtime.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "box_http";

/* An animation at the per-event limit, base64'd, plus the JSON around it. */
#define MAX_BODY (BOX_FRAMES_MAX_BYTES / 3 * 4 + 64 * 1024)
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

/* Send a cJSON document (consumed) with the given status. */
static esp_err_t send_json(httpd_req_t *req, int status, cJSON *doc)
{
    char *body = doc ? cJSON_PrintUnformatted(doc) : NULL;
    cJSON_Delete(doc);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *message)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "detail", message);
    return send_json(req, status, doc);
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "status", "ok");
    return send_json(req, 200, doc);
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
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "id", accepted.id);
    cJSON_AddStringToObject(doc, "source", accepted.source);
    cJSON_AddStringToObject(doc, "priority", box_priority_name(accepted.priority));
    cJSON_AddNumberToObject(doc, "duration", accepted.duration);
    cJSON_AddNumberToObject(doc, "queued", (double)accepted.queued);
    return send_json(req, 200, doc);
}

static esp_err_t delete_events(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    box_runtime_clear_queue();
    return send_ok(req);
}

static esp_err_t get_display(httpd_req_t *req)
{
    box_panel_state_t *state = malloc(sizeof(*state));
    if (!state) {
        return send_error(req, 503, "out of memory");
    }
    box_runtime_state(state);
    box_runtime_info_t info;
    box_runtime_info(&info);
    char color[8];
    box_rgb_format(state->color, color);

    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "scene", state->scene);
    if (state->source[0]) {
        cJSON_AddStringToObject(doc, "source", state->source);
    } else {
        cJSON_AddNullToObject(doc, "source");
    }
    if (state->priority) {
        cJSON_AddStringToObject(doc, "priority", state->priority);
    } else {
        cJSON_AddNullToObject(doc, "priority");
    }
    cJSON_AddStringToObject(doc, "text", state->text);
    cJSON_AddStringToObject(doc, "color", color);
    /* Two decimals: the float would otherwise print as 0.800000011920929. */
    cJSON_AddNumberToObject(doc, "brightness", round(info.brightness * 100.0) / 100.0);
    cJSON_AddNumberToObject(doc, "width", info.width);
    cJSON_AddNumberToObject(doc, "height", info.height);
    cJSON_AddNumberToObject(doc, "fps", info.fps);
    /* Senders size animations to fit: larger frames than this are refused. */
    cJSON_AddNumberToObject(doc, "max_frames_bytes", BOX_FRAMES_MAX_BYTES);
    cJSON_AddNumberToObject(doc, "drawn_fps", round(info.drawn_fps * 10.0) / 10.0);
    cJSON_AddNumberToObject(doc, "shown_fps", round(info.shown_fps * 10.0) / 10.0);
    cJSON_AddNumberToObject(doc, "queued", (double)state->queued);
    cJSON *ambient = cJSON_AddArrayToObject(doc, "ambient");
    for (size_t i = 0; i < state->ambient_count; ++i) {
        cJSON_AddItemToArray(ambient, cJSON_CreateString(state->ambient[i]));
    }
    free(state);
    return send_json(req, 200, doc);
}

static esp_err_t post_clear(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    box_runtime_clear_all();
    return send_ok(req);
}

static esp_err_t post_brightness(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    size_t len;
    char *body = read_body(req, &len);
    if (!body) {
        return ESP_OK;
    }
    cJSON *doc = cJSON_ParseWithLength(body, len);
    free(body);
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(doc, "brightness");
    const bool valid = cJSON_IsNumber(value) && value->valuedouble >= 0.0 &&
                       value->valuedouble <= 1.0;
    const float brightness = valid ? (float)value->valuedouble : 0.0f;
    cJSON_Delete(doc);
    if (!valid) {
        return send_error(req, 422, "brightness: a number from 0.0 to 1.0");
    }
    if (box_runtime_set_brightness(brightness) != ESP_OK) {
        return send_error(req, 503, "the panel refused the brightness change");
    }
    return send_ok(req);
}

static esp_err_t get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req,
        "box: POST /api/events with "
        "{\"source\":\"me\",\"scene\":{\"kind\":\"text\",\"text\":\"hello\"}}\n"
        "GET /api/display for the panel's size and state.\n");
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
        {.uri = "/api/display", .method = HTTP_GET, .handler = get_display},
        {.uri = "/api/display/clear", .method = HTTP_POST, .handler = post_clear},
        {.uri = "/api/display/brightness", .method = HTTP_POST, .handler = post_brightness},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG, "route %s",
                            routes[i].uri);
    }
    ESP_LOGI(TAG, "serving on port %d%s", CONFIG_BOX_HTTP_PORT,
             CONFIG_BOX_TOKEN[0] ? " (token required for writes)" : "");
    return ESP_OK;
}
