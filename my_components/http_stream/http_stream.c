#include "http_stream.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "HTTP_STREAM";

// Con trỏ tới tài nguyên dùng chung (được truyền từ Main vào)
static QueueHandle_t s_frame_queue = NULL;
static SemaphoreHandle_t s_motion_mutex = NULL;
static object_database_t *s_obj_db = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Handler trả về JSON Metadata
static esp_err_t metadata_handler(httpd_req_t *req) {
    if (!s_obj_db || !s_motion_mutex) return ESP_FAIL;

    httpd_resp_set_type(req, "application/json");
    char json_buf[512];
    int len = 0;
    
    // Lock mutex nhẹ để đọc dữ liệu an toàn
    xSemaphoreTake(s_motion_mutex, pdMS_TO_TICKS(100));
    
    len = snprintf(json_buf, sizeof(json_buf), "{\"object_count\":%d,\"objects\":[", s_obj_db->count);
    for (int i = 0; i < s_obj_db->count; i++) {
        if (i > 0) json_buf[len++] = ',';
        len += snprintf(&json_buf[len], sizeof(json_buf) - len,
            "{\"id\":%d,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"age\":%d}",
            s_obj_db->objects[i].id,
            s_obj_db->objects[i].min_x,
            s_obj_db->objects[i].min_y,
            s_obj_db->objects[i].max_x - s_obj_db->objects[i].min_x,
            s_obj_db->objects[i].max_y - s_obj_db->objects[i].min_y,
            s_obj_db->objects[i].age);
    }
    xSemaphoreGive(s_motion_mutex);
    
    len += snprintf(&json_buf[len], sizeof(json_buf) - len, "]}");
    return httpd_resp_send(req, json_buf, len);
}

// Handler stream video MJPEG
static esp_err_t stream_handler(httpd_req_t *req) {
    if (!s_frame_queue) return ESP_FAIL;
    esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    char part_buf[64];
    while (true) {
        jpeg_frame_t *frame = NULL;
        if (xQueueReceive(s_frame_queue, &frame, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue; // Không có frame thì chờ tiếp
        }
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, frame->jpg_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)frame->jpg_buf, frame->jpg_len);
        }
        // Giải phóng bộ nhớ
        free(frame->jpg_buf);
        free(frame);
        if (res != ESP_OK) break;
    }
    return res;
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

    httpd_handle_t stream_httpd = NULL;
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
    httpd_uri_t meta_uri = { .uri = "/metadata", .method = HTTP_GET, .handler = metadata_handler };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        httpd_register_uri_handler(stream_httpd, &meta_uri);
        ESP_LOGI(TAG, "HTTP Server started on port %d", config.server_port);
    }
}