WiFi Manager Component (ESP-IDF)
📌 Giới thiệu

wifi_manager là một component quản lý WiFi cho ESP32 (ESP-IDF), được thiết kế theo hướng:

Dễ dùng

Tách biệt logic WiFi khỏi app_main

Phù hợp cho project IoT / Camera / HTTP Stream / AIoT

Dùng FreeRTOS EventGroup để đồng bộ trạng thái

Component này không phụ thuộc UI, không tự reconnect phức tạp, phù hợp cho embedded project thực tế.

📁 Cấu trúc thư mục
my_components/
└── wifi_manager/
    ├── include/
    │   └── wifi_manager.h
    ├── wifi_manager.c
    ├── CMakeLists.txt
    └── README.md   ← file này

⚙️ Kiến trúc tổng thể
app_main
   │
   ├── wifi_manager_init()
   │
   ├── wifi_manager_connect()
   │
   └── xEventGroupWaitBits()
           │
           └── WIFI CONNECTED

Trạng thái WiFi
typedef enum {
    WIFI_MGR_DISCONNECTED = 0,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED
} wifi_mgr_state_t;

📌 API chính
1️⃣ Khởi tạo WiFi
void wifi_manager_init(wifi_mgr_mode_t mode);


Khởi tạo:

NVS

TCP/IP stack

Event loop

WiFi driver

Hiện tại hỗ trợ:

WIFI_MGR_MODE_STA

2️⃣ Kết nối WiFi
void wifi_manager_connect(const char *ssid, const char *password);


Set SSID / Password

Gọi esp_wifi_connect()

Chuyển trạng thái sang WIFI_MGR_CONNECTING

3️⃣ Ngắt kết nối
void wifi_manager_disconnect(void);

4️⃣ Kiểm tra trạng thái
bool wifi_manager_is_connected(void);
wifi_mgr_state_t wifi_manager_get_state(void);

5️⃣ EventGroup (quan trọng)
EventGroupHandle_t wifi_manager_get_event_group(void);


Các bit sự kiện:

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

🧪 Ví dụ sử dụng cơ bản
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_SSID     "TPLINK24G"
#define WIFI_PASSWORD "1234567890"

void app_main(void)
{
    wifi_manager_init(WIFI_MGR_MODE_STA);
    wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);

    EventBits_t bits = xEventGroupWaitBits(
        wifi_manager_get_event_group(),
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI("MAIN", "WiFi connected, continue app...");
    }
}

🔄 Cơ chế reconnect

Khi WIFI_EVENT_STA_DISCONNECTED

Tự động retry tối đa MAX_RETRY

Sau khi vượt quá số lần retry:

Set WIFI_FAIL_BIT

Chuyển state về DISCONNECTED

static const int MAX_RETRY = 5;

📡 Event xử lý

Component lắng nghe:

WIFI_EVENT

IP_EVENT_STA_GOT_IP

Khi có IP:

Reset retry counter

Set WIFI_CONNECTED_BIT

State → WIFI_MGR_CONNECTED

🧠 Thiết kế & lý do
Vì sao dùng EventGroup?

Nhẹ

Chuẩn FreeRTOS

Phù hợp đồng bộ nhiều task (HTTP, Camera, MQTT…)

Vì sao không dùng task riêng?

Tránh phức tạp

Tránh race condition

ESP-IDF WiFi đã chạy nền sẵn

⚠️ Lưu ý quan trọng

wifi_manager_init() chỉ nên gọi 1 lần

Chỉ gọi wifi_manager_connect() sau khi init

Không gọi esp_wifi_* trực tiếp ở app_main

📦 Phụ thuộc

Component sử dụng các component chuẩn của ESP-IDF:

esp_wifi

esp_event

esp_netif

nvs_flash

freertos

Không cần thêm dependency ngoài.

🚀 Gợi ý mở rộng

Thêm:

AP mode

AP+STA

Scan WiFi

Save credentials vào NVS

Tích hợp với:

HTTP server

Camera stream

MQTT