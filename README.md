# ESP32-S3 Advanced Motion Detection & Object Tracking

![Status](https://img.shields.io/badge/Status-Active-success)
![Platform](https://img.shields.io/badge/ESP--IDF-v5.x-blue)
![Hardware](https://img.shields.io/badge/ESP32--S3-PSRAM-red)
![License](https://img.shields.io/badge/License-MIT-green)

Hệ thống Thị giác Máy tính Nhúng (Embedded Vision) hiệu năng cao chạy hoàn toàn trên nền tảng **ESP32-S3**. Dự án cung cấp khả năng theo dõi đa đối tượng thời gian thực, phát hiện hành vi đứng yên/di chuyển và truyền hình ảnh MJPEG qua WiFi với độ trễ cực thấp (< 200ms) mà không cần phụ thuộc vào Cloud hay AI Accelerator.

## 🚀 Tính Năng Nổi Bật

* **Hiệu Năng Cốt Lõi:**
    * Tối ưu hóa đặc biệt cho ESP32-S3 có PSRAM.
    * Sử dụng thuật toán xử lý ảnh dựa trên dịch bit (bit-shifting) giúp giảm tải CPU.
    * Quản lý bộ nhớ Heap/PSRAM an toàn, chống phân mảnh.

* **Theo Dõi Thông Minh (Intelligent Tracking):**
    * **Đa Đối Tượng:** Định danh và theo dõi đồng thời lên đến 10 đối tượng.
    * **Phân Tích Hành Vi:** Phân biệt rõ ràng giữa đối tượng "Đang di chuyển" và "Đang đứng yên" (hữu ích cho cảnh báo xâm nhập hoặc vật thể bị bỏ quên).
    * **Thuật Toán:** Sử dụng Phân tích Thành phần Liên thông (CCA) với thuật toán Loang màu (Flood Fill) không đệ quy và ghép nối đối tượng (Matching) theo khoảng cách.

* **Hệ Thống Streaming:**
    * Truyền hình ảnh MJPEG mượt mà.
    * Cơ chế hàng đợi **"Luôn Tươi Mới" (Always-Fresh Queue)**: Tự động hủy frame cũ khi mạng lag để đảm bảo hình ảnh luôn là thời gian thực (Real-time).

* **Kiến Trúc Module:**
    * Tách biệt Driver, WiFi, HTTP Server và Logic xử lý thành các Components độc lập.
    * Hỗ trợ cấu hình WiFi qua **Kconfig** (Menuconfig).

## 🛠 Yêu Cầu Phần Cứng

| Thành phần | Thông số kỹ thuật | Ghi chú |
| :--- | :--- | :--- |
| **MCU** | ESP32-S3 (WROOM-1/1U) | Bắt buộc phải có **PSRAM** (2MB+) & Flash (4MB+) |
| **Camera** | OV2640 / OV5640 | Module Camera chuẩn DVP 24 chân |
| **Nguồn** | 5V / 2A | Nguồn điện ổn định là yếu tố then chốt để WiFi hoạt động mượt mà |

## 📂 Cấu Trúc Dự Án

Dự án tuân theo kiến trúc hướng Component của ESP-IDF:

```text
├── components/
│   ├── esp32_camera_driver/  # Driver phần cứng cho OV2640 & Cấu hình chân S3
│   ├── http_stream/          # Web Server đa luồng (MJPEG + JSON Metadata)
│   └── wifi_manager/         # Quản lý kết nối mạng (Hỗ trợ Kconfig)
├── main/
│   └── main.c                # Logic chính: Thuật toán Motion, Tracking & Scheduler
├── CMakeLists.txt            # Cấu hình Build hệ thống
└── README.md                 # Tài liệu dự án
# ⚙️ Cấu Hình & Tinh Chỉnh

## 1. Cấu hình WiFi (Khuyên dùng)
Không cần sửa code. Sử dụng công cụ cấu hình của IDF:

1. Chạy lệnh: `idf.py menuconfig`
2. Tìm menu: **WiFi Configuration**
3. Nhập SSID và Password.
4. Lưu lại (S) và thoát (Q).

## 2. Tinh chỉnh Thuật toán (Trong `main/main.c`)

| Macro | Mặc định | Mô tả |
|-------|----------|-------|
| `BLOCK_SIZE` | 8 | Kích thước khối xử lý (8x8 pixel). Tăng lên để chạy nhanh hơn, giảm xuống để chi tiết hơn. |
| `MOTION_THRESHOLD` | 30 | Độ nhạy với thay đổi màu sắc (0-255). Giá trị cao = Lọc nhiễu tốt hơn nhưng kém nhạy. |
| `MIN_OBJECT_SIZE` | 200 | Diện tích tối thiểu (pixel) để được coi là vật thể. |
| `STATIONARY_TIMEOUT` | 300 | Số frame (khoảng 10-12s) giữ ID của vật thể đứng yên trước khi xóa khỏi bộ nhớ. |
| `DISTANCE_THRESHOLD` | 90 | Khoảng cách tối đa (pixel) để ghép nối đối tượng giữa 2 frame liên tiếp. |

# 🚀 Hướng Dẫn Cài Đặt

## Bước 1: Chuẩn bị môi trường
- Cài đặt ESP-IDF v5.0 trở lên.
- VS Code với Extension Espressif IDF.

## Bước 2: Build & Nạp Code
```bash
### 1. Thiết lập target là chip ESP32-S3
idf.py set-target esp32s3

### 2. Cấu hình WiFi (Quan trọng)
idf.py menuconfig

### 3. Biên dịch dự án
idf.py build

### 4. Nạp code và mở Monitor (Thay COMx bằng cổng của bạn)
idf.py -p COMx flash monitor
#📡 Tài Liệu API
Sau khi khởi động thành công, địa chỉ IP của thiết bị sẽ hiện trên Terminal (ví dụ: 192.168.1.105).

##1. Video Stream (MJPEG)
URL: http://<IP_ADDRESS>/

Phương thức: GET

Mô tả: Luồng video trực tiếp có vẽ sẵn bounding box và ID đối tượng.

##2. Metadata Đối Tượng (JSON)
URL: http://<IP_ADDRESS>/metadata

Phương thức: GET

Mô tả: Dữ liệu thô dùng cho IoT Dashboard hoặc xử lý AI ở tầng trên.

Ví dụ phản hồi:

json
{
  "object_count": 2,
  "objects": [
    {
      "id": 42,
      "x": 100, "y": 50,
      "w": 60, "h": 120,
      "age": 250
    },
    {
      "id": 43,
      "x": 10, "y": 200,
      "w": 30, "h": 30,
      "age": 12
    }
  ]
}
#⚠️ Khắc Phục Sự Cố (Troubleshooting)
##Video bị Lag/Giật:
Kiểm tra ăng-ten WiFi.

Đảm bảo wifi_manager đã tắt chế độ tiết kiệm điện (esp_wifi_set_ps(WIFI_PS_NONE)).

Giảm chất lượng ảnh trong esp32_camera_driver.c (jpeg_quality tăng lên 50-60).

##Lỗi Camera Init Failed:
Kiểm tra dây cáp camera.

##Kiểm tra lại định nghĩa chân (Pinout) trong components/esp32_camera_driver/esp32_camera_driver.c xem có khớp với bo mạch của bạn không (Freenove vs AI-Thinker).

##Tràn bộ nhớ (Out of Memory):
Đảm bảo chip là ESP32-S3 có PSRAM và đã bật PSRAM trong idf.py menuconfig > Component config > ESP32S3-specific.