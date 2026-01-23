# MJPEG Stream Driver for ESP32

Driver HTTP streaming MJPEG có thể tái sử dụng cho ESP32, tối ưu cho camera streaming và real-time video applications.

## Tính năng

✅ **Plug-and-play**: Dễ dàng tích hợp vào bất kỳ project ESP32 nào  
✅ **Thread-safe**: An toàn đa luồng với FreeRTOS  
✅ **Hiệu suất cao**: Frame queue với chiến lược "drop old frames"  
✅ **Linh hoạt**: Hỗ trợ custom URI handlers  
✅ **Statistics**: Theo dõi frames sent/dropped, bandwidth, clients  
✅ **Zero-copy**: Ownership transfer để tối ưu bộ nhớ  

## Cài đặt

### ESP-IDF Component

```bash
cd your_project/components
git clone https://github.com/yourusername/mjpeg_stream.git
```

Hoặc thêm vào `components` directory của bạn.

### Cấu trúc thư mục

```
your_project/
├── my_components/
│   └── mjpeg_stream/
│       ├── include
        |   └── mjpeg_stream.h
│       ├── mjpeg_stream.c
│       └── CMakeLists.txt
└── main/
    └── your_app.c
```

## Sử dụng cơ bản

### 1. Khởi tạo stream

```c
#include "mjpeg_stream.h"

mjpeg_stream_handle_t stream;

// Sử dụng cấu hình mặc định
mjpeg_config_t config = mjpeg_stream_get_default_config();

// Hoặc tùy chỉnh
config.server_port = 8080;
config.frame_queue_len = 6;
config.stream_uri = "/camera";

// Khởi tạo
mjpeg_stream_init(&config, &stream);
mjpeg_stream_start(stream);
```

### 2. Gửi frames

```c
// Từ ESP32 Camera
camera_fb_t *fb = esp_camera_fb_get();

// Chuyển đổi sang JPEG
uint8_t *jpg_buf;
size_t jpg_len;
fmt2jpg(fb->buf, fb->len, fb->width, fb->height, 
        fb->format, 85, &jpg_buf, &jpg_len);

esp_camera_fb_return(fb);

// Tạo frame
mjpeg_frame_t *frame = mjpeg_stream_create_frame(
    jpg_buf, jpg_len, esp_timer_get_time()
);

// Gửi (non-blocking)
mjpeg_stream_send_frame(stream, frame, 0);
```

### 3. Đăng ký custom handlers

```c
esp_err_t stats_handler(httpd_req_t *req) {
    mjpeg_stats_t stats;
    mjpeg_stream_get_stats(stream, &stats);
    
    char json[128];
    snprintf(json, sizeof(json), 
             "{\"fps\":%.1f, \"clients\":%lu}",
             calculate_fps(&stats), stats.active_clients);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

mjpeg_stream_register_uri(stream, "/stats", HTTP_GET, stats_handler, NULL);
```

### 4. Cleanup

```c
mjpeg_stream_stop(stream);
mjpeg_stream_deinit(stream);
```

## API Reference

### Configuration

```c
typedef struct {
    uint16_t server_port;        // HTTP port (default: 80)
    uint8_t frame_queue_len;     // Queue depth (default: 4)
    uint32_t stack_size;         // Task stack (default: 8192)
    uint8_t task_priority;       // Priority (default: 5)
    int8_t task_core_id;         // CPU core (-1 = any)
    uint32_t max_frame_size;     // Max frame bytes (0 = unlimited)
    bool drop_old_frames;        // Drop old on full queue
    const char *stream_uri;      // Endpoint URI (default: "/")
    const char *boundary;        // Multipart boundary
} mjpeg_config_t;
```

### Core Functions

| Function | Description |
|----------|-------------|
| `mjpeg_stream_init()` | Khởi tạo stream với config |
| `mjpeg_stream_start()` | Start HTTP server |
| `mjpeg_stream_stop()` | Stop streaming |
| `mjpeg_stream_send_frame()` | Gửi JPEG frame |
| `mjpeg_stream_create_frame()` | Helper tạo frame |
| `mjpeg_stream_get_stats()` | Lấy thống kê |
| `mjpeg_stream_reset_stats()` | Reset counters |
| `mjpeg_stream_register_uri()` | Thêm custom endpoint |
| `mjpeg_stream_deinit()` | Cleanup resources |

### Statistics

```c
typedef struct {
    uint32_t frames_sent;
    uint32_t frames_dropped;
    uint32_t bytes_sent;
    uint32_t active_clients;
    int64_t last_frame_time;
} mjpeg_stats_t;
```

## Ví dụ nâng cao

### Multiple streams

```c
mjpeg_stream_handle_t stream1, stream2;

// Stream 1: High quality
mjpeg_config_t config1 = mjpeg_stream_get_default_config();
config1.server_port = 8080;
config1.stream_uri = "/hd";
mjpeg_stream_init(&config1, &stream1);
mjpeg_stream_start(stream1);

// Stream 2: Low quality
mjpeg_config_t config2 = mjpeg_stream_get_default_config();
config2.server_port = 8081;
config2.stream_uri = "/sd";
config2.max_frame_size = 30 * 1024; // 30KB limit
mjpeg_stream_init(&config2, &stream2);
mjpeg_stream_start(stream2);
```

### Custom frame source

```c
void synthetic_video_task(void *pvParameters) {
    while (true) {
        // Tạo synthetic JPEG (e.g., from graphics)
        uint8_t *jpg = generate_test_pattern();
        size_t len = get_jpg_size(jpg);
        
        mjpeg_frame_t *frame = mjpeg_stream_create_frame(
            jpg, len, esp_timer_get_time()
        );
        
        mjpeg_stream_send_frame(stream, frame, 100);
        vTaskDelay(pdMS_TO_TICKS(40)); // 25 FPS
    }
}
```

### Performance monitoring

```c
void monitor_task(void *pvParameters) {
    mjpeg_stats_t prev = {0}, curr;
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        mjpeg_stream_get_stats(stream, &curr);
        
        uint32_t fps = curr.frames_sent - prev.frames_sent;
        uint32_t bandwidth = (curr.bytes_sent - prev.bytes_sent) / 1024;
        
        ESP_LOGI(TAG, "FPS: %lu, Bandwidth: %lu KB/s, Drops: %lu",
                 fps, bandwidth, curr.frames_dropped - prev.frames_dropped);
        
        prev = curr;
    }
}
```

## Tối ưu hóa

### Memory

- Sử dụng `MALLOC_CAP_SPIRAM` cho JPEG buffers lớn
- Giảm `frame_queue_len` nếu RAM hạn chế
- Enable `drop_old_frames` để tránh queue overflow

### Performance

- Pin HTTP task lên core 0 (WiFi core)
- Pin camera task lên core 1
- Tăng `task_priority` để giảm latency
- Giảm JPEG quality nếu cần bandwidth thấp

### Network

```c
// Disable WiFi power save
esp_wifi_set_ps(WIFI_PS_NONE);

// Increase TCP buffer
esp_netif_set_tcp_tx_buffer_size(netif, 16384);
```

## Troubleshooting

| Vấn đề | Nguyên nhân | Giải pháp |
|--------|-------------|-----------|
| Lag/choppy video | Frame queue đầy | Tăng `frame_queue_len`, enable `drop_old_frames` |
| High memory usage | Queue quá lớn | Giảm `frame_queue_len` hoặc `max_frame_size` |
| Client disconnects | Network timeout | Giảm JPEG size, check WiFi signal |
| Low FPS | CPU overload | Giảm resolution, tăng task priority |

## License

MIT License - Free to use in commercial/personal projects.

## Credits

Extracted and refactored from ESP32 camera motion detection project.

## Contributing

Pull requests welcome! Please test on real hardware before submitting.