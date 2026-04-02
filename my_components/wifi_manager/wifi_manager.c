#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "oled_ssd1306.h"


static const char *TAG = "WIFI_MGR";

void wifi_manager_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            // SỬ DỤNG MACRO TỪ KCONFIG (Thêm tiền tố CONFIG_)
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_connect();

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", CONFIG_WIFI_SSID);
    int retry = 0;
    // Sử dụng biến cấu hình số lần thử lại từ Menuconfig luôn
    while (retry < CONFIG_WIFI_MAX_RETRY) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_get_ip_info(netif, &ip_info);
            if (ip_info.ip.addr != 0) {
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip_info.ip));
                ESP_LOGI(TAG, "WEB STREAMING URL: http://" IPSTR ":80/stream",IP2STR(&ip_info.ip));
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
        ESP_LOGI(TAG, "Waiting for WiFi... (%d/%d)", retry, CONFIG_WIFI_MAX_RETRY);
    }
    ESP_LOGE(TAG, "Failed to connect to WiFi!");
}




//=======================CHẾ ĐỘ PHÁT WIFI=============================
// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_system.h"
// #include "esp_wifi.h"
// #include "esp_event.h"
// #include "esp_log.h"
// #include "nvs_flash.h"
// #include "esp_mac.h"

// // --- SỬA LẠI ĐOẠN NAY ---
// #include "esp_netif.h"      // Thư viện mạng ESP
// #include "lwip/ip_addr.h"   // Thư viện chứa macro IP4_ADDR
// #include "lwip/err.h"
// #include "lwip/sys.h"
// // --- CẤU HÌNH WIFI AP ---
// #define ESP_WIFI_SSID      "ESP32-CAM-MOTION"
// #define ESP_WIFI_PASS      "12345678" // Mật khẩu ít nhất 8 ký tự
// #define ESP_WIFI_CHANNEL   1
// #define MAX_STA_CONN       4

// static const char *TAG = "WIFI_AP";

// static void wifi_event_handler(void* arg, esp_event_base_t event_base,
//                                     int32_t event_id, void* event_data)
// {
//     if (event_id == WIFI_EVENT_AP_STACONNECTED) {
//         wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
//         ESP_LOGI(TAG, "Device connected: "MACSTR", AID=%d",
//                  MAC2STR(event->mac), event->aid);
//     } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
//         wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
//         ESP_LOGI(TAG, "Device disconnected: "MACSTR", AID=%d",
//                  MAC2STR(event->mac), event->aid);
//     }
// }

// void wifi_init_softap(void)
// {
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
    
//     // 1. Tạo Netif AP (Lưu lại handle để dùng cài IP)
//     esp_netif_t *p_netif = esp_netif_create_default_wifi_ap(); 

//     // 2. CẤU HÌNH IP TĨNH (192.168.4.1)
//     // Phải stop DHCP server trước khi set IP tĩnh
//     ESP_ERROR_CHECK(esp_netif_dhcps_stop(p_netif));

//     esp_netif_ip_info_t ipInfo;
//     IP4_ADDR(&ipInfo.ip, 192, 168, 4, 1);
//     IP4_ADDR(&ipInfo.gw, 192, 168, 4, 1);
//     IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0);
    
//     ESP_ERROR_CHECK(esp_netif_set_ip_info(p_netif, &ipInfo));
//     ESP_ERROR_CHECK(esp_netif_dhcps_start(p_netif)); // Bật lại DHCP để cấp IP cho điện thoại

//     // 3. Init Wifi
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
//                                                         ESP_EVENT_ANY_ID,
//                                                         &wifi_event_handler,
//                                                         NULL,
//                                                         NULL));

//     // 4. Config Wifi (Dùng Macro đã define ở trên)
//     wifi_config_t wifi_config = {
//         .ap = {
//             .ssid = ESP_WIFI_SSID,
//             .ssid_len = strlen(ESP_WIFI_SSID),
//             .channel = ESP_WIFI_CHANNEL,
//             .password = ESP_WIFI_PASS,
//             .max_connection = MAX_STA_CONN,
//             .authmode = WIFI_AUTH_WPA_WPA2_PSK,
//             .pmf_cfg = {
//                 .required = false,
//             },
//         },
//     };
    
//     if (strlen(ESP_WIFI_PASS) == 0) {
//         wifi_config.ap.authmode = WIFI_AUTH_OPEN;
//     }

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     // 5. QUAN TRỌNG: Tắt tiết kiệm điện để Stream mượt
//     esp_wifi_set_ps(WIFI_PS_NONE);

//     ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s IP: 192.168.4.1",
//              ESP_WIFI_SSID, ESP_WIFI_PASS);
// }