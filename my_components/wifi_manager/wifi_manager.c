#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

// --- KHÔNG CẦN DEFINE CỨNG NỮA ---
// #define WIFI_SSID      "TPLINK24G"  <-- Xóa dòng này
// #define WIFI_PASS      "1234567890" <-- Xóa dòng này

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
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
        ESP_LOGI(TAG, "Waiting for WiFi... (%d/%d)", retry, CONFIG_WIFI_MAX_RETRY);
    }
    ESP_LOGE(TAG, "Failed to connect to WiFi!");
}