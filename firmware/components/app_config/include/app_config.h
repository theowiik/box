// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];      /* 1..32 bytes plus NUL. */
    char password[65];  /* Empty, 8..63 printable ASCII bytes, or 64 hex digits, plus NUL. */
} app_config_wifi_t;

/* Read saved Vibe Mode Wi-Fi credentials in task context; out must be non-NULL.
 * Returns ESP_OK only for a complete, valid pair; clears out on any failure.
 * The caller owns the snapshot and must clear it after use. */
esp_err_t app_config_load_wifi(app_config_wifi_t *out);

/* The panel brightness the user picked in Vibe Mode, in percent. */
#define APP_CONFIG_BRIGHTNESS_MIN 20
#define APP_CONFIG_BRIGHTNESS_MAX 100
#define APP_CONFIG_BRIGHTNESS_FALLBACK 80

/* Read the Vibe Mode brightness preference in task context, after
 * iris_ota_support_start() has initialized the sysmeta partition. Returns
 * ESP_OK with the stored value; on a missing or out-of-range value returns an
 * error and sets *out_percent to APP_CONFIG_BRIGHTNESS_FALLBACK. */
esp_err_t app_config_load_brightness(uint32_t *out_percent);

#ifdef __cplusplus
}
#endif
