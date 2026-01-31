idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager log http_stream esp32_camera_driver esp_timer esp_http_server esp_netif ota_server oled_driver
)
