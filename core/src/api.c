#include "box/api.h"

#include <math.h>
#include <stdio.h>

#include "cJSON.h"

/* Print and consume a document. */
static char *print(cJSON *doc)
{
    char *json = doc ? cJSON_PrintUnformatted(doc) : NULL;
    cJSON_Delete(doc);
    return json;
}

/* A float would otherwise print as 0.800000011920929. */
static double tidy(float value, double places)
{
    return round(value * places) / places;
}

static void add_or_null(cJSON *doc, const char *key, const char *value)
{
    if (value && value[0]) {
        cJSON_AddStringToObject(doc, key, value);
    } else {
        cJSON_AddNullToObject(doc, key);
    }
}

char *box_api_status(const box_panel_state_t *state, const box_api_info_t *info)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON *showing = cJSON_AddObjectToObject(doc, "showing");
    if (!doc || !showing) {
        cJSON_Delete(doc);
        return NULL;
    }
    char color[8];
    box_rgb_format(state->color, color);
    cJSON_AddStringToObject(showing, "type", state->content);
    add_or_null(showing, "source", state->source);
    add_or_null(showing, "priority", state->priority);
    add_or_null(showing, "title", state->title);
    add_or_null(showing, "text", state->text);
    cJSON_AddStringToObject(showing, "color", color);
    cJSON_AddStringToObject(showing, "style", state->style);
    cJSON_AddNumberToObject(doc, "queued", (double)state->queued);
    cJSON *ambient = cJSON_AddArrayToObject(doc, "ambient");
    for (size_t i = 0; ambient && i < state->ambient_count; ++i) {
        cJSON_AddItemToArray(ambient, cJSON_CreateString(state->ambient[i]));
    }
    cJSON_AddStringToObject(doc, "style", info->style);
    cJSON_AddNumberToObject(doc, "brightness", tidy(info->brightness, 100.0));
    cJSON *fps = cJSON_AddObjectToObject(doc, "fps");
    if (fps) {
        cJSON_AddNumberToObject(fps, "drawn", tidy(info->drawn_fps, 10.0));
        cJSON_AddNumberToObject(fps, "shown", tidy(info->shown_fps, 10.0));
    }
    return print(doc);
}

char *box_api_display(const box_api_info_t *info)
{
    static const char *const STYLES[] = {"smooth", "led"};
    static const char *const CONTENT[] = {"text", "image", "color", "clear", "glance"};
    cJSON *doc = cJSON_CreateObject();
    if (!doc) {
        return NULL;
    }
    cJSON_AddNumberToObject(doc, "width", info->width);
    cJSON_AddNumberToObject(doc, "height", info->height);
    cJSON_AddStringToObject(doc, "shape", info->shape);
    cJSON_AddItemToObject(doc, "content", cJSON_CreateStringArray(CONTENT, 5));
    cJSON_AddItemToObject(doc, "styles", cJSON_CreateStringArray(STYLES, 2));
    cJSON_AddStringToObject(doc, "style", info->style);
    cJSON_AddNumberToObject(doc, "led_grid", info->grid);
    cJSON_AddNumberToObject(doc, "brightness", tidy(info->brightness, 100.0));
    cJSON_AddNumberToObject(doc, "fps", info->fps);
    cJSON *limits = cJSON_AddObjectToObject(doc, "limits");
    if (limits) {
        cJSON_AddNumberToObject(limits, "text_chars", BOX_TEXT_MAX_CHARS);
        cJSON_AddNumberToObject(limits, "title_chars", BOX_TITLE_MAX_CHARS);
        cJSON_AddNumberToObject(limits, "image_bytes", BOX_IMAGE_MAX_BYTES);
        cJSON_AddNumberToObject(limits, "image_side", BOX_IMAGE_MAX_SIDE);
        cJSON_AddNumberToObject(limits, "duration", BOX_DURATION_MAX);
    }
    return print(doc);
}

char *box_api_accepted(const box_accepted_t *accepted)
{
    cJSON *doc = cJSON_CreateObject();
    if (!doc) {
        return NULL;
    }
    cJSON_AddStringToObject(doc, "id", accepted->id);
    cJSON_AddStringToObject(doc, "source", accepted->source);
    cJSON_AddStringToObject(doc, "priority", box_priority_name(accepted->priority));
    cJSON_AddNumberToObject(doc, "duration", accepted->duration);
    cJSON_AddNumberToObject(doc, "queued", (double)accepted->queued);
    return print(doc);
}

char *box_api_error(const char *message)
{
    cJSON *doc = cJSON_CreateObject();
    if (doc) {
        cJSON_AddStringToObject(doc, "detail", message);
    }
    return print(doc);
}

char *box_api_ok(void)
{
    cJSON *doc = cJSON_CreateObject();
    if (doc) {
        cJSON_AddStringToObject(doc, "status", "ok");
    }
    return print(doc);
}

void box_api_free(char *json)
{
    cJSON_free(json);
}

static bool refuse(box_error_t *err, const char *message)
{
    err->status = 422;
    snprintf(err->message, sizeof(err->message), "%s", message);
    return false;
}

bool box_api_parse_settings(const char *json, size_t len, bool *has_brightness,
                            float *brightness, bool *has_style, box_style_t *style,
                            box_error_t *err)
{
    *has_brightness = *has_style = false;
    cJSON *doc = cJSON_ParseWithLength(json, len);
    bool ok = cJSON_IsObject(doc) || refuse(err, "body: a JSON object");
    const cJSON *b = ok ? cJSON_GetObjectItemCaseSensitive(doc, "brightness") : NULL;
    if (ok && b) {
        ok = (cJSON_IsNumber(b) && b->valuedouble >= 0.0 && b->valuedouble <= 1.0) ||
             refuse(err, "brightness: a number from 0.0 to 1.0");
        *brightness = ok ? (float)b->valuedouble : 0.0f;
        *has_brightness = ok;
    }
    const cJSON *st = ok ? cJSON_GetObjectItemCaseSensitive(doc, "style") : NULL;
    if (ok && st) {
        ok = (cJSON_IsString(st) && box_style_parse(st->valuestring, style)) ||
             refuse(err, "style: smooth or led");
        *has_style = ok;
    }
    if (ok && !*has_brightness && !*has_style) {
        ok = refuse(err, "nothing to change: send brightness and/or style");
    }
    cJSON_Delete(doc);
    return ok;
}
