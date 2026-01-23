# ESP32 Camera Driver

Driver wrapper đơn giản và mạnh mẽ cho ESP32 Camera, hỗ trợ các board phổ biến và cấu hình tùy chỉnh.

## Tính năng

✅ **Multi-board support**: ESP32-CAM, ESP32-S3-EYE, custom boards  
✅ **Flexible capture**: Single-shot, continuous streaming, callback-based  
✅ **Auto-conversion**: Grayscale/RGB → JPEG  
✅ **Runtime settings**: Brightness, contrast, flip, effects  
✅ **Statistics**: FPS, bandwidth, failed frames  
✅ **Thread-safe**: FreeRTOS task integration  
✅ **Low memory**: PSRAM support, efficient buffering  

## Hỗ trợ phần cứng

| Board | Sensor | Status |
|-------|--------|--------|
| AI-Thinker ESP32-CAM | OV2640 | ✅ Tested |
| ESP32-S3-EYE | OV2640 | ✅ Tested |
| ESP32-S3 Custom | OV2640/OV3660/OV5640 | ✅ Supported |
| Custom boards | Any ESP32 compatible | ✅ Configurable |

## Cài đặt

### Yêu cầu

- ESP-IDF v4.4 hoặc mới hơn
- ESP32-CAM library (`esp32-camera` component)

### Thêm vào project

```bash
cd your_project/components
git clone https://github.com/yourusername/esp32_camera_driver.git
```

Hoặc thêm vào `idf_component.yml`:

```yaml
dependencies:
  esp32_camera_driver:
    git: https://github.com/yourusername/esp32_camera_driver.git
```

### Cấu trúc

```
your_project/
├── my_components/
│   └── esp32_camera_driver/
│       ├── include
|       |   └──esp32_camera_driver.h
│       ├── esp32_camera_driver.c
|       ├── camera_example.c
│       └── CMakeLists.txt
└── main/
    └── app.c
```

## Sử dụng cơ bản

### 1. Quick Start (ESP32-CAM)

```c
#include "esp32_camera_driver.h"

void app_main(void) {
    // Init với cấu hình mặc định
    camera_driver_handle_t camera;
    camera_config_t config = camera_get_board_config(CAMERA_BOARD_ESP32_CAM);
    
    camera_driver_init(&config, &camera);
    
    // Capture frame
    camera_fb_t *fb = NULL;
    camera_driver_capture(camera, &fb, 1000);
    
    if (fb) {
        printf("Captured: %dx%d, %zu bytes\n", 
               fb->width, fb->height, fb->len);
        camera_driver_return_frame(camera, fb);
    }
    
    camera_driver_deinit(camera);
}
```

### 2. Continuous Streaming

```c
bool my_callback(camera_fb_t *fb, void *user_data) {
    // Process frame (do NOT free it!)
    printf("Frame: %zu bytes\n", fb->len);
    return true;  // Continue streaming
}

void start_streaming(void) {
    camera_driver_handle_t camera;
    camera_config_t config = camera_get_default_config(CAMERA_BOARD_ESP32_S3_CUSTOM);
    camera_driver_init(&config, &camera);
    
    camera_driver_start_stream(
        camera,
        my_callback,
        NULL,     // user data
        4096,     // stack size
        2         // priority
    );
}
```

### 3. Custom Configuration

```c
camera_config_t config = {
    .board_type = CAMERA_BOARD_ESP32_CAM,
    .resolution = CAMERA_RES_VGA,          // 640x480
    .pixel_format = CAMERA_FORMAT_JPEG,
    .jpeg_quality = 10,                    // 10-63 (lower = better)
    .fb_count = 2,                         // Double buffering
    .fb_location = CAMERA_FB_PSRAM,        // Use PSRAM
    .grab_mode = CAMERA_GRAB_LATEST,       // Always fresh
    .xclk_freq_hz = 20000000,              // 20MHz
    .auto_init = true
};

camera_driver_handle_t camera;
camera_driver_init(&config, &camera);
```

## API Reference

### Configuration

#### Board Types
```c
CAMERA_BOARD_ESP32_CAM          // AI-Thinker ESP32-CAM
CAMERA_BOARD_ESP32_S3_EYE       // ESP32-S3-EYE
CAMERA_BOARD_ESP32_S3_CUSTOM    // Custom ESP32-S3
CAMERA_BOARD_CUSTOM             // Fully custom pins
```

#### Resolutions
```c
CAMERA_RES_QQVGA    // 160x120
CAMERA_RES_QVGA     // 320x240  (recommended for streaming)
CAMERA_RES_VGA      // 640x480
CAMERA_RES_SVGA     // 800x600
CAMERA_RES_XGA      // 1024x768
CAMERA_RES_SXGA     // 1280x1024
CAMERA_RES_UXGA     // 1600x1200 (requires PSRAM)
```

#### Pixel Formats
```c
CAMERA_FORMAT_JPEG       // JPEG compressed (best for streaming)
CAMERA_FORMAT_GRAYSCALE  // 8-bit grayscale
CAMERA_FORMAT_RGB565     // 16-bit RGB
CAMERA_FORMAT_YUV422     // YUV format
```

### Core Functions

| Function | Description |
|----------|-------------|
| `camera_get_default_config()` | Lấy cấu hình mặc định cho board |
| `camera_driver_init()` | Khởi tạo camera |
| `camera_driver_start()` | Start camera (nếu không auto) |
| `camera_driver_stop()` | Stop camera |
| `camera_driver_capture()` | Capture single frame |
| `camera_driver_return_frame()` | Trả frame về driver |
| `camera_driver_start_stream()` | Start continuous capture |
| `camera_driver_stop_stream()` | Stop streaming |
| `camera_driver_deinit()` | Cleanup |

### Settings Functions

| Function | Description |
|----------|-------------|
| `camera_driver_set_settings()` | Brightness, contrast, saturation |
| `camera_driver_set_effect()` | Special effects (grayscale, negative, etc.) |
| `camera_driver_set_whitebal()` | Auto white balance |
| `camera_driver_set_exposure_ctrl()` | Auto exposure |
| `camera_driver_set_gain_ctrl()` | Auto gain |
| `camera_driver_set_hmirror()` | Horizontal flip |
| `camera_driver_set_vflip()` | Vertical flip |

### Utility Functions

| Function | Description |
|----------|-------------|
| `camera_driver_get_sensor()` | Get sensor info |
| `camera_driver_get_stats()` | Get statistics |
| `camera_driver_reset_stats()` | Reset counters |
| `camera_driver_convert_to_jpeg()` | Convert frame to JPEG |

## Ví dụ nâng cao

### 1. Adjust Image Quality

```c
camera_driver_handle_t camera;
camera_driver_init(&config, &camera);

// Improve image quality
camera_driver_set_settings(camera, 
    1,   // brightness: -2 to 2
    1,   // contrast: -2 to 2
    0    // saturation: -2 to 2
);

// Enable auto-adjustments
camera_driver_set_exposure_ctrl(camera, true);
camera_driver_set_whitebal(camera, true);
camera_driver_set_gain_ctrl(camera, true);
```

### 2. Flip Camera

```c
// Camera mounted upside-down
camera_driver_set_vflip(camera, true);
camera_driver_set_hmirror(camera, true);
```

### 3. Custom Board Pins

```c
camera_pin_config_t custom_pins = {
    .pin_pwdn = 32,
    .pin_reset = -1,
    .pin_xclk = 0,
    .pin_sccb_sda = 26,
    .pin_sccb_scl = 27,
    .pin_d7 = 35,
    .pin_d6 = 34,
    .pin_d5 = 39,
    .pin_d4 = 36,
    .pin_d3 = 21,
    .pin_d2 = 19,
    .pin_d1 = 18,
    .pin_d0 = 5,
    .pin_vsync = 25,
    .pin_href = 23,
    .pin_pclk = 22
};

camera_config_t config = camera_get_default_config(CAMERA_BOARD_CUSTOM);
config.custom_pins = &custom_pins;
```

### 4. Performance Monitoring

```c
camera_stats_t stats;
camera_driver_get_stats(camera, &stats);

printf("Frames: %lu captured, %lu failed\n", 
       stats.frames_captured, stats.frames_failed);
printf("Bytes: %lu\n", stats.total_bytes);
printf("Avg frame time: %lu us\n", stats.avg_frame_time_us);

float fps = 1000000.0f / stats.avg_frame_time_us;
printf("FPS: %.1f\n", fps);
```

### 5. Integration với MJPEG Stream

```c
// Kết hợp camera driver + MJPEG streaming
bool stream_callback(camera_fb_t *fb, void *user_data) {
    mjpeg_stream_handle_t *stream = (mjpeg_stream_handle_t *)user_data;
    
    // Convert to JPEG if needed
    uint8_t *jpg_buf;
    size_t jpg_len;
    camera_driver_convert_to_jpeg(camera, fb, 85, &jpg_buf, &jpg_len);
    
    // Send to MJPEG stream
    mjpeg_frame_t *frame = mjpeg_stream_create_frame(jpg_buf, jpg_len, 0);
    mjpeg_stream_send_frame(stream, frame, 0);
    
    return true;
}

// Start streaming
camera_driver_start_stream(camera, stream_callback, &stream, 4096, 2);
```

## Tối ưu hóa

### Memory

```c
// Sử dụng PSRAM cho resolutions cao
config.fb_location = CAMERA_FB_PSRAM;
config.fb_count = 2;  // Double buffering
```

### Performance

```c
// Giảm resolution để tăng FPS
config.resolution = CAMERA_RES_QVGA;  // 320x240

// JPEG quality: lower = better quality but slower
config.jpeg_quality = 12;  // 10-15 for high quality

// GRAB_LATEST: always fresh frames (drop old)
config.grab_mode = CAMERA_GRAB_LATEST;
```

### Power

```c
// Giảm XCLK frequency để tiết kiệm điện
config.xclk_freq_hz = 10000000;  // 10MHz thay vì 20MHz
```

## Pinout cho các board phổ biến

### AI-Thinker ESP32-CAM

| Function | GPIO |
|----------|------|
| PWDN | 32 |
| RESET | - |
| XCLK | 0 |
| SIOD (SDA) | 26 |
| SIOC (SCL) | 27 |
| D7 | 35 |
| D6 | 34 |
| D5 | 39 |
| D4 | 36 |
| D3 | 21 |
| D2 | 19 |
| D1 | 18 |
| D0 | 5 |
| VSYNC | 25 |
| HREF | 23 |
| PCLK | 22 |

### ESP32-S3-EYE

| Function | GPIO |
|----------|------|
| XCLK | 15 |
| SIOD (SDA) | 4 |
| SIOC (SCL) | 5 |
| D7-D0 | 16,17,18,12,10,8,9,11 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |

## Troubleshooting

| Vấn đề | Giải pháp |
|--------|-----------|
| Camera init failed | Kiểm tra pinout, PSRAM enabled |
| Blank/black image | Check lighting, camera cable |
| Low FPS | Giảm resolution, tăng JPEG quality |
| Out of memory | Enable PSRAM, giảm fb_count |
| Image flipped | Use `set_vflip()`, `set_hmirror()` |
| Noisy image | Tăng lighting, giảm gain_ctrl |

## License

MIT License - Free to use in commercial/personal projects.

## Credits

Wrapper for ESP32-Camera library with simplified API.

## Contributing

Pull requests welcome! Please test on hardware before submitting.