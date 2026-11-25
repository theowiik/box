#include "box_runtime.h"

#include <string.h>

#include "box/engine.h"
#include "box/led.h"
#include "display.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "box";

#define RENDER_STACK 6144
#define RENDER_PRIORITY 5
/* The box's own cards and announcements use this source. */
#define BOX_SOURCE "box"
#define STATUS_DWELL 12.0
/* The window the measured frame rate is averaged over, in microseconds. */
#define STATS_PERIOD_US (5 * 1000 * 1000)

/* Ownership: the core objects belong to the runtime for the life of the
 * firmware. `core` guards the scheduler and engine (the HTTP and Wi-Fi tasks
 * submit, the render task ticks and renders); `screen` guards the LED image
 * (the render task draws, the ESP-Iris task copies it for screenshots). */
static struct {
    SemaphoreHandle_t core;
    SemaphoreHandle_t screen;
    box_engine_t engine;
    box_scheduler_t *scheduler;
    box_led_t *led;
    float brightness;
    /* Percent waiting for the render task to apply, or -1. Brightness is a
     * panel command, so only the task that owns the display bus sends it. */
    int pending_brightness;
    /* Measured over the last stats window: frames drawn that changed the
     * screen, and frames whose transfer to the panel completed. The presenter
     * may merge frames when the bus is busy, so shown can trail drawn. */
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

static void render_task(void *arg)
{
    (void)arg;
    TickType_t period = pdMS_TO_TICKS(1000 / CONFIG_BOX_FPS);
    period = period > 0 ? period : 1;
    TickType_t wake = xTaskGetTickCount();
    uint32_t failures = 0;
    uint32_t drawn = 0;
    uint64_t shown_from = box_display_frames_shown();
    int64_t stats_from = esp_timer_get_time();
    for (;;) {
        const double now = now_s();
        xSemaphoreTake(s.core, portMAX_DELAY);
        box_scheduler_tick(s.scheduler, now);
        /* The engine's frame is only ever written here, so it can be read
         * after the lock is released. */
        const box_frame_t *frame = box_engine_render(&s.engine, now);
        const int brightness = s.pending_brightness;
        s.pending_brightness = -1;
        xSemaphoreGive(s.core);

        if (brightness >= 0) {
            const esp_err_t err = box_display_set_brightness(brightness);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "brightness %d%% not applied (%s)", brightness,
                         esp_err_to_name(err));
            }
        }
        xSemaphoreTake(s.screen, portMAX_DELAY);
        box_rect_t dirty;
        if (box_led_update(s.led, frame, &dirty)) {
            const esp_err_t err = box_display_present(box_led_pixels(s.led), &dirty);
            ++drawn;
            if (err != ESP_OK && failures++ % 100 == 0) {
                ESP_LOGW(TAG, "present failed (%s), %u so far", esp_err_to_name(err),
                         (unsigned)failures);
            }
        }
        xSemaphoreGive(s.screen);

        const int64_t now_us = esp_timer_get_time();
        if (now_us - stats_from >= STATS_PERIOD_US) {
            const uint64_t shown = box_display_frames_shown();
            const double seconds = (double)(now_us - stats_from) / 1e6;
            xSemaphoreTake(s.core, portMAX_DELAY);
            s.drawn_fps = (float)(drawn / seconds);
            s.shown_fps = (float)((double)(shown - shown_from) / seconds);
            xSemaphoreGive(s.core);
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
    s.core = xSemaphoreCreateMutex();
    s.screen = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s.core && s.screen, ESP_ERR_NO_MEM, TAG, "locks");

    int width, height;
    ESP_RETURN_ON_ERROR(box_display_start(&width, &height), TAG, "display");
    ESP_RETURN_ON_FALSE(box_engine_init(&s.engine, CONFIG_BOX_PANEL_SIZE, CONFIG_BOX_PANEL_SIZE),
                        ESP_ERR_NO_MEM, TAG, "engine");
    s.scheduler = box_scheduler_new(&s.engine, esp_random());
    ESP_RETURN_ON_FALSE(s.scheduler, ESP_ERR_NO_MEM, TAG, "scheduler");
    box_scheduler_set_log(s.scheduler, log_line, NULL);
    s.led = box_led_new(CONFIG_BOX_PANEL_SIZE, CONFIG_BOX_PANEL_SIZE, width, height);
    ESP_RETURN_ON_FALSE(s.led, ESP_ERR_NO_MEM, TAG, "led renderer");

    /* Before the render task exists, so this task may still use the bus. */
    s.pending_brightness = -1;
    s.brightness = brightness;
    ESP_RETURN_ON_ERROR(box_display_set_brightness((int)(brightness * 100.0f + 0.5f)), TAG,
                        "brightness");
    const esp_err_t shot = box_display_register_screenshot(box_led_pixels(s.led), s.screen);
    if (shot != ESP_OK) {
        ESP_LOGW(TAG, "screenshots unavailable (%s)", esp_err_to_name(shot));
    }
    ESP_RETURN_ON_FALSE(xTaskCreate(render_task, "box_render", RENDER_STACK, NULL,
                                    RENDER_PRIORITY, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "render task");
    s.started = true;
    ESP_LOGI(TAG, "panel %dx%d dots, %d px pitch, %d fps", CONFIG_BOX_PANEL_SIZE,
             CONFIG_BOX_PANEL_SIZE, box_led_pitch(s.led), CONFIG_BOX_FPS);
    return ESP_OK;
}

bool box_runtime_submit(box_event_t *event, bool focus, box_accepted_t *out, box_error_t *err)
{
    if (!s.started) {
        *err = (box_error_t){.status = 503, .message = "the box is still starting"};
        return false;
    }
    xSemaphoreTake(s.core, portMAX_DELAY);
    const bool ok = box_scheduler_submit(s.scheduler, event, focus, now_s(), out, err);
    xSemaphoreGive(s.core);
    return ok;
}

void box_runtime_status(const char *text)
{
    if (!s.started) {
        return;
    }
    if (!text) {
        xSemaphoreTake(s.core, portMAX_DELAY);
        box_scheduler_remove_ambient(s.scheduler, BOX_SOURCE, now_s());
        xSemaphoreGive(s.core);
        return;
    }
    box_event_t event;
    box_accepted_t accepted;
    box_error_t err;
    if (!box_event_text(&event, BOX_SOURCE, text, BOX_PRIORITY_AMBIENT, STATUS_DWELL) ||
        !box_runtime_submit(&event, false, &accepted, &err)) {
        ESP_LOGW(TAG, "status card not shown: %s", text);
    }
    box_event_clear(&event);
}

void box_runtime_announce(const char *text, double duration)
{
    box_event_t event;
    box_accepted_t accepted;
    box_error_t err;
    if (!box_event_text(&event, BOX_SOURCE, text, BOX_PRIORITY_NORMAL, duration) ||
        !box_runtime_submit(&event, false, &accepted, &err)) {
        ESP_LOGW(TAG, "announcement not shown: %s", text);
    }
    box_event_clear(&event);
}

void box_runtime_clear_queue(void)
{
    xSemaphoreTake(s.core, portMAX_DELAY);
    box_scheduler_clear(s.scheduler, now_s());
    xSemaphoreGive(s.core);
}

void box_runtime_clear_all(void)
{
    xSemaphoreTake(s.core, portMAX_DELAY);
    /* Empty the rotation and the queue and leave it empty — a blank *card*
     * would park a dark slot that other cards cycle past. */
    box_scheduler_clear_ambient(s.scheduler, now_s());
    box_scheduler_clear(s.scheduler, now_s());
    xSemaphoreGive(s.core);
}

void box_runtime_state(box_panel_state_t *out)
{
    xSemaphoreTake(s.core, portMAX_DELAY);
    box_scheduler_state(s.scheduler, out);
    xSemaphoreGive(s.core);
}

void box_runtime_info(box_runtime_info_t *out)
{
    xSemaphoreTake(s.core, portMAX_DELAY);
    *out = (box_runtime_info_t){
        .width = s.engine.frame.width,
        .height = s.engine.frame.height,
        .fps = CONFIG_BOX_FPS,
        .brightness = s.brightness,
        .drawn_fps = s.drawn_fps,
        .shown_fps = s.shown_fps,
    };
    xSemaphoreGive(s.core);
}

esp_err_t box_runtime_set_brightness(float brightness)
{
    if (!(brightness >= 0.0f && brightness <= 1.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s.started) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Applied by the render task within a frame; see pending_brightness. */
    xSemaphoreTake(s.core, portMAX_DELAY);
    s.brightness = brightness;
    s.pending_brightness = (int)(brightness * 100.0f + 0.5f);
    xSemaphoreGive(s.core);
    return ESP_OK;
}
