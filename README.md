# ESP32 Motion Detection System
![Status](https://img.shields.io/badge/Status-Active-success)
![Platform](https://img.shields.io/badge/ESP--IDF-v5.x-blue)
![Hardware](https://img.shields.io/badge/ESP32--S3-PSRAM-red)
![License](https://img.shields.io/badge/License-MIT-green)
---
Hệ thống giám sát thông minh trên nền tảng **ESP32**, ứng dụng **thị giác máy tính (Computer Vision)** để phát hiện chuyển động, theo dõi và quản lý đối tượng theo thời gian thực.  
Dự án hướng tới các ứng dụng **giám sát thông minh và IoT Edge Computing** với khả năng **stream video, hiển thị trực tiếp và cập nhật firmware từ xa (OTA)**.

---

## 🚀 Tính năng nổi bật

- **Phát hiện chuyển động (Motion Detection)**  
  Sử dụng thuật toán *Frame Differencing* để phát hiện sự thay đổi giữa các khung hình theo thời gian thực.

- **Theo dõi đối tượng (Object Tracking)**  
  Tự động gán **Tracking ID** (`#1`, `#2`, …) và theo dõi **tọa độ trung tâm (Centroid)** của từng vật thể.

- **Hiển thị trực tiếp trên OLED**  
  Theo dõi số lượng, ID, trạng thái và kích thước vật thể (pixel) ngay trên thiết bị.

- **Cập nhật OTA (Over-The-Air)**  
  Nạp firmware mới trực tiếp qua **Web Browser**, không cần kết nối USB.

- **MJPEG Streaming qua WiFi**  
  Xem video camera theo thời gian thực thông qua trình duyệt web.

---

## 🛠 Công nghệ sử dụng

### Phần cứng
- ESP32-S3-CAM
- Camera **OV2640**
- Màn hình **OLED SSD1306 (I2C)**

### Phần mềm
- Ngôn ngữ: **C**
- Framework: **ESP-IDF**
- RTOS: **FreeRTOS**
- Giao thức:  
  - **HTTP Server** (Streaming & OTA)  
  - **I2C** (OLED)

### Thuật toán
- **Frame Differencing** – phát hiện chuyển động
- **Flood Fill Algorithm** – gom nhóm điểm ảnh (Blob Detection)
- **Centroid Tracking** – theo dõi vị trí đối tượng

---

## 📦 Cấu trúc thư mục

```text
cadpro_motion_detection_esp32s3_cam/
├── build/                       # Thư mục build (tự sinh bởi ESP-IDF)
│
├── main/                        # Ứng dụng chính
│   ├── main.c                   # Entry point & logic điều phối hệ thống
│   └── CMakeLists.txt
│
├── managed_components/          # Component được quản lý tự động (ESP-IDF)
│   ├── espressif__esp_jpeg       # Thư viện xử lý JPEG
│   └── espressif__esp32-camera   # Driver camera chính thức của Espressif
│
├── my_components/               # Các component tự phát triển
│   ├── esp32_camera_driver/     # Wrapper / mở rộng driver camera
│   ├── http_stream/             # MJPEG Streaming qua HTTP
│   ├── oled_driver/             # Driver OLED SSD1306 (I2C)
│   ├── ota_server/              # OTA Web Server
│   └── wifi_manager/            # Quản lý WiFi (STA, reconnect, event)
│
├── partitions.csv               # Bảng phân vùng (OTA, NVS, App)
├── sdkconfig                    # Cấu hình build hiện tại
├── sdkconfig.defaults           # Cấu hình mặc định
├── sdkconfig.old                # Backup cấu hình cũ
│
├── dependencies.lock            # Lock version component (ESP-IDF)
├── CMakeLists.txt               # CMakeLists cấp project
├── .gitignore
└── README.md                    # Tài liệu mô tả dự án
```

## ⚙️ Cấu hình & tinh chỉnh thuật toán
Các tham số quan trọng nằm trong file "main.c".
Cần tinh chỉnh tùy theo môi trường thực tế:
| Tham Số | Giá trị (Default) | Ý nghĩa & Hướng dẫn Tinh chỉnh |
| :--- | :---: | :--- |
| **`BLOCK_SIZE`** | `8` | **Độ phân giải lưới (Grid Size).**<br>Kích thước ô vuông (pixel) dùng để quét ảnh.<br>🔼 **Tăng lên (16):** Xử lý nhanh hơn, giảm tải CPU.<br>🔽 **Giảm xuống (4):** Tăng độ chính xác cho đối tượng nhỏ (tốn RAM). |
| **`MOTION_THRESHOLD`** | `5` | **Độ nhạy sáng (Sensitivity).**<br>Ngưỡng chênh lệch màu sắc tối thiểu để tính là chuyển động.<br>🔼 **Tăng lên (10-20):** Nếu camera bị nhiễu hạt (noise) hoặc báo động giả.<br>🔽 **Giảm xuống (2-3):** Để bắt chuyển động rất nhẹ (nhưng dễ nhiễu). |
| **`ALERT_THRESHOLD`** | `10` | **Ngưỡng diện tích kích hoạt.**<br>Cần ít nhất 10 ô (blocks) thay đổi thì mới xác nhận có đối tượng.<br>🔼 **Tăng lên:** Để lọc bỏ lá cây rung, mưa rơi.<br>🔽 **Giảm xuống:** Để bắt đối tượng kích thước nhỏ. |
| **`CONSECUTIVE_FRAMES`** | `2` | **Bộ lọc nhiễu nhất thời.**<br>Số khung hình liên tiếp phải có chuyển động thì mới kích hoạt.<br>👉 Giúp loại bỏ hiện tượng nháy đèn flash hoặc nhiễu điện (glitch). |
| **`MIN_OBJECT_SIZE`** | `200` | **Lọc kích thước tối thiểu (pixel).**<br>Đối tượng có diện tích < 200px sẽ bị bỏ qua.<br>👉 Tăng lên nếu muốn bỏ qua vật nhỏ, chỉ bắt đối tượng lớn. |
| **`MAX_OBJECT_SIZE`** | `40000` | **Lọc kích thước tối đa (pixel).**<br>Đối tượng quá lớn (do rung lắc camera khiến cả khung hình bị lệch) sẽ bị bỏ qua. |
| **`REGION_MERGE_THRESHOLD`** | `3` | **Khoảng cách gộp (Merge Distance).**<br>Khoảng cách tối đa (block) để ghép 2 mảnh vỡ thành 1 đối tượng.<br>⚠️ **Quan trọng:** Nếu 2 đối tượng đi gần nhau bị dính thành 1 ID 👉 **Giảm xuống `0` hoặc `1`.** |
| **`DISTANCE_THRESHOLD`** | `70` | **Tốc độ theo dõi (Tracking Speed).**<br>Khoảng cách tối đa (pixel) đối tượng có thể di chuyển giữa 2 frame mà vẫn giữ ID cũ.<br>🔼 **Tăng lên:** Nếu đối tượng di chuyển nhanh.<br>🔽 **Giảm xuống:** Nếu đối tượng đi chậm, tránh bắt nhầm ID. |
| **`OBJECT_TIMEOUT`** | `6` | **Thời gian chờ (Lost Frames).**<br>Số frame hệ thống chờ đợi khi đối tượng bị khuất tạm thời trước khi xóa ID. |
| **`STATIONARY_TIMEOUT`** | `300` | **Thời gian xóa đối tượng đứng yên.**<br>Sau 300 frame (khoảng 10-20s) đứng yên, đối tượng sẽ bị xóa khỏi màn hình theo dõi. |
| **`STATIONARY_THRESHOLD`** | `10` | **Phạm vi rung lắc.**<br>Nếu đối tượng di chuyển trong phạm vi < 10px (VD: ngồi thở nhẹ, lắc lư), hệ thống vẫn coi là Đứng yên `[S]`. |

---

### 💡 Khắc Phục Sự Cố Nhanh (Quick Troubleshooting)

* **Vấn đề 1: Hai đối tượng di chuyển gần nhau bị gộp thành 1 khung bao?**
    * 👉 **Sửa:** Giảm `REGION_MERGE_THRESHOLD` xuống `0`.

* **Vấn đề 2: Báo động giả liên tục dù không có đối tượng?**
    * 👉 **Sửa:** Tăng `MOTION_THRESHOLD` lên `10` hoặc `15`.

* **Vấn đề 3: Đối tượng di chuyển nhanh bị mất dấu (ID nhảy liên tục)?**
    * 👉 **Sửa:** Tăng `DISTANCE_THRESHOLD` lên `100`.

* **Vấn đề 4: Không bắt được đối tượng ở xa (kích thước nhỏ)?**
    * 👉 **Sửa:** Giảm `MIN_OBJECT_SIZE` xuống `100`.


## ⚙️ Cách build & chạy
### 1. Kết nối phần cứng
* Camera: Gắn chặt vào socket trên ESP32-S3 (lưu ý chiều dây cáp).
* OLED: Kết nối I2C (SDA/SCL).
### 2. Cài đặt môi trường
* Đảm bảo đã cài đặt ESP-IDF (VS Code Extension hoặc Command Line).
* Clone dự án về máy tính.
### 3. Cấu hình WiFi & Project
Trước khi nạp code,  cần cài đặt tên WiFi và mật khẩu để thiết bị có thể kết nối mạng.
* Mở terminal tại thư mục dự án và chạy lệnh:
```bash
idf.py menuconfig
```
* Trong giao diện cấu hình (màn hình xanh), truy cập theo đường dẫn:
Component config ➡️ WiFi Manager Configuration.
- Nhập WiFi SSID (Tên mạng).
- Nhập WiFi Password (Mật khẩu).
- Nhấn S để lưu lại (Save) và Esc để thoát.

Nếu dùng ESP32-S3, set target cho ESP-IDF
```bash
idf.py set-target esp32s3
```
### 4. Build và Nạp Code
Sử dụng lệnh sau để biên dịch
```bash
idf.py build
```
Sử dụng lệnh sau để nạp và xem log (Thay COMxx bằng cổng của bạn):
```bash
idf.py -p COMxx flash monitor
```
