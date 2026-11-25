# The box on ESP-Mosaico: every ESP-IDF, FreeRTOS and hardware call lives here,
# so box_core stays portable.
idf_component_register(SRCS "runtime.c" "display.c" "net.c" "http.c"
    INCLUDE_DIRS "include"
    REQUIRES box_core esp_common
    PRIV_REQUIRES app_config esp-mosaico-bsp esp_display_present esp_http_server
        esp_wifi esp_netif esp_event esp_timer esp_iris mdns mbedtls freertos log)
