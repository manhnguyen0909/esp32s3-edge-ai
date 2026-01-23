idf_component_register(
    SRCS "esp32_camera_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp32-camera log esp_timer
)
