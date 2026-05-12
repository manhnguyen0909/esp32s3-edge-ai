# ESP32 Egde AI System
![Status](https://img.shields.io/badge/Status-Active-success)
![Platform](https://img.shields.io/badge/ESP--IDF-v5.x-blue)
![Hardware](https://img.shields.io/badge/ESP32--S3-PSRAM-red)
![License](https://img.shields.io/badge/License-MIT-green)
---
Hệ thống giám sát thông minh dựa trên **ESP32-S3-CAM**, kết hợp **phát hiện chuyển động**, **theo dõi đối tượng**, **phát hiện người bằng AI**, **stream video MJPEG** và **OTA firmware**.

Dự án phù hợp cho các ứng dụng **giám sát tại biên (IoT Edge)**, cho phép thu thập hình ảnh, phân tích chuyển động/đối tượng trên thiết bị, đồng thời vẫn hỗ trợ **nâng cấp firmware từ xa**.

---

## 🚀 Tính năng chính

- **Phát hiện chuyển động (Motion Detection)**
  - Dùng phương pháp *Frame Differencing* để tìm vùng thay đổi giữa các khung hình.
  - Kết hợp **Flood Fill** để gom nhóm vùng di chuyển thành đối tượng.

- **Theo dõi đối tượng (Object Tracking)**
  - Gán ID cho từng đối tượng và theo dõi vị trí trung tâm (centroid).
  - Hỗ trợ giữ ID khi đối tượng di chuyển liên tục.

- **Phát hiện người bằng AI**
  - Khởi tạo mô-đun AI trong `main.cpp`.
  - Hàm `ai_run(...)` trả về `1` nếu phát hiện người, `0` nếu không.

- **MJPEG Streaming qua HTTP**
  - Web server stream ảnh camera theo thời gian thực.
  - Khởi tạo bằng `start_http_server(frame_queue, motion_mutex, &obj_db)`.

- **OTA firmware cập nhật trực tiếp**
  - Khởi động OTA server trên cổng `81`.
  - Cập nhật firmware qua trình duyệt web mà không cần cắm USB.

- **OLED SSD1306 hiển thị trạng thái**
  - Hiển thị trạng thái khởi động, WiFi, OTA và thông tin giám sát.

- **WiFi STA với quản lý kết nối**
  - Dùng component `wifi_manager` để kết nối mạng và tự reconnect khi mất mạng.

---

## 🛠 Công nghệ sử dụng

### Phần cứng
- ESP32-S3-CAM
- Camera **OV2640**
- Màn hình **OLED SSD1306 (I2C)**

### Phần mềm
- Ngôn ngữ: **C / C++**
- Framework: **ESP-IDF v5.x**
- RTOS: **FreeRTOS**
- Thư viện JPEG: **espressif__esp_jpeg**
- Driver camera: **espressif__esp32-camera**

### Giao tiếp và dịch vụ
- **HTTP Server** cho MJPEG streaming và OTA
- **I2C** cho OLED
- **WiFi STA** cho kết nối mạng

---

## 📁 Cấu trúc thư mục

```text
cadpro_motion_detection_esp32s3_cam_project/
├── build/                       # Thư mục build được sinh bởi ESP-IDF
├── main/                        # Mã nguồn chính và logic ứng dụng
│   ├── main.cpp                 # Entry point và điều phối các component
│   ├── image_utils.c/.h         # Xử lý ảnh và phân tích chuyển động
│   ├── JPEGDEC.*                # Giải mã JPEG
│   ├── person_ai/               # Mô-đun AI phát hiện người
│   └── idf_component.yml
├── managed_components/          # Component ESP-IDF tự động quản lý
├── my_components/               # Component tự phát triển
│   ├── esp32_camera_driver/     # Khởi tạo camera và cấu hình
│   ├── http_stream/             # HTTP MJPEG streaming
│   ├── oled_driver/             # Điều khiển màn hình OLED SSD1306
│   ├── ota_server/              # OTA Web Server
│   └── wifi_manager/            # Quản lý kết nối WiFi STA
├── partitions.csv               # Bảng phân vùng OTA/NVS/App
├── sdkconfig                    # Cấu hình build hiện tại
├── sdkconfig.defaults           # Cấu hình mặc định
├── sdkconfig.old                # Backup cấu hình cũ
├── dependencies.lock            # Lock version component ESP-IDF
├── CMakeLists.txt               # CMakeLists cấp project
├── .gitignore
└── README.md                    # Tài liệu dự án
```

---

## 🔧 Cấu hình chính trong `main.cpp`
Các tham số chính có thể điều chỉnh theo môi trường thực tế:

| Tham số | Giá trị mặc định | Ý nghĩa |
| :--- | :---: | :--- |
| `BLOCK_SIZE` | `8` | Kích thước mỗi block ảnh dùng để so sánh chuyển động. |
| `MOTION_THRESHOLD` | `15` | Ngưỡng khác biệt pixel để coi là chuyển động. |
| `ALERT_THRESHOLD` | `10` | Số block tối thiểu để xác định một vùng chuyển động. |
| `CONSECUTIVE_FRAMES` | `4` | Số khung hình liên tiếp phải có chuyển động để báo động. |
| `REGION_MERGE_THRESHOLD` | `3` | Khoảng cách tối đa để gộp các vùng chuyển động gần nhau. |
| `MIN_OBJECT_SIZE` | `200` | Kích thước nhỏ nhất của đối tượng (pixel). |
| `MAX_OBJECT_SIZE` | `40000` | Kích thước lớn nhất của đối tượng (pixel). |
| `DISTANCE_THRESHOLD` | `70` | Khoảng cách di chuyển tối đa giữa 2 frame để giữ ID. |
| `OBJECT_TIMEOUT` | `6` | Số frame chờ đối tượng bị mất tạm thời trước khi xóa ID. |
| `STATIONARY_TIMEOUT` | `10` | Số frame để xác định đối tượng đứng yên. |
| `STATIONARY_THRESHOLD` | `10` | Ngưỡng dao động nhỏ để giữ trạng thái đứng yên. |

---

## 🧠 Hoạt động chính của hệ thống

1. Khởi tạo AI, màn hình OLED, NVS và camera.
2. Kết nối WiFi bằng module `wifi_manager`.
3. Khởi động OTA server và HTTP stream server.
4. Tạo task phát hiện chuyển động và cập nhật OLED.
5. Nếu phát hiện chuyển động/đối tượng, hệ thống sẽ phân tích, gán ID và giữ trạng thái.

---

## ⚙️ Hướng dẫn build & chạy

### 1. Kết nối phần cứng
- Camera: Gắn chặt vào socket camera trên ESP32-S3.
- OLED: Kết nối qua I2C (SDA/SCL) và cấp nguồn đúng.

### 2. Cài đặt môi trường
- Cài ESP-IDF v5.x và thiết lập biến môi trường.
- Mở terminal trong thư mục dự án.

### 3. Cấu hình WiFi
```bash
idf.py menuconfig
```
- Vào `Component config` ➜ `WiFi Manager Configuration`.
- Cấu hình `ESP_WIFI_SSID` và `ESP_WIFI_PASS`.
- Lưu và thoát.

### 4. Build và flash
```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMxx flash monitor
```

---

## 🛠 Lưu ý
- Nếu dùng ESP32-S3, đảm bảo target đã được chọn là `esp32s3`.
- Đảm bảo camera OV2640 hoạt động và kết nối đúng.
- Nếu không thấy WiFi, kiểm tra cấu hình SSID/Password trong `menuconfig`.
- OTA server chạy trên cổng `81` và yêu cầu thiết bị đã kết nối WiFi.

