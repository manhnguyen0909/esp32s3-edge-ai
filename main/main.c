#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

// --- INCLUDE COMPONENTS ---
#include "wifi_manager.h"
#include "esp32_camera_driver.h"
#include "http_stream.h"

static const char *TAG = "MAIN_APP";

// ================= CONFIG THUẬT TOÁN MOTION =================
#define BLOCK_SIZE       8
#define MOTION_THRESHOLD 30
#define ALERT_THRESHOLD  20
#define CONSECUTIVE_FRAMES 2
#define MIN_OBJECT_SIZE  200
#define REGION_MERGE_THRESHOLD 3
#define OBJECT_TIMEOUT   6
#define STATIONARY_TIMEOUT 300
#define STATIONARY_THRESHOLD 3
#define DISTANCE_THRESHOLD 90

// ================= GLOBAL RESOURCES =================
static QueueHandle_t frame_queue = NULL;
static SemaphoreHandle_t motion_mutex = NULL;
static uint8_t *prev_frame = NULL;
static int grid_w = 0, grid_h = 0;

// Database lấy từ http_stream.h
static object_database_t obj_db = {0}; 
static int next_object_id = 1;
static int consecutive_motion_count = 0;

// ================= HELPER STRUCTS & ARRAYS =================
typedef struct {
    int min_x, min_y, max_x, max_y;
    int size;
} motion_region_t;

#define MAX_REGIONS 20
static int motion_regions_count = 0;
static motion_region_t motion_regions[MAX_REGIONS];
static uint8_t visited[40 * 30] = {0}; // 320/8 * 240/8 = 40*30 grid

typedef struct {
    motion_region_t regions[MAX_REGIONS];
    int region_count;
    bool has_motion;
} motion_result_t;

// ================= THUẬT TOÁN MOTION (CORE LOGIC) =================

// Hàm vẽ khung hình chữ nhật lên Frame Buffer (để xem trên Web)
static inline void draw_box(uint8_t *buf, int width, int height, 
                            int min_x, int min_y, int max_x, int max_y) {
    if (min_x >= max_x || min_y >= max_y) return;
    min_x = (min_x < 0) ? 0 : min_x;
    min_y = (min_y < 0) ? 0 : min_y;
    max_x = (max_x >= width) ? width - 1 : max_x;
    max_y = (max_y >= height) ? height - 1 : max_y;
    
    const uint8_t color = 255; // Màu trắng (trên ảnh Grayscale)
    // Vẽ cạnh ngang
    for (int x = min_x; x <= max_x; x++) { buf[min_y * width + x] = color; buf[max_y * width + x] = color; }
    // Vẽ cạnh dọc
    for (int y = min_y; y <= max_y; y++) { buf[y * width + min_x] = color; buf[y * width + max_x] = color; }
}

static inline void draw_box_with_id(uint8_t *buf, int width, int height, 
                                    int min_x, int min_y, int max_x, int max_y, int obj_id) {
    draw_box(buf, width, height, min_x, min_y, max_x, max_y);
    // Vẽ tâm
    int center_x = (min_x + max_x) / 2;
    int center_y = (min_y + max_y) / 2;
    if (center_x > 0 && center_x < width && center_y > 0 && center_y < height) {
        buf[center_y * width + center_x] = 255;
        if (center_x + 1 < width) buf[center_y * width + center_x + 1] = 255;
        if (center_y + 1 < height) buf[(center_y + 1) * width + center_x] = 255;
    }
}

// Tính trung bình khối (Bit shifting optimization)
static inline uint8_t get_block_average(const uint8_t *buf, int width, int bx, int by, int block_size) {
    uint32_t sum = 0;
    const int start_y = by * block_size;
    const int start_x = bx * block_size;
    for (int y = 0; y < block_size; y++) {
        const int row_offset = (start_y + y) * width + start_x;
        for (int x = 0; x < block_size; x++) sum += buf[row_offset + x];
    }
    return (uint8_t)(sum >> 6); // Chia 64
}

// Flood Fill tìm vùng motion (Connected Component Analysis)
static void flood_fill_cca(int start_x, int start_y, uint8_t *motion_grid, 
                           int grid_width, int grid_height, motion_region_t *region) {
    typedef struct { int x, y; } point_t;
    point_t stack[MAX_REGIONS * 4];
    int stack_idx = 0;
    stack[stack_idx++] = (point_t){start_x, start_y};
    
    region->min_x = start_x; region->min_y = start_y;
    region->max_x = start_x; region->max_y = start_y;
    region->size = 0;
    
    while (stack_idx > 0) {
        point_t p = stack[--stack_idx];
        if (p.x < 0 || p.x >= grid_width || p.y < 0 || p.y >= grid_height) continue;
        if (visited[p.y * grid_width + p.x]) continue;
        if (!motion_grid[p.y * grid_width + p.x]) continue;
        
        visited[p.y * grid_width + p.x] = 1;
        region->size++;
        if (p.x < region->min_x) region->min_x = p.x;
        if (p.x > region->max_x) region->max_x = p.x;
        if (p.y < region->min_y) region->min_y = p.y;
        if (p.y > region->max_y) region->max_y = p.y;
        
        if (stack_idx < MAX_REGIONS * 4 - 8) {
            int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
            int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
            for (int i = 0; i < 8; i++) {
                int nx = p.x + dx[i], ny = p.y + dy[i];
                if (nx >= 0 && nx < grid_width && ny >= 0 && ny < grid_height) {
                    if (!visited[ny * grid_width + nx] && motion_grid[ny * grid_width + nx]) {
                        stack[stack_idx++] = (point_t){nx, ny};
                    }
                }
            }
        }
    }
}

// Gộp các vùng gần nhau
static void merge_nearby_regions(void) {
    if (motion_regions_count > 12) return;
    int merged = 1;
    while (merged && motion_regions_count > 1) {
        merged = 0;
        for (int i = 0; i < motion_regions_count - 1; i++) {
            for (int j = i + 1; j < motion_regions_count; j++) {
                int gap_x = 0, gap_y = 0;
                if (motion_regions[i].max_x < motion_regions[j].min_x) gap_x = motion_regions[j].min_x - motion_regions[i].max_x;
                else if (motion_regions[j].max_x < motion_regions[i].min_x) gap_x = motion_regions[i].min_x - motion_regions[j].max_x;
                
                if (motion_regions[i].max_y < motion_regions[j].min_y) gap_y = motion_regions[j].min_y - motion_regions[i].max_y;
                else if (motion_regions[j].max_y < motion_regions[i].min_y) gap_y = motion_regions[i].min_y - motion_regions[j].max_y;
                
                int max_gap = (gap_x > gap_y) ? gap_x : gap_y;
                if (max_gap <= REGION_MERGE_THRESHOLD) {
                    // Merge logic
                    if (motion_regions[j].min_x < motion_regions[i].min_x) motion_regions[i].min_x = motion_regions[j].min_x;
                    if (motion_regions[j].min_y < motion_regions[i].min_y) motion_regions[i].min_y = motion_regions[j].min_y;
                    if (motion_regions[j].max_x > motion_regions[i].max_x) motion_regions[i].max_x = motion_regions[j].max_x;
                    if (motion_regions[j].max_y > motion_regions[i].max_y) motion_regions[i].max_y = motion_regions[j].max_y;
                    motion_regions[i].size += motion_regions[j].size;
                    
                    for (int k = j; k < motion_regions_count - 1; k++) motion_regions[k] = motion_regions[k + 1];
                    motion_regions_count--;
                    merged = 1;
                    break;
                }
            }
            if (merged) break;
        }
    }
}

static void find_motion_regions(uint8_t *motion_grid, int grid_width, int grid_height) {
    memset(visited, 0, grid_width * grid_height);
    motion_regions_count = 0;
    for (int y = 0; y < grid_height; y++) {
        for (int x = 0; x < grid_width; x++) {
            if (!visited[y * grid_width + x] && motion_grid[y * grid_width + x]) {
                if (motion_regions_count < MAX_REGIONS) {
                    flood_fill_cca(x, y, motion_grid, grid_width, grid_height, &motion_regions[motion_regions_count]);
                    motion_regions_count++;
                }
            }
        }
    }
    merge_nearby_regions();
}

static inline int distance(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

// Logic theo dõi đối tượng (Tracking)
static void match_and_update_objects(motion_result_t *motion_result) {
    xSemaphoreTake(motion_mutex, portMAX_DELAY);
    
    // Đánh dấu tất cả là lost
    for (int i = 0; i < obj_db.count; i++) { obj_db.objects[i].active = false; obj_db.objects[i].lost_frames++; }
    
    for (int r = 0; r < motion_result->region_count; r++) {
        motion_region_t *region = &motion_result->regions[r];
        int region_width = region->max_x - region->min_x + 1;
        int region_height = region->max_y - region->min_y + 1;
        
        // Lọc nhiễu kích thước
        if ((region_width * region_height * BLOCK_SIZE * BLOCK_SIZE) < MIN_OBJECT_SIZE) continue;
        
        int center_x = (region->min_x + region->max_x) * BLOCK_SIZE / 2;
        int center_y = (region->min_y + region->max_y) * BLOCK_SIZE / 2;
        
        // Tìm object cũ khớp nhất
        int best_match = -1;
        int min_dist = DISTANCE_THRESHOLD;
        
        for (int i = 0; i < obj_db.count; i++) {
            if (obj_db.objects[i].active) continue;
            int dist = distance(center_x, center_y, obj_db.objects[i].center_x, obj_db.objects[i].center_y);
            if (dist < min_dist) { min_dist = dist; best_match = i; }
        }
        
        if (best_match >= 0) {
            // Update object cũ
            int old_cx = obj_db.objects[best_match].center_x;
            int old_cy = obj_db.objects[best_match].center_y;
            obj_db.objects[best_match].min_x = region->min_x * BLOCK_SIZE;
            obj_db.objects[best_match].min_y = region->min_y * BLOCK_SIZE;
            obj_db.objects[best_match].max_x = (region->max_x + 1) * BLOCK_SIZE - 1;
            obj_db.objects[best_match].max_y = (region->max_y + 1) * BLOCK_SIZE - 1;
            obj_db.objects[best_match].center_x = center_x;
            obj_db.objects[best_match].center_y = center_y;
            obj_db.objects[best_match].active = true;
            obj_db.objects[best_match].lost_frames = 0;
            obj_db.objects[best_match].age++;
            
            // Check đứng yên
            int movement = abs(center_x - old_cx) + abs(center_y - old_cy);
            if (movement <= STATIONARY_THRESHOLD) {
                if (!obj_db.objects[best_match].stationary) {
                    obj_db.objects[best_match].stationary = true;
                    obj_db.objects[best_match].stationary_frames = 1;
                } else obj_db.objects[best_match].stationary_frames++;
            } else {
                obj_db.objects[best_match].stationary = false;
                obj_db.objects[best_match].stationary_frames = 0;
            }
        } else {
            // Tạo object mới
            if (obj_db.count < MAX_OBJECTS_TRACK) { // Defined in http_stream.h
                int idx = obj_db.count;
                obj_db.objects[idx].id = next_object_id++;
                obj_db.objects[idx].min_x = region->min_x * BLOCK_SIZE;
                obj_db.objects[idx].min_y = region->min_y * BLOCK_SIZE;
                obj_db.objects[idx].max_x = (region->max_x + 1) * BLOCK_SIZE - 1;
                obj_db.objects[idx].max_y = (region->max_y + 1) * BLOCK_SIZE - 1;
                obj_db.objects[idx].center_x = center_x;
                obj_db.objects[idx].center_y = center_y;
                obj_db.objects[idx].active = true;
                obj_db.objects[idx].lost_frames = 0;
                obj_db.objects[idx].age = 1;
                obj_db.objects[idx].stationary = false;
                obj_db.objects[idx].stationary_frames = 0;
                obj_db.count++;
                ESP_LOGI(TAG, "New Object ID=%d", obj_db.objects[idx].id);
            }
        }
    }
    
    // Xóa object quá hạn
    for (int i = obj_db.count - 1; i >= 0; i--) {
        int timeout = obj_db.objects[i].stationary ? STATIONARY_TIMEOUT : OBJECT_TIMEOUT;
        if (obj_db.objects[i].lost_frames > timeout) {
            ESP_LOGI(TAG, "Removed ID=%d", obj_db.objects[i].id);
            for (int j = i; j < obj_db.count - 1; j++) obj_db.objects[j] = obj_db.objects[j + 1];
            obj_db.count--;
        }
    }
    xSemaphoreGive(motion_mutex);
}

// Logic tính toán Motion
static void compute_motion(camera_fb_t *fb, motion_result_t *result) {
    if (prev_frame == NULL) {
        grid_w = fb->width / BLOCK_SIZE;
        grid_h = fb->height / BLOCK_SIZE;
        prev_frame = (uint8_t *)heap_caps_malloc(grid_w * grid_h, MALLOC_CAP_8BIT);
        if (prev_frame) {
             for (int y = 0; y < grid_h; y++) 
                for (int x = 0; x < grid_w; x++) 
                    prev_frame[y * grid_w + x] = get_block_average(fb->buf, fb->width, x, y, BLOCK_SIZE);
        }
        result->has_motion = false;
        result->region_count = 0;
        return;
    }

    uint8_t *motion_grid = (uint8_t *)heap_caps_malloc(grid_w * grid_h, MALLOC_CAP_8BIT);
    if (!motion_grid) return;
    
    uint32_t diff_blocks = 0;
    for (int y = 0; y < grid_h; y++) {
        for (int x = 0; x < grid_w; x++) {
            const uint8_t current_avg = get_block_average(fb->buf, fb->width, x, y, BLOCK_SIZE);
            const uint8_t prev_avg = prev_frame[y * grid_w + x];
            if (abs((int)current_avg - (int)prev_avg) > MOTION_THRESHOLD) {
                motion_grid[y * grid_w + x] = 1;
                diff_blocks++;
            } else motion_grid[y * grid_w + x] = 0;
            prev_frame[y * grid_w + x] = current_avg;
        }
    }

    result->has_motion = (diff_blocks > ALERT_THRESHOLD);
    if (result->has_motion) {
        find_motion_regions(motion_grid, grid_w, grid_h);
        result->region_count = motion_regions_count;
        for (int i = 0; i < motion_regions_count && i < MAX_REGIONS; i++) result->regions[i] = motion_regions[i];
        consecutive_motion_count++;
    } else {
        consecutive_motion_count = 0;
        result->region_count = 0;
    }
    free(motion_grid);
}

// ================= MAIN TASKS =================
static void motion_detection_task(void *pvParameters) {
    ESP_LOGI(TAG, "Motion Task Started (No OLED)");
    motion_result_t current_motion = {0};
    int local_frame_counter = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        local_frame_counter++;
        
        // 1. Tính toán Motion (Mỗi 2 frame làm 1 lần để giảm tải)
        if (local_frame_counter % 2 == 0) {
            compute_motion(fb, &current_motion);
            match_and_update_objects(&current_motion);
        }
        
        // 2. Vẽ khung lên FrameBuffer (Để stream về web thấy được)
        if (xSemaphoreTake(motion_mutex, 0) == pdTRUE) {
            for (int i = 0; i < obj_db.count; i++) {
                if (obj_db.objects[i].active && obj_db.objects[i].lost_frames < 2) {
                    draw_box_with_id(fb->buf, fb->width, fb->height,
                                     obj_db.objects[i].min_x, obj_db.objects[i].min_y,
                                     obj_db.objects[i].max_x, obj_db.objects[i].max_y,
                                     obj_db.objects[i].id);
                }
            }
            xSemaphoreGive(motion_mutex);
        }

        // 3. Nén JPEG và gửi đi
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        bool converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 10, &jpg_buf, &jpg_len);
        esp_camera_fb_return(fb);

        if (converted && jpg_buf) {
            jpeg_frame_t *frame = (jpeg_frame_t *)malloc(sizeof(jpeg_frame_t));
            if (frame) {
                frame->jpg_buf = jpg_buf;
                frame->jpg_len = jpg_len;
                frame->timestamp = esp_timer_get_time();
                
                // Đẩy vào Queue
                if (xQueueSend(frame_queue, &frame, 0) != pdTRUE) {
                    // Nếu đầy, drop frame cũ
                    jpeg_frame_t *old_frame = NULL;
                    if (xQueueReceive(frame_queue, &old_frame, 0) == pdTRUE) { free(old_frame->jpg_buf); free(old_frame); }
                    if (xQueueSend(frame_queue, &frame, 0) != pdTRUE) { free(frame->jpg_buf); free(frame); }
                }
            } else free(jpg_buf);
        }
        
        // Delay ngắn để giữ FPS cao
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ================= APP MAIN =================
void app_main(void) {
    // 1. NVS Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    // 2. Init các Component
    if (camera_driver_init() != ESP_OK) return;
    wifi_manager_init();

    // 3. Tạo tài nguyên chia sẻ
    frame_queue = xQueueCreate(4, sizeof(jpeg_frame_t *));
    motion_mutex = xSemaphoreCreateMutex();
    
    // 4. Start Motion Task (Core 1)
    xTaskCreatePinnedToCore(motion_detection_task, "motion_task", 4096, NULL, 2, NULL, 1);

    // 5. Start HTTP Server
    start_http_server(frame_queue, motion_mutex, &obj_db);
    
    ESP_LOGI(TAG, "System Started - OLED Removed");
}