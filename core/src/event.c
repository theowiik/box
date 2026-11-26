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
        return fail(err, 422, "content.%s: a hex colour such as #FFAA00", field);
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


/* An optional string field, at most `max_chars` characters. */
static bool optional_string(const cJSON *obj, const char *field, size_t max_chars,
                            const char **out, box_error_t *err)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    *out = "";
    if (!item || cJSON_IsNull(item)) {
        return true;
    }
    if (!cJSON_IsString(item)) {
        return fail(err, 422, "content.%s: a string", field);
    }
    if (box_utf8_count(item->valuestring, strlen(item->valuestring)) > max_chars) {
        return fail(err, 422, "content.%s: at most %u characters", field, (unsigned)max_chars);
    }
    *out = item->valuestring;
    return true;
}

/* An optional enumerated string; `names` is NULL-terminated, the index is
 * stored in *out (left alone when the field is absent). */
static bool optional_choice(const cJSON *obj, const char *field, const char *const *names,
                            int *out, box_error_t *err)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (!item || cJSON_IsNull(item)) {
        return true;
    }
    for (int i = 0; cJSON_IsString(item) && names[i]; ++i) {
        if (strcmp(item->valuestring, names[i]) == 0) {
            *out = i;
            return true;
        }
    }
    char list[96] = "";
    for (int i = 0; names[i]; ++i) {
        strncat(list, i ? ", " : "", sizeof(list) - strlen(list) - 1);
        strncat(list, names[i], sizeof(list) - strlen(list) - 1);
    }
    return fail(err, 422, "content.%s: one of %s", field, list);
}

static box_scene_t *out_of_memory(box_error_t *err)
{
    fail(err, 503, "out of memory");
    return NULL;
}

static box_scene_t *parse_text(const cJSON *c, box_rgb_t fallback, box_rgb_t *color,
                               box_error_t *err)
{
    static const char *const ALIGN[] = {"center", "left", NULL};
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(c, "text");
    if (!cJSON_IsString(text)) {
        fail(err, 422, "content.text: required string");
        return NULL;
    }
    const char *body, *title;
    if (!optional_string(c, "text", BOX_TEXT_MAX_CHARS, &body, err) ||
        !optional_string(c, "title", BOX_TITLE_MAX_CHARS, &title, err)) {
        return NULL;
    }
    int align = 0;
    bool has_color;
    if (!optional_choice(c, "align", ALIGN, &align, err) ||
        !optional_color(c, "color", color, &has_color, err)) {
        return NULL;
    }
    if (!has_color) {
        *color = fallback;
    }
    box_scene_t *scene = box_scene_new_text(title, strlen(title), body, strlen(body), *color,
                                            align ? BOX_ALIGN_LEFT : BOX_ALIGN_CENTER);
    return scene ? scene : out_of_memory(err);
}

static box_scene_t *parse_color(const cJSON *c, box_rgb_t *color, box_error_t *err)
{
    bool has;
    if (!optional_color(c, "color", color, &has, err)) {
        return NULL;
    }
    if (!has) {
        fail(err, 422, "content.color: required for color content");
        return NULL;
    }
    box_scene_t *scene = box_scene_new_color(*color);
    return scene ? scene : out_of_memory(err);
}

static box_scene_t *parse_image(const cJSON *c, box_error_t *err)
{
    static const char *const SCALING[] = {"pixelated", "smooth", NULL};
    int width, height;
    if (!whole_number(cJSON_GetObjectItemCaseSensitive(c, "width"), 1, BOX_IMAGE_MAX_SIDE,
                      &width) ||
        !whole_number(cJSON_GetObjectItemCaseSensitive(c, "height"), 1, BOX_IMAGE_MAX_SIDE,
                      &height)) {
        fail(err, 422, "content.width and content.height: whole numbers from 1 to %d",
             BOX_IMAGE_MAX_SIDE);
        return NULL;
    }
    double fps = 12.0;
    const cJSON *fps_item = cJSON_GetObjectItemCaseSensitive(c, "fps");
    if (fps_item && !cJSON_IsNull(fps_item)) {
        if (!cJSON_IsNumber(fps_item) || fps_item->valuedouble < 0.1 ||
            fps_item->valuedouble > 60.0) {
            fail(err, 422, "content.fps: a number from 0.1 to 60");
            return NULL;
        }
        fps = fps_item->valuedouble;
    }
    bool loop = true;
    const cJSON *loop_item = cJSON_GetObjectItemCaseSensitive(c, "loop");
    if (loop_item && !cJSON_IsNull(loop_item)) {
        if (!cJSON_IsBool(loop_item)) {
            fail(err, 422, "content.loop: true or false");
            return NULL;
        }
        loop = cJSON_IsTrue(loop_item);
    }
    int scaling = BOX_SCALING_PIXELATED;
    if (!optional_choice(c, "scaling", SCALING, &scaling, err)) {
        return NULL;
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(c, "data");
    if (!cJSON_IsString(data)) {
        fail(err, 422, "content.data: required base64 string of RGB frames");
        return NULL;
    }
    const size_t encoded = strlen(data->valuestring);
    /* Size first, from the encoded length, before anything is allocated. */
    if (encoded / 4 * 3 > BOX_IMAGE_MAX_BYTES + 2) {
        fail(err, 422, "content.data: exceeds %u bytes", (unsigned)BOX_IMAGE_MAX_BYTES);
        return NULL;
    }
    const size_t size = box_base64_decoded_size(data->valuestring, encoded);
    if (encoded == 0 || size == 0) {
        fail(err, 422, "content.data: must be non-empty valid base64");
        return NULL;
    }
    const size_t stride = (size_t)width * (size_t)height * 3;
    if (size % stride) {
        fail(err, 422,
             "content.data: is %u bytes, not a whole number of %dx%d RGB frames (%u bytes each)",
             (unsigned)size, width, height, (unsigned)stride);
        return NULL;
    }
    if (!box_images_budget_allows(size)) {
        fail(err, 503, "image memory is full; let queued images play out first");
        return NULL;
    }
    uint8_t *pixels = malloc(size);
    if (!pixels) {
        fail(err, 503, "out of memory for a %u byte image", (unsigned)size);
        return NULL;
    }
    if (!box_base64_decode(data->valuestring, encoded, pixels)) {
        free(pixels);
        fail(err, 422, "content.data: must be valid base64");
        return NULL;
    }
    box_scene_t *scene = box_scene_new_image(pixels, size, width, height, (float)fps, loop,
                                             (box_scaling_t)scaling);
    return scene ? scene : out_of_memory(err);
}

static box_scene_t *parse_content(const cJSON *c, box_rgb_t fallback, box_rgb_t *color,
                                  box_error_t *err)
{
    if (!cJSON_IsObject(c)) {
        fail(err, 422, "content: required object with a \"type\"");
        return NULL;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(c, "type");
    const char *name = cJSON_IsString(type) ? type->valuestring : "";
    *color = fallback;
    if (strcmp(name, "text") == 0) {
        return parse_text(c, fallback, color, err);
    }
    if (strcmp(name, "image") == 0) {
        return parse_image(c, err);
    }
    if (strcmp(name, "color") == 0) {
        return parse_color(c, color, err);
    }
    if (strcmp(name, "clear") == 0) {
        box_scene_t *scene = box_scene_new_clear();
        return scene ? scene : out_of_memory(err);
    }
    fail(err, 422, "content.type: one of text, image, color, clear");
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

    box_style_t style = BOX_STYLE_DEFAULT;
    const cJSON *style_item = cJSON_GetObjectItemCaseSensitive(root, "style");
    if (style_item && !cJSON_IsNull(style_item)) {
        if (!cJSON_IsString(style_item) || !box_style_parse(style_item->valuestring, &style)) {
            fail(err, 422, "style: smooth or led");
            goto done;
        }
    }

    const box_rgb_t fallback = box_source_color(out->source, out->priority);
    out->scene = parse_content(cJSON_GetObjectItemCaseSensitive(root, "content"), fallback,
                               &out->color, err);
    if (out->scene) {
        box_scene_set_style(out->scene, style);
        ok = true;
    }

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

bool box_event_text(box_event_t *out, const char *source, const char *title, const char *text,
                    box_priority_t priority, double duration)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->source, sizeof(out->source), "%s", source);
    out->priority = priority;
    out->duration = duration > 0 ? duration : box_priority_default_duration(priority);
    out->color = box_source_color(source, priority);
    title = title ? title : "";
    out->scene = box_scene_new_text(title, strlen(title), text, strlen(text), out->color,
                                    BOX_ALIGN_CENTER);
    return out->scene != NULL;
}
