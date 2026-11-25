idf_component_register(SRCS "app_config.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_mosaico_app_recovery mbedtls nvs_flash)
