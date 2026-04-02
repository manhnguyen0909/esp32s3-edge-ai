#include "esp32_camera_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// Pinout cho ESP32-S3 (Freenove/AI Thinker)
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 42
#define CAM_PIN_SIOC 41
#define CAM_PIN_D7 38
#define CAM_PIN_D6 45
#define CAM_PIN_D5 48
#define CAM_PIN_D4 21
#define CAM_PIN_D3 13
#define CAM_PIN_D2 11
#define CAM_PIN_D1 12
#define CAM_PIN_D0 14
#define CAM_PIN_VSYNC 40
#define CAM_PIN_HREF 39
#define CAM_PIN_PCLK 47

static const char *TAG = "CAM_DRIVER";
// Mượn biến cờ hiệu từ file main.c
extern volatile bool camera_is_restarting;

// ĐƯA BIẾN CONFIG RA NGOÀI ĐỂ DÙNG CHUNG
static camera_config_t camera_config = {
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
    
    .xclk_freq_hz = 16000000,           
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_GRAYSCALE, // Vẫn giữ nguyên ảnh xám
    .frame_size = FRAMESIZE_SVGA,         // Mặc định lúc mới bật
    .fb_count = 2,                     
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

esp_err_t camera_driver_init(void) {
    // Chỉ gọi hàm init bằng biến toàn cục
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);   
        s->set_hmirror(s, 1); 
    }
    ESP_LOGI(TAG, "Camera Init Success");
    return ESP_OK;
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Mượn biến cờ hiệu từ file main.c
extern volatile bool camera_is_restarting;

esp_err_t camera_driver_set_framesize(framesize_t size) {
    ESP_LOGI(TAG, "Đang tiến hành tắt Camera để đổi độ phân giải...");
    
    // 1. Bật cờ để khóa mõm motion_detection_task bên Core 1 lại
    camera_is_restarting = true;
    
    // 2. Delay một chút (100ms) để chắc chắn motion_task đã nhả camera ra
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Tắt camera an toàn
    esp_camera_deinit();

    // 4. Khởi tạo lại với kích thước mới
    camera_config.frame_size = size;
    esp_err_t err = esp_camera_init(&camera_config);
    
    if (err == ESP_OK) {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL) {
            s->set_vflip(s, 1);
            s->set_hmirror(s, 1);
        }
        ESP_LOGI(TAG, "Đổi Frame Size THÀNH CÔNG sang: %d", size);
        
        // 5. Mở khóa cho motion_detection_task chạy tiếp
        camera_is_restarting = false; 
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "LỖI NẶNG: Không thể khởi tạo lại Camera!");
        // Vẫn phải mở khóa để hệ thống không bị treo vĩnh viễn
        camera_is_restarting = false; 
        return ESP_FAIL;
    }
}