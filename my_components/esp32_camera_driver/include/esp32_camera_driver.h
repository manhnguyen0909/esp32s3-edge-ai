#pragma once
#include "esp_err.h"
#include "esp_camera.h"

// Hàm khởi tạo camera với cấu hình tối ưu cho Motion Detection (Grayscale, QVGA)
esp_err_t camera_driver_init(void);
esp_err_t camera_driver_set_framesize(framesize_t size);