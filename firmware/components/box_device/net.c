#include "box_net.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mbedtls/platform_util.h"
#include "mdns.h"
#include "sdkconfig.h"

static const char *TAG = "box_net";

#define RETRY_FIRST_US (1 * 1000 * 1000)
#define RETRY_MAX_US (30 * 1000 * 1000)

static struct {
    box_net_status_cb_t on_status;
    void *ctx;
    esp_timer_handle_t retry;
    int64_t retry_us;
    bool connected;
} s;

static void notify(box_net_status_t status, const char *ip)
{
    if (s.on_status) {
        s.on_status(status, ip, s.ctx);
    }
}

static void retry_now(void *arg)
{
    (void)arg;
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        retry_now(NULL);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        if (s.connected) {
            s.connected = false;
            notify(BOX_NET_CONNECTING, NULL);
        }
        ESP_LOGW(TAG, "disconnected (reason %d), retrying in %llds", e ? e->reason : -1,
                 (long long)(s.retry_us / 1000000));
        esp_timer_start_once(s.retry, (uint64_t)s.retry_us);
        s.retry_us = s.retry_us * 2 < RETRY_MAX_US ? s.retry_us * 2 : RETRY_MAX_US;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        s.connected = true;
        s.retry_us = RETRY_FIRST_US;
        ESP_LOGI(TAG, "connected: http://%s.local (%s)", CONFIG_BOX_HOSTNAME, ip);
        notify(BOX_NET_CONNECTED, ip);
    }
}

static esp_err_t ignore_already_done(esp_err_t err)
{
    /* Another component may have set these up first; that is fine. */
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        err = mdns_hostname_set(CONFIG_BOX_HOSTNAME);
    }
    if (err == ESP_OK) {
        err = mdns_instance_name_set("box");
    }
    if (err == ESP_OK) {
        err = mdns_service_add("box", "_http", "_tcp", CONFIG_BOX_HTTP_PORT, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS unavailable (%s); use the IP address", esp_err_to_name(err));
        return;
    }
    /* ESP-Iris advertises its own TCP endpoint on the same responder. */
    const esp_iris_mdns_config_t iris = {.instance_prefix = "box"};
    err = esp_iris_mdns_register(&iris);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ESP-Iris not advertised over mDNS (%s)", esp_err_to_name(err));
    }
}

esp_err_t box_net_start(box_net_status_cb_t on_status, void *ctx)
{
    s.on_status = on_status;
    s.ctx = ctx;
    s.retry_us = RETRY_FIRST_US;

    /* The TCP/IP stack comes up even without Wi-Fi: the HTTP server needs it. */
    esp_err_t err = ignore_already_done(esp_netif_init());
    if (err == ESP_OK) {
        err = ignore_already_done(esp_event_loop_create_default());
    }
    ESP_RETURN_ON_ERROR(err, TAG, "TCP/IP stack");

    app_config_wifi_t creds;
    const esp_err_t creds_err = app_config_load_wifi(&creds);
    if (creds_err != ESP_OK) {
        ESP_LOGW(TAG, "no Wi-Fi saved (%s); set it up in Vibe Mode", esp_err_to_name(creds_err));
        notify(BOX_NET_UNCONFIGURED, NULL);
        return ESP_OK;
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) {
        mbedtls_platform_zeroize(&creds, sizeof(creds));
        ESP_LOGE(TAG, "could not create the Wi-Fi interface");
        return ESP_FAIL;
    }
    esp_netif_set_hostname(netif, CONFIG_BOX_HOSTNAME);

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    /* Credentials stay in Vibe Mode's store: never copy them into this app's NVS. */
    if (err == ESP_OK) {
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, creds.ssid, strnlen(creds.ssid, sizeof(config.sta.ssid)));
    memcpy(config.sta.password, creds.password,
           strnlen(creds.password, sizeof(config.sta.password)));
    /* Refuse a downgrade to an open or WEP network posing as the saved one. */
    config.sta.threshold.authmode = creds.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    mbedtls_platform_zeroize(&creds, sizeof(creds));
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    mbedtls_platform_zeroize(&config, sizeof(config));
    ESP_RETURN_ON_ERROR(err, TAG, "configure Wi-Fi");

    const esp_timer_create_args_t retry = {.callback = retry_now, .name = "box_wifi_retry"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry, &s.retry), TAG, "retry timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL),
                        TAG, "IP events");
    notify(BOX_NET_CONNECTING, NULL);
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
    start_mdns();
    return ESP_OK;
}
