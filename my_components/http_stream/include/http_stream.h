#pragma once
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"

// --- CÁC STRUCT DÙNG CHUNG (Main tạo dữ liệu, HTTP gửi dữ liệu) ---

typedef struct {
    uint8_t *jpg_buf;
    size_t jpg_len;
    int64_t timestamp;
} jpeg_frame_t;

#define MAX_OBJECTS_TRACK 10

// Struct lưu thông tin đối tượng (copy từ code cũ vào đây để dùng chung)
typedef struct {
    int id;
    int min_x, min_y;
    int max_x, max_y;
    int center_x, center_y;
    int age;
    int lost_frames;
    int active;     // Dùng int thay bool cho chuẩn
    int stationary;
    int stationary_frames;
} tracked_object_t;

typedef struct {
    tracked_object_t objects[MAX_OBJECTS_TRACK];
    int count;
} object_database_t;

// --- API ---
// Khởi tạo server, truyền vào con trỏ hàng đợi và database từ Main
void start_http_server(QueueHandle_t frame_q, SemaphoreHandle_t mutex, object_database_t *db_ptr);