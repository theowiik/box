#include "box_runtime.h"

#include <stdlib.h>
#include <time.h>

#include "box/box.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mosaico_display.h"
#include "sdkconfig.h"

static const char *TAG = "box";

#define RENDER_STACK 12288
#define RENDER_PRIORITY 5
/* The box's own cards and announcements use this source. */
#define BOX_SOURCE "box"
#define STATUS_DWELL 12.0
/* The window the measured frame rate is averaged over, in microseconds. */
#define STATS_PERIOD_US (5 * 1000 * 1000)
/* Before network time arrives the clock starts near 1970: treat anything
 * earlier than this as unknown rather than showing a wrong time. */
#define TIME_KNOWN_AFTER 1700000000
/* The idle clock, in warm LED orange. */
static const box_rgb_t CLOCK_COLOR = {0xFF, 0x8A, 0x3D};

/* Ownership: the box (core) and the display belong to the runtime for the
 * life of the firmware. `lock` serializes everything but box_show(), which
 * only the render task calls (see box/box.h). */
static struct {
    SemaphoreHandle_t lock;
    box_display_t display;
    box_t *box;
    float drawn_fps;
    float shown_fps;
    bool started;
} s;

static double now_s(void)
{
    return (double)esp_timer_get_time() / 1e6;
}

static void log_line(void *ctx, const char *line)
{
    (void)ctx;
    ESP_LOGI(TAG, "%s", line);
}

static bool local_time(void *ctx, int *hour, int *minute)
{
    (void)ctx;
    const time_t now = time(NULL);
    if (now < TIME_KNOWN_AFTER) {
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    *hour = tm.tm_hour;
    *minute = tm.tm_min;
    return true;
}

static void render_task(void *arg)
{
    (void)arg;
    TickType_t period = pdMS_TO_TICKS(1000 / CONFIG_BOX_FPS);
    period = period > 0 ? period : 1;
    TickType_t wake = xTaskGetTickCount();
    uint32_t drawn = 0;
    int64_t render_us = 0, show_us = 0;
    uint64_t shown_from = mosaico_display_frames_shown();
    int64_t stats_from = esp_timer_get_time();
    for (;;) {
        const int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(s.lock, portMAX_DELAY);
        const box_frame_t *frame = box_render(s.box, now_s());
        xSemaphoreGive(s.lock);
        const int64_t t1 = esp_timer_get_time();
        /* Outside the lock: pushing pixels must not hold up incoming events. */
        const bool pushed = box_show(s.box, frame);
        drawn += pushed;
        if (pushed) {
            render_us += t1 - t0;
            show_us += esp_timer_get_time() - t1;
        }

        const int64_t now_us = esp_timer_get_time();
        if (now_us - stats_from >= STATS_PERIOD_US) {
            const uint64_t shown = mosaico_display_frames_shown();
            const double seconds = (double)(now_us - stats_from) / 1e6;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            s.drawn_fps = (float)(drawn / seconds);
            s.shown_fps = (float)((double)(shown - shown_from) / seconds);
            xSemaphoreGive(s.lock);
            if (drawn > 10) { /* where a busy second goes */
                ESP_LOGI(TAG, "%.1f fps: render %lld us, show %lld us a frame", drawn / seconds,
                         (long long)(render_us / drawn), (long long)(show_us / drawn));
            }
            render_us = show_us = 0;
            drawn = 0;
            shown_from = shown;
            stats_from = now_us;
        }
        vTaskDelayUntil(&wake, period);
    }
}

esp_err_t box_runtime_start(float brightness)
{
    ESP_RETURN_ON_FALSE(!s.started, ESP_ERR_INVALID_STATE, TAG, "already started");
    s.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_NO_MEM, TAG, "lock");
    ESP_RETURN_ON_ERROR(mosaico_display_start(brightness, &s.display), TAG, "display");
    s.box = box_new(&s.display, CONFIG_BOX_LED_GRID, esp_random());
    ESP_RETURN_ON_FALSE(s.box, ESP_ERR_NO_MEM, TAG, "box");
    box_scheduler_set_log(box_scheduler(s.box), log_line, NULL);
    box_request_brightness(s.box, brightness);
    box_style_t style = BOX_STYLE_SMOOTH;
    box_style_parse(CONFIG_BOX_STYLE, &style);
    box_set_style(s.box, style);

    /* When nothing is queued and no ambient card is up, show the time. */
    setenv("TZ", CONFIG_BOX_TIMEZONE, 1);
    tzset();
    box_scene_set_clock(local_time, NULL); /* glances keep this time too */
    box_scene_t *clock = box_scene_new_clock(local_time, NULL, CLOCK_COLOR);
    ESP_RETURN_ON_FALSE(clock, ESP_ERR_NO_MEM, TAG, "clock");
    box_set_idle(s.box, clock, now_s());
    box_scene_release(clock);

    ESP_RETURN_ON_FALSE(xTaskCreate(render_task, "box_render", RENDER_STACK, NULL,
                                    RENDER_PRIORITY, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "render task");
    s.started = true;
    ESP_LOGI(TAG, "%dx%d, %s style, %d fps", s.display.width, s.display.height,
             box_style_name(style), CONFIG_BOX_FPS);
    return ESP_OK;
}

bool box_runtime_submit(box_event_t *event, bool focus, box_accepted_t *out, box_error_t *err)
{
    if (!s.started) {
        *err = (box_error_t){.status = 503, .message = "the box is still starting"};
        return false;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool ok = box_scheduler_submit(box_scheduler(s.box), event, focus, now_s(), out, err);
    xSemaphoreGive(s.lock);
    return ok;
}

void box_runtime_status(const char *text)
{
    if (!s.started) {
        return;
    }
    if (!text) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        box_scheduler_remove_ambient(box_scheduler(s.box), BOX_SOURCE, now_s());
        xSemaphoreGive(s.lock);
        return;
    }
    box_event_t event;
    box_accepted_t accepted;
    box_error_t err;
    if (!box_event_text(&event, BOX_SOURCE, NULL, text, BOX_PRIORITY_AMBIENT, STATUS_DWELL) ||
        !box_runtime_submit(&event, false, &accepted, &err)) {
        ESP_LOGW(TAG, "status card not shown: %s", text);
    }
    box_event_clear(&event);
}

void box_runtime_announce(const char *title, const char *text, double duration)
{
    box_event_t event;
    box_accepted_t accepted;
    box_error_t err;
    if (!box_event_text(&event, BOX_SOURCE, title, text, BOX_PRIORITY_NORMAL, duration) ||
        !box_runtime_submit(&event, false, &accepted, &err)) {
        ESP_LOGW(TAG, "announcement not shown: %s", text);
    }
    box_event_clear(&event);
}

void box_runtime_clear_queue(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    box_scheduler_clear(box_scheduler(s.box), now_s());
    xSemaphoreGive(s.lock);
}

void box_runtime_clear_all(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    /* Empty the rotation and the queue and leave it empty — a blank *card*
     * would park a dark slot that other cards cycle past. */
    box_scheduler_clear_ambient(box_scheduler(s.box), now_s());
    box_scheduler_clear(box_scheduler(s.box), now_s());
    xSemaphoreGive(s.lock);
}

void box_runtime_state(box_panel_state_t *out, box_api_info_t *info)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    box_scheduler_state(box_scheduler(s.box), out);
    *info = (box_api_info_t){
        .width = s.display.width,
        .height = s.display.height,
        .shape = "square",
        .grid = box_grid(s.box),
        .fps = CONFIG_BOX_FPS,
        .style = box_style_name(box_style(s.box)),
        .brightness = box_brightness(s.box),
        .drawn_fps = s.drawn_fps,
        .shown_fps = s.shown_fps,
    };
    xSemaphoreGive(s.lock);
}

esp_err_t box_runtime_set_brightness(float brightness)
{
    if (!(brightness >= 0.0f && brightness <= 1.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s.started) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    box_request_brightness(s.box, brightness);
    xSemaphoreGive(s.lock);
    return ESP_OK;
}

esp_err_t box_runtime_set_style(box_style_t style)
{
    if (!s.started) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    box_set_style(s.box, style);
    xSemaphoreGive(s.lock);
    return ESP_OK;
}

void box_runtime_clear_ambient(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    box_scheduler_clear_ambient(box_scheduler(s.box), now_s());
    xSemaphoreGive(s.lock);
}
