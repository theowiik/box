# The box on ESP-Mosaico: every ESP-IDF, FreeRTOS and hardware call lives here.
# The box's logic is ../../../core (component "core"), shared by any target;
# mosaico_display.c is this board's implementation of its display interface.
idf_component_register(SRCS "runtime.c" "mosaico_display.c" "net.c" "http.c"
    INCLUDE_DIRS "include"
    REQUIRES core esp_common
    PRIV_REQUIRES app_config esp-mosaico-bsp esp_display_present esp_http_server
        esp_wifi esp_netif esp_event esp_timer esp_iris mdns mbedtls freertos log)
