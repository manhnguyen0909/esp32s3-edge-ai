#include "http_stream.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "HTTP_STREAM";

// Con trỏ tới tài nguyên dùng chung
static QueueHandle_t s_frame_queue = NULL;
static SemaphoreHandle_t s_motion_mutex = NULL;
static object_database_t *s_obj_db = NULL;

// --- PHẦN MỚI: Xử lý Preflight Request (OPTIONS) ---
static esp_err_t cors_options_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Cache-Control, *");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}
// ---------------------------------------------------

// Handler trả về JSON Metadata
static esp_err_t metadata_handler(httpd_req_t *req) {
    if (!s_obj_db || !s_motion_mutex) return ESP_FAIL;

    // CORS Header cho GET
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    
    char json_buf[1024]; 
    int len = 0;
    
    if (xSemaphoreTake(s_motion_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        len = snprintf(json_buf, sizeof(json_buf), "{\"object_count\":%d,\"objects\":[", s_obj_db->count);
        
        for (int i = 0; i < s_obj_db->count; i++) {
            if (i > 0) len += snprintf(&json_buf[len], sizeof(json_buf) - len, ",");
            if (sizeof(json_buf) - len < 100) break; 

            len += snprintf(&json_buf[len], sizeof(json_buf) - len,
                "{\"id\":%d,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"age\":%d, \"is_person\":%s}",
                s_obj_db->objects[i].id,
                s_obj_db->objects[i].min_x,
                s_obj_db->objects[i].min_y,
                s_obj_db->objects[i].max_x - s_obj_db->objects[i].min_x,
                s_obj_db->objects[i].max_y - s_obj_db->objects[i].min_y,
                s_obj_db->objects[i].age
                , s_obj_db->objects[i].is_person ? "true" : "false"
            );
        }
        xSemaphoreGive(s_motion_mutex);
    } else {
        len = snprintf(json_buf, sizeof(json_buf), "{\"object_count\":0,\"objects\":[");
    }
    
    len += snprintf(&json_buf[len], sizeof(json_buf) - len, "]}");
    return httpd_resp_send(req, json_buf, len);
}

// Handler trả về 1 ảnh JPEG (Snapshot)
static esp_err_t stream_handler(httpd_req_t *req) {
    // CORS Header cho GET
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    jpeg_frame_t *frame = NULL;
    if (s_frame_queue && xQueueReceive(s_frame_queue, &frame, pdMS_TO_TICKS(1000)) == pdTRUE) {
        httpd_resp_set_type(req, "image/jpeg");
        esp_err_t res = httpd_resp_send(req, (const char *)frame->jpg_buf, frame->jpg_len);
        free(frame->jpg_buf);
        free(frame);
        return res;
    } else {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "No Frame Available", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
}

void start_http_server(QueueHandle_t frame_q, SemaphoreHandle_t mutex, object_database_t *db_ptr) {
    s_frame_queue = frame_q;
    s_motion_mutex = mutex;
    s_obj_db = db_ptr;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.core_id = 0;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    // 3. Tăng timeout (để server kiên nhẫn hơn)
    config.recv_wait_timeout = 10; 
    config.send_wait_timeout = 10;


    httpd_handle_t stream_httpd = NULL;
    
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        // 1. Đăng ký các URI cho phương thức GET
        httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
        httpd_uri_t meta_uri = { .uri = "/metadata", .method = HTTP_GET, .handler = metadata_handler, .user_ctx = NULL };
        
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        httpd_register_uri_handler(stream_httpd, &meta_uri);

        // 2. Đăng ký các URI cho phương thức OPTIONS (Preflight Fix)
        httpd_uri_t stream_options = { .uri = "/stream", .method = HTTP_OPTIONS, .handler = cors_options_handler, .user_ctx = NULL };
        httpd_uri_t meta_options = { .uri = "/metadata", .method = HTTP_OPTIONS, .handler = cors_options_handler, .user_ctx = NULL };

        httpd_register_uri_handler(stream_httpd, &stream_options);
        httpd_register_uri_handler(stream_httpd, &meta_options);
        
        ESP_LOGI(TAG, "HTTP Server started on port %d with CORS support", config.server_port);
    } else {
        ESP_LOGE(TAG, "Error starting server!");
    }
}