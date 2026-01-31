idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        freertos
        log
        esp_event
        oled_driver
)
