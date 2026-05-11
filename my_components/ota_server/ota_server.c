#include "ota_server.h"
#include <stdlib.h> // Dùng malloc/free
#include <string.h>
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "sys/param.h"
#include "esp_timer.h"
#include "esp_netif.h"  

static const char *TAG = "OTA_SERVER";

// HTML + JavaScript: Gửi file dạng Raw Binary (XHR)
static const char *ota_index_html = 
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>ESP32 Firmware Update</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; max-width: 400px; margin: 50px auto; padding: 20px; text-align: center; }"
    "input { margin-bottom: 20px; }"
    "#progress-container { width: 100%; background-color: #ddd; height: 25px; border-radius: 5px; display: none; }"
    "#progress-bar { width: 0%; height: 100%; background-color: #4CAF50; border-radius: 5px; transition: width 0.2s; }"
    "button { padding: 10px 20px; font-size: 16px; cursor: pointer; background: #007bff; color: white; border: none; border-radius: 5px; }"
    "button:disabled { background: #ccc; }"
    "</style></head>"
    "<body>"
    "<h2>Firmware Update</h2>"
    "<input type='file' id='file_input' accept='.bin'>"
    "<br>"
    "<div id='progress-container'><div id='progress-bar'></div></div>"
    "<p id='status'></p>"
    "<button onclick='uploadFirmware()' id='btn-upload'>Start Update</button>"

    "<script>"
    "function uploadFirmware() {"
    "   var fileInput = document.getElementById('file_input');"
    "   var file = fileInput.files[0];"
    "   if (!file) { alert('Please select a .bin file!'); return; }"
    
    "   var xhr = new XMLHttpRequest();"
    "   xhr.open('POST', '/update', true);"
    
    "   document.getElementById('progress-container').style.display = 'block';"
    "   document.getElementById('btn-upload').disabled = true;"
    
    "   xhr.upload.onprogress = function(e) {"
    "       if (e.lengthComputable) {"
    "           var percent = (e.loaded / e.total) * 100;"
    "           document.getElementById('progress-bar').style.width = percent + '%';"
    "           document.getElementById('status').innerText = 'Uploading: ' + Math.round(percent) + '%';"
    "       }"
    "   };"
    
    "   xhr.onload = function() {"
    "       if (xhr.status === 200) {"
    "           document.getElementById('status').innerText = 'Success! Rebooting...';"
    "           document.getElementById('progress-bar').style.backgroundColor = '#4CAF50';"
    "       } else {"
    "           document.getElementById('status').innerText = 'Error: ' + xhr.responseText;"
    "           document.getElementById('progress-bar').style.backgroundColor = '#f44336';"
    "           document.getElementById('btn-upload').disabled = false;"
    "       }"
    "   };"
    
    "   xhr.onerror = function() {"
    "       document.getElementById('status').innerText = 'Network Error';"
    "       document.getElementById('btn-upload').disabled = false;"
    "   };"
    
    "   xhr.send(file);"
    "}"
    "</script>"
    "</body></html>";

// Handler 1: GET /ota
static esp_err_t ota_index_handler(httpd_req_t *req) {
    httpd_resp_send(req, ota_index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler 2: POST /update
static esp_err_t ota_update_handler(httpd_req_t *req) {
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    char *buf = NULL;
    const int buf_size = 4096;
    int received;
    int remaining = req->content_len;
    esp_err_t err;

    ESP_LOGI(TAG, "Starting OTA update, content_len: %d", remaining);

    // 1. Allocate temporary receive buffer
    buf = (char *)malloc(buf_size);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "OOM", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // 2. Inspect current partition and select the next update partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running ? running->label : "null");

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGW(TAG, "esp_ota_get_next_update_partition returned NULL, thử fallback");
        if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
            update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
        } else {
            update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        }
    }

    if (!update_partition) {
        ESP_LOGE(TAG, "Passive partition still not found; check partition table");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Partition Error", HTTPD_RESP_USE_STRLEN);
        free(buf);
        return ESP_FAIL;
    }

    const esp_partition_t *boot = esp_ota_get_boot_partition();
    ESP_LOGI(TAG, "Boot partition %s", boot ? boot->label : "null");

    // 3. Bắt đầu OTA
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        free(buf);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "OTA Begin Failed", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             update_partition->subtype, update_partition->address);

    // 4. Vòng lặp nhận và ghi dữ liệu
    while (remaining > 0) {
        received = httpd_req_recv(req, buf, MIN(remaining, buf_size));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Socket receive failed");
            esp_ota_end(update_handle);
            free(buf);
            return ESP_FAIL;
        }

        err = esp_ota_write(update_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
            esp_ota_end(update_handle);
            free(buf);
            return ESP_FAIL;
        }
        remaining -= received;
    }

    free(buf);

    // 5. Kết thúc và Kiểm tra toàn vẹn
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "OTA Validation Failed", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // 6. Set Boot Partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Set Boot Failed", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA Success! Rebooting...");
    httpd_resp_sendstr(req, "OTA Success! Rebooting...");
    
    // Flush stdout before restarting to ensure all log output is transmitted
    fflush(stdout);
    
    // 7. Restart after a short delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

void start_ota_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81; 
    // --- Đổi cổng điều khiển sang số khác 80 (mặc định) ---
    config.ctrl_port = 32769; 
    config.stack_size = 8192;
    config.lru_purge_enable = true; 

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/ota",
            .method    = HTTP_GET,
            .handler   = ota_index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t update_uri = {
            .uri       = "/update",
            .method    = HTTP_POST,
            .handler   = ota_update_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &update_uri);
        
        ESP_LOGI(TAG, "OTA Server started at port 81");
    }
    
    // --- ĐOẠN CODE MỚI: LẤY IP VÀ IN LINK ---
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    
    if (netif) {
        esp_netif_get_ip_info(netif, &ip_info);
        // IPSTR and IP2STR are ESP macros for formatting IP addresses
        ESP_LOGI(TAG, "-------------------------------------------------------------");
        ESP_LOGI(TAG, "🚀 OTA SERVER READY!");
        ESP_LOGI(TAG, "👉 Click to open: http://" IPSTR ":81/ota", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "-------------------------------------------------------------");
    } else {
        ESP_LOGW(TAG, "OTA Server started on port 81 (Wait for WiFi IP...)");
    }
}