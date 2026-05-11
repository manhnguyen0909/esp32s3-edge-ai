# Tự động tìm và gom TẤT CẢ các file .c và .cpp có trong thư mục main
file(GLOB_RECURSE srcs "*.c" "*.cpp" "*.cc" "*.S")

idf_component_register(
    SRCS ${srcs}
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
        JPEGDEC
)
