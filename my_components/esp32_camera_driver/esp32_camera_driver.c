#include "esp32_camera_driver.h"
#include "esp_log.h"

// Pinout cho ESP32-S3 (Freenove/AI Thinker)
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    42
#define CAM_PIN_SIOC    41
#define CAM_PIN_D7      38
#define CAM_PIN_D6      45
#define CAM_PIN_D5      48
#define CAM_PIN_D4      21
#define CAM_PIN_D3      13
#define CAM_PIN_D2      11
#define CAM_PIN_D1      12
#define CAM_PIN_D0      14
#define CAM_PIN_VSYNC   40
#define CAM_PIN_HREF    39
#define CAM_PIN_PCLK    47

static const char *TAG = "CAM_DRIVER";

esp_err_t camera_driver_init(void) {
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        
        // Cấu hình tối ưu
        .xclk_freq_hz = 12000000,           // 12MHz để tránh OVF
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_GRAYSCALE,// Dùng ảnh xám xử lý cho nhanh
        .frame_size = FRAMESIZE_QVGA,       // 320x240
        .jpeg_quality = 50,                 // Chất lượng vừa phải
        .fb_count = 4,                      // Buffer lớn tránh giật
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }
    ESP_LOGI(TAG, "Camera Init Success");
    return ESP_OK;
}