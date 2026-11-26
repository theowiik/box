// The box on ESP-Mosaico: boot, then wire the runtime to Wi-Fi and HTTP.
#include <stdio.h>

#include "app_config.h"
#include "box_http.h"
#include "box_net.h"
#include "box_runtime.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "box_main";

/* Seconds the box shows its own address after joining the network. */
#define ADDRESS_DURATION 15.0

static void on_network(box_net_status_t status, const char *ip, void *ctx)
{
    (void)ctx;
    switch (status) {
    case BOX_NET_UNCONFIGURED:
        box_runtime_status("No Wi-Fi. Set it up in Vibe Mode");
        break;
    case BOX_NET_CONNECTING:
        box_runtime_status("Connecting to Wi-Fi…");
        break;
    case BOX_NET_CONNECTED: {
        box_runtime_status(NULL);
        char host[48];
        snprintf(host, sizeof(host), "%s.local", CONFIG_BOX_HOSTNAME);
        box_runtime_announce(host, ip, ADDRESS_DURATION);
        break;
    }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    /* First, so Vibe Mode stays reachable even if anything below fails. */
    iris_ota_support_start();

    uint32_t brightness = APP_CONFIG_BRIGHTNESS_FALLBACK;
    (void)app_config_load_brightness(&brightness); /* falls back on any error */

    esp_err_t err = box_runtime_start((float)brightness / 100.0f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "box failed to start (%s); Vibe Mode remains reachable",
                 esp_err_to_name(err));
        return;
    }
    /* Network first: it brings up the TCP/IP stack the HTTP server needs. */
    err = box_net_start(on_network, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network unavailable (%s)", esp_err_to_name(err));
        box_runtime_status("Network failed to start");
        return;
    }
    err = box_http_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP API unavailable (%s)", esp_err_to_name(err));
        box_runtime_status("HTTP API failed to start");
        return;
    }
    ESP_LOGI(TAG, "box ready");
}
