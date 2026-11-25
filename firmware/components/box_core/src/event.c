#include "box/event.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"
#include "box/text.h"
#include "cJSON.h"

/* Per-source colours. A connector that sends no colour gets its brand here. */
static const struct {
    const char *source;
    const char *color;
} SOURCE_COLORS[] = {
    {"discord", "#5865F2"},
    {"slack", "#36C5F0"},
    {"github", "#F0F6FC"},
    {"email", "#E8B923"},
    {"calendar", "#4FC3F7"},
    {"build", "#FF5252"},
    {"weather", "#78909C"},
    {"essr", "#F2A900"}, /* En Stor Stark Review: lager amber. */
};

/* Fallback colour by priority when the source is unknown. */
static const char *const PRIORITY_COLORS[] = {
    [BOX_PRIORITY_AMBIENT] = "#7A8A99",
    [BOX_PRIORITY_NORMAL] = "#00E676",
    [BOX_PRIORITY_ALERT] = "#FF4F00",
    [BOX_PRIORITY_ALARM] = "#FF1744",
};

static const char *const PRIORITY_NAMES[] = {
    [BOX_PRIORITY_AMBIENT] = "ambient",
    [BOX_PRIORITY_NORMAL] = "normal",
    [BOX_PRIORITY_ALERT] = "alert",
    [BOX_PRIORITY_ALARM] = "alarm",
};

/* How long an event holds the panel when it does not say. An alarm is worth
 * interrupting for, so it is worth reading twice. */
static const double DEFAULT_DURATION[] = {
    [BOX_PRIORITY_AMBIENT] = 12.0,
    [BOX_PRIORITY_NORMAL] = 8.0,
    [BOX_PRIORITY_ALERT] = 10.0,
    [BOX_PRIORITY_ALARM] = 20.0,
};

#define DEFAULT_SOURCE "api"
#define DEFAULT_FPS 12.0
#define MIN_FPS 0.1
#define MAX_FPS 60.0

const char *box_priority_name(box_priority_t priority)
{
    return PRIORITY_NAMES[priority];
}

bool box_priority_parse(const char *name, box_priority_t *out)
{
    for (int p = BOX_PRIORITY_AMBIENT; p <= BOX_PRIORITY_ALARM; ++p) {
        if (strcmp(name, PRIORITY_NAMES[p]) == 0) {
            *out = (box_priority_t)p;
            return true;
        }
    }
    return false;
}

double box_priority_default_duration(box_priority_t priority)
{
    return DEFAULT_DURATION[priority];
}

box_rgb_t box_source_color(const char *source, box_priority_t priority)
{
    box_rgb_t color = {0, 0, 0};
    for (size_t i = 0; i < sizeof(SOURCE_COLORS) / sizeof(SOURCE_COLORS[0]); ++i) {
        if (strcmp(source, SOURCE_COLORS[i].source) == 0) {
            box_rgb_parse(SOURCE_COLORS[i].color, &color);
            return color;
        }
    }
    box_rgb_parse(PRIORITY_COLORS[priority], &color);
    return color;
}

void box_event_clear(box_event_t *event)
{
    box_scene_release(event->scene);
    memset(event, 0, sizeof(*event));
}

static bool fail(box_error_t *err, int status, const char *fmt, ...)
{
    err->status = status;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);
    return false;
}

static bool valid_source(const char *s)
{
    const size_t len = strlen(s);
    if (len == 0 || len > BOX_SOURCE_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* An optional colour field: absent or null leaves *has unset. */
static bool optional_color(const cJSON *obj, const char *field, box_rgb_t *out, bool *has,
                           box_error_t *err)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    *has = false;
    if (!item || cJSON_IsNull(item)) {
        return true;
    }
    if (!cJSON_IsString(item) || !box_rgb_parse(item->valuestring, out)) {
        return fail(err, 422, "scene.%s: invalid hex color, expected #RRGGBB", field);
    }
    *has = true;
    return true;
}

static bool whole_number(const cJSON *item, double lo, double hi, int *out)
{
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double v = item->valuedouble;
    if (v != floor(v) || v < lo || v > hi) {
        return false;
    }
    *out = (int)v;
    return true;
}

static box_scene_t *parse_text(const cJSON *spec, box_rgb_t fallback, box_rgb_t *color,
                               box_error_t *err)
{
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(spec, "text");
    if (!cJSON_IsString(text)) {
        fail(err, 422, "scene.text: required string");
        return NULL;
    }
    const size_t len = strlen(text->valuestring);
    if (box_utf8_count(text->valuestring, len) > BOX_TEXT_MAX_CHARS) {
        fail(err, 422, "scene.text: at most %d characters", BOX_TEXT_MAX_CHARS);
        return NULL;
    }
    bool has_color;
    if (!optional_color(spec, "color", color, &has_color, err)) {
        return NULL;
    }
    if (!has_color) {
        *color = fallback;
    }
    box_scene_t *scene = box_scene_new_text(text->valuestring, len, *color);
    if (!scene) {
        fail(err, 503, "out of memory");
    }
    return scene;
}

static box_scene_t *parse_solid(const cJSON *spec, box_rgb_t *color, box_error_t *err)
{
    bool has_color;
    if (!optional_color(spec, "color", color, &has_color, err)) {
        return NULL;
    }
    if (!has_color) {
        fail(err, 422, "scene.color: required for a solid scene");
        return NULL;
    }
    box_scene_t *scene = box_scene_new_solid(*color);
    if (!scene) {
        fail(err, 503, "out of memory");
    }
    return scene;
}

static box_scene_t *parse_frames(const cJSON *spec, box_error_t *err)
{
    int width, height;
    if (!whole_number(cJSON_GetObjectItemCaseSensitive(spec, "width"), 1, BOX_FRAMES_MAX_SIDE,
                      &width) ||
        !whole_number(cJSON_GetObjectItemCaseSensitive(spec, "height"), 1, BOX_FRAMES_MAX_SIDE,
                      &height)) {
        fail(err, 422, "scene.width and scene.height: whole numbers from 1 to %d",
             BOX_FRAMES_MAX_SIDE);
        return NULL;
    }
    double fps = DEFAULT_FPS;
    const cJSON *fps_item = cJSON_GetObjectItemCaseSensitive(spec, "fps");
    if (fps_item && !cJSON_IsNull(fps_item)) {
        if (!cJSON_IsNumber(fps_item) || fps_item->valuedouble < MIN_FPS ||
            fps_item->valuedouble > MAX_FPS) {
            fail(err, 422, "scene.fps: a number from %.1f to %.0f", MIN_FPS, MAX_FPS);
            return NULL;
        }
        fps = fps_item->valuedouble;
    }
    bool loop = true;
    const cJSON *loop_item = cJSON_GetObjectItemCaseSensitive(spec, "loop");
    if (loop_item && !cJSON_IsNull(loop_item)) {
        if (!cJSON_IsBool(loop_item)) {
            fail(err, 422, "scene.loop: true or false");
            return NULL;
        }
        loop = cJSON_IsTrue(loop_item);
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(spec, "data");
    if (!cJSON_IsString(data)) {
        fail(err, 422, "scene.data: required base64 string");
        return NULL;
    }
    const size_t encoded = strlen(data->valuestring);
    /* Size first, from the encoded length, before anything is allocated. */
    if (encoded / 4 * 3 > BOX_FRAMES_MAX_BYTES + 2) {
        fail(err, 422, "scene.data: exceeds %u bytes", (unsigned)BOX_FRAMES_MAX_BYTES);
        return NULL;
    }
    if (encoded == 0) {
        fail(err, 422, "scene.data: is empty");
        return NULL;
    }
    const size_t size = box_base64_decoded_size(data->valuestring, encoded);
    if (size == 0) {
        fail(err, 422, "scene.data: must be valid base64");
        return NULL;
    }
    if (size > BOX_FRAMES_MAX_BYTES) {
        fail(err, 422, "scene.data: exceeds %u bytes", (unsigned)BOX_FRAMES_MAX_BYTES);
        return NULL;
    }
    /* Checked at the door, so a truncated upload is a 422 instead of a crash
     * mid-loop. */
    const size_t stride = (size_t)width * (size_t)height * 3;
    if (size % stride) {
        fail(err, 422,
             "scene.data: is %u bytes, not a whole number of %dx%d RGB frames (%u bytes each)",
             (unsigned)size, width, height, (unsigned)stride);
        return NULL;
    }
    if (!box_frames_budget_allows(size)) {
        fail(err, 503, "animation memory is full; let queued animations play out first");
        return NULL;
    }
    uint8_t *pixels = malloc(size);
    if (!pixels) {
        fail(err, 503, "out of memory for a %u byte animation", (unsigned)size);
        return NULL;
    }
    if (!box_base64_decode(data->valuestring, encoded, pixels)) {
        free(pixels);
        fail(err, 422, "scene.data: must be valid base64");
        return NULL;
    }
    box_scene_t *scene = box_scene_new_frames(pixels, size, width, height, (float)fps, loop);
    if (!scene) {
        fail(err, 503, "out of memory");
    }
    return scene;
}

static box_scene_t *parse_scene(const cJSON *spec, box_rgb_t fallback, box_rgb_t *color,
                                box_error_t *err)
{
    if (!cJSON_IsObject(spec)) {
        fail(err, 422, "scene: required object with a \"kind\"");
        return NULL;
    }
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(spec, "kind");
    const char *name = cJSON_IsString(kind) ? kind->valuestring : "";
    *color = fallback;
    if (strcmp(name, "text") == 0) {
        return parse_text(spec, fallback, color, err);
    }
    if (strcmp(name, "solid") == 0) {
        return parse_solid(spec, color, err);
    }
    if (strcmp(name, "frames") == 0) {
        return parse_frames(spec, err);
    }
    if (strcmp(name, "blank") == 0) {
        box_scene_t *scene = box_scene_new_blank();
        if (!scene) {
            fail(err, 503, "out of memory");
        }
        return scene;
    }
    if (strcmp(name, "visual") == 0) {
        fail(err, 422,
             "scene.kind: visuals are rendered off the box now; send them as frames "
             "(connectors/sources: `box-sources visual NAME`)");
        return NULL;
    }
    fail(err, 422, "scene.kind: one of text, solid, blank, frames");
    return NULL;
}

/* Validate a parsed document into `out`. Consumes `root`. */
static bool from_root(cJSON *root, box_event_t *out, box_error_t *err)
{
    memset(out, 0, sizeof(*out));
    memset(err, 0, sizeof(*err));
    if (!root) {
        return fail(err, 400, "body is not valid JSON");
    }
    bool ok = false;
    if (!cJSON_IsObject(root)) {
        fail(err, 422, "body: expected a JSON object");
        goto done;
    }

    const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
    if (source && !cJSON_IsNull(source)) {
        if (!cJSON_IsString(source) || !valid_source(source->valuestring)) {
            fail(err, 422, "source: 1-%d characters of letters, digits, '.', '_' or '-'",
                 BOX_SOURCE_MAX);
            goto done;
        }
        strcpy(out->source, source->valuestring);
    } else {
        strcpy(out->source, DEFAULT_SOURCE);
    }

    out->priority = BOX_PRIORITY_NORMAL;
    const cJSON *priority = cJSON_GetObjectItemCaseSensitive(root, "priority");
    if (priority && !cJSON_IsNull(priority)) {
        if (!cJSON_IsString(priority) || !box_priority_parse(priority->valuestring, &out->priority)) {
            fail(err, 422, "priority: one of ambient, normal, alert, alarm");
            goto done;
        }
    }

    out->duration = box_priority_default_duration(out->priority);
    const cJSON *duration = cJSON_GetObjectItemCaseSensitive(root, "duration");
    if (duration && !cJSON_IsNull(duration)) {
        if (!cJSON_IsNumber(duration) || !(duration->valuedouble > 0.0) ||
            duration->valuedouble > BOX_DURATION_MAX) {
            fail(err, 422, "duration: seconds, above 0 and at most %.0f", BOX_DURATION_MAX);
            goto done;
        }
        out->duration = duration->valuedouble;
    }

    const box_rgb_t fallback = box_source_color(out->source, out->priority);
    out->scene = parse_scene(cJSON_GetObjectItemCaseSensitive(root, "scene"), fallback,
                             &out->color, err);
    ok = out->scene != NULL;

done:
    cJSON_Delete(root);
    if (!ok) {
        box_event_clear(out);
    }
    return ok;
}

bool box_event_parse(const char *json, size_t len, box_event_t *out, box_error_t *err)
{
    return from_root(cJSON_ParseWithLength(json, len), out, err);
}

bool box_event_parse_take(char *json, size_t len, box_event_t *out, box_error_t *err)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    free(json);
    return from_root(root, out, err);
}

bool box_event_text(box_event_t *out, const char *source, const char *text,
                    box_priority_t priority, double duration)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->source, sizeof(out->source), "%s", source);
    out->priority = priority;
    out->duration = duration > 0 ? duration : box_priority_default_duration(priority);
    out->color = box_source_color(source, priority);
    out->scene = box_scene_new_text(text, strlen(text), out->color);
    return out->scene != NULL;
}
