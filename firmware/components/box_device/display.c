#include "display.h"

#include <stdbool.h>
#include <string.h>

#include "bsp/esp_mosaico.h"
#include "esp_check.h"
#include "esp_display_present.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "box_display";

/* The CO5300 over QSPI needs X origin and width in multiples of 4 pixels. */
#define ALIGN_X 4
#define MAX_AREAS 4
/* Longest wait for in-flight frames before a panel command (a frame is ~25 ms). */
#define QUIESCE_MS 200

static struct {
    esp_display_presenter_t *presenter;
    esp_display_presenter_caps_t caps;
    int width, height;
    /* Screenshot source and the capture taken for one in-flight request. */
    const uint16_t *screen;
    SemaphoreHandle_t screen_lock;
    uint16_t *capture;
} s;

esp_err_t box_display_start(int *out_width, int *out_height)
{
    const bsp_display_config_t config = BSP_DISPLAY_DEFAULT_CONFIG();
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &panel), TAG, "initialize CO5300");
    ESP_RETURN_ON_FALSE(panel && bsp_display_get_panel_io(), ESP_ERR_INVALID_STATE, TAG,
                        "panel/io missing");

    /* The same target the Mosaico template gives GSP: GRAM panel over QSPI,
     * RGB565 with byte swap, tear-effect sync so frames never tear. */
    const esp_display_presenter_config_t presenter_config = {
        .width = BSP_LCD_H_RES,
        .height = BSP_LCD_V_RES,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .max_damage_areas = MAX_AREAS,
        .transfer_timeout_ms = ESP_DISPLAY_PRESENT_DEFAULT_TRANSFER_TIMEOUT_MS,
        .pipeline_acquire_timeout_ms = ESP_DISPLAY_PRESENT_DEFAULT_PIPELINE_ACQUIRE_TIMEOUT_MS,
        .target = {
            .hw = {
                .panel = panel,
                .io = bsp_display_get_panel_io(),
                .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                .rotation = ESP_DISPLAY_PRESENT_ROTATE_0,
                .swap_bytes = true,
#if CONFIG_BSP_CO5300_ENABLE_TE
                .te_enabled = true,
                .te_sync = {
                    .gpio_num = BSP_LCD_TE,
                    .bus_freq_hz = BSP_LCD_PIXEL_CLOCK_HZ,
                    .data_lines = BSP_LCD_DATA_WIDTH,
                },
#else
                .te_sync = {.gpio_num = -1},
#endif
            },
            .fb = {.mode = ESP_DISPLAY_PRESENT_MODE_AUTO},
        },
    };
    ESP_RETURN_ON_ERROR(esp_display_presenter_create(&presenter_config, &s.presenter), TAG,
                        "create presenter");
    ESP_RETURN_ON_ERROR(esp_display_presenter_get_caps(s.presenter, &s.caps), TAG, "caps");
    s.width = BSP_LCD_H_RES;
    s.height = BSP_LCD_V_RES;
    ESP_LOGI(TAG, "%dx%d, contract %d, draw buffer %u bytes", s.width, s.height,
             (int)s.caps.contract, (unsigned)s.caps.drawbuf_bytes);
    *out_width = s.width;
    *out_height = s.height;
    return ESP_OK;
}

esp_err_t box_display_set_brightness(int percent)
{
    if (s.presenter) {
        /* A panel command mid-transfer hangs the QSPI bus; let frames drain. */
        ESP_RETURN_ON_ERROR(esp_display_presenter_quiesce(s.presenter, QUIESCE_MS), TAG,
                            "quiesce before brightness");
    }
    return bsp_display_brightness_set(percent < 0 ? 0 : percent > 100 ? 100 : percent);
}

static esp_display_present_area_t aligned(const box_rect_t *r)
{
    esp_display_present_area_t a = {
        .x1 = r->x1 - r->x1 % ALIGN_X,
        .y1 = r->y1,
        .x2 = r->x2 + (ALIGN_X - 1 - r->x2 % ALIGN_X),
        .y2 = r->y2,
    };
    a.x2 = a.x2 < s.width ? a.x2 : s.width - 1;
    return a;
}

/* Copy one area of the screen into presenter-owned draw buffers, in bands as
 * tall as each lease holds. */
static esp_err_t submit_area(const uint16_t *screen, const esp_display_present_area_t *area)
{
    const size_t width = (size_t)(area->x2 - area->x1 + 1);
    const size_t stride = width * sizeof(uint16_t);
    int y = area->y1;
    while (y <= area->y2) {
        esp_display_presenter_buffer_t buffer = {0};
        ESP_RETURN_ON_ERROR(esp_display_presenter_acquire_buffer(s.presenter, &buffer), TAG,
                            "acquire");
        size_t rows = buffer.capacity_bytes / stride;
        ESP_RETURN_ON_FALSE(rows > 0, ESP_ERR_NO_MEM, TAG, "draw buffer smaller than a row");
        if (rows > (size_t)(area->y2 - y + 1)) {
            rows = (size_t)(area->y2 - y + 1);
        }
        uint8_t *out = buffer.surface.pixels;
        for (size_t r = 0; r < rows; ++r) {
            memcpy(out + r * stride, screen + (size_t)(y + (int)r) * (size_t)s.width + area->x1,
                   stride);
        }
        const esp_display_present_area_t band = {
            .x1 = area->x1, .y1 = y, .x2 = area->x2, .y2 = y + (int)rows - 1,
        };
        ESP_RETURN_ON_ERROR(esp_display_presenter_submit_buffer(s.presenter, &buffer, &band,
                                                                stride),
                            TAG, "submit");
        y += (int)rows;
    }
    return ESP_OK;
}

/* FULL/DIRECT contracts lease the whole logical surface: copy all of it. */
static esp_err_t submit_full_surface(const uint16_t *screen)
{
    esp_display_presenter_buffer_t buffer = {0};
    ESP_RETURN_ON_ERROR(esp_display_presenter_acquire_buffer(s.presenter, &buffer), TAG,
                        "acquire");
    const size_t row = (size_t)s.width * sizeof(uint16_t);
    const size_t stride = buffer.surface.stride_bytes ? buffer.surface.stride_bytes : row;
    ESP_RETURN_ON_FALSE(buffer.capacity_bytes >= stride * (size_t)s.height, ESP_ERR_NO_MEM, TAG,
                        "surface too small");
    uint8_t *out = buffer.surface.pixels;
    for (int y = 0; y < s.height; ++y) {
        memcpy(out + (size_t)y * stride, screen + (size_t)y * (size_t)s.width, row);
    }
    const esp_display_present_area_t all = {0, 0, s.width - 1, s.height - 1};
    return esp_display_presenter_submit_buffer(s.presenter, &buffer, &all, stride);
}

esp_err_t box_display_present(const uint16_t *screen, const box_rect_t *dirty)
{
    const esp_display_present_area_t want = aligned(dirty);
    const esp_display_present_surface_request_t request = {
        .dirty_areas = &want,
        .dirty_area_count = 1,
        .render_alignment = {.x_pixels = ALIGN_X, .y_pixels = 1, .width_pixels = ALIGN_X,
                             .height_pixels = 1},
    };
    esp_display_present_area_t areas[MAX_AREAS];
    size_t count = 0;
    bool full = false;
    ESP_RETURN_ON_ERROR(esp_display_presenter_begin_next_frame(s.presenter, &request, areas,
                                                               MAX_AREAS, &count, &full),
                        TAG, "begin frame");
    const esp_display_present_area_t whole = {0, 0, s.width - 1, s.height - 1};
    if (full || count == 0) {
        areas[0] = whole;
        count = 1;
    }
    esp_err_t err = ESP_OK;
    if (s.caps.contract == ESP_DISPLAY_PRESENT_CONTRACT_PARTITION) {
        for (size_t i = 0; i < count && err == ESP_OK; ++i) {
            err = submit_area(screen, &areas[i]);
        }
    } else {
        full = true;
        err = submit_full_surface(screen);
    }
    if (err != ESP_OK) {
        esp_display_presenter_cancel_frame(s.presenter);
        return err;
    }
    const esp_display_presenter_submit_t submit = full
        ? (esp_display_presenter_submit_t){.coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL}
        : (esp_display_presenter_submit_t){
              .coverage = ESP_DISPLAY_PRESENT_COVERAGE_AREAS,
              .areas = areas,
              .area_count = count,
          };
    return esp_display_presenter_commit_frame(s.presenter, &submit);
}

uint64_t box_display_frames_shown(void)
{
    esp_display_presenter_frame_status_t status;
    if (!s.presenter || esp_display_presenter_get_frame_status(s.presenter, &status) != ESP_OK) {
        return 0;
    }
    return status.completed_transfer_frame;
}

/* --- iris screenshot ------------------------------------------------------ */

static esp_err_t screen_begin(const esp_iris_media_desc_t *requested,
                              esp_iris_media_desc_t *actual, uint32_t *total_size, void *ctx)
{
    (void)requested;
    (void)ctx;
    if (!actual || !total_size || s.capture) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t bytes = (size_t)s.width * (size_t)s.height * sizeof(uint16_t);
    s.capture = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s.capture) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s.screen_lock, portMAX_DELAY);
    memcpy(s.capture, s.screen, bytes);
    xSemaphoreGive(s.screen_lock);
    *actual = (esp_iris_media_desc_t){
        .width = (uint16_t)s.width,
        .height = (uint16_t)s.height,
        .stride = (uint32_t)s.width * sizeof(uint16_t),
        .format = ESP_IRIS_PIXEL_FORMAT_RGB565,
    };
    *total_size = (uint32_t)bytes;
    return ESP_OK;
}

static esp_err_t screen_read(uint32_t offset, uint8_t *out, size_t capacity, size_t *out_size,
                             void *ctx)
{
    (void)ctx;
    const size_t total = (size_t)s.width * (size_t)s.height * sizeof(uint16_t);
    if (!s.capture || !out || !out_size || capacity == 0 || offset >= total) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t size = total - offset < capacity ? total - offset : capacity;
    memcpy(out, (const uint8_t *)s.capture + offset, size);
    *out_size = size;
    return ESP_OK;
}

static void screen_end(void *ctx)
{
    (void)ctx;
    heap_caps_free(s.capture);
    s.capture = NULL;
}

esp_err_t box_display_register_screenshot(const uint16_t *screen, SemaphoreHandle_t lock)
{
    s.screen = screen;
    s.screen_lock = lock;
    const esp_iris_screen_backend_t backend = {
        .begin = screen_begin,
        .read = screen_read,
        .end = screen_end,
        .user_ctx = NULL,
    };
    return esp_iris_screen_register(&backend);
}
