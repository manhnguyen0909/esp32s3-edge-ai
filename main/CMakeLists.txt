idf_component_register(
    SRCS "main.c" "person_ai/ai_person.cc" "image_utils.c" "person_ai/person_detect_model_data.cc"
    INCLUDE_DIRS
        "."
        "person_ai"
    REQUIRES 
        wifi_manager 
        log 
        http_stream 
        esp32_camera_driver 
        esp_timer 
        esp_http_server 
        esp_netif 
        ota_server 
        oled_driver
        espressif__esp-tflite-micro
)
