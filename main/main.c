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
#include "ota_server.h"
#include "oled_ssd1306.h"

// --- INCLUDE COMPONENTS ---
#include "wifi_manager.h"
#include "esp32_camera_driver.h"
#include "http_stream.h"

#include "ai_person.h"   // TensorFlow Lite Micro cho AI nhận diện người
#include "image_utils.h" // Hàm resize ảnh cho AI
static const char *TAG = "MAIN_APP";

// ================= CONFIG THUẬT TOÁN MOTION =================
#define BLOCK_SIZE 8
#define MOTION_THRESHOLD 15
#define ALERT_THRESHOLD 10
#define CONSECUTIVE_FRAMES 4
#define REGION_MERGE_THRESHOLD 3
#define OBJECT_TIMEOUT 6
#define STATIONARY_TIMEOUT 10
#define STATIONARY_THRESHOLD 10
// #define DISTANCE_THRESHOLD 70
// #define MIN_OBJECT_SIZE  200
// #define MAX_OBJECT_SIZE  40000

// Ví dụ: VGA(640) -> 64px, QVGA(320) -> 32px
#define DYNAMIC_DIST_THRESHOLD(w) ((w) / 10)

// Diện tích nhỏ nhất = Tổng điểm ảnh / 2000 (khoảng 0.05%)
// Ví dụ: VGA -> ~150px, QVGA -> ~38px
#define DYNAMIC_MIN_OBJ_SIZE(w, h) (((w) * (h)) / 400)

// Diện tích lớn nhất = 1/3 tổng diện tích ảnh
#define DYNAMIC_MAX_OBJ_SIZE(w, h) (((w) * (h)) / 3)

// ================= CONFIG VÙNG ROI (KHỚP VỚI JS) =================
// Cắt một dải ngang ở giữa màn hình (giống như giám sát làn đường)
int ROI_X = 0;  // 140 / 4 = 35
int ROI_Y = 0;   // Bắt đầu từ mép trên
int ROI_W = 188;  // 300 / 4 = 75
int ROI_H = 296; // 480 / 4 = 120 (Lấy trọn chiều dọc của QQVGA)

// Buffer riêng cho ROI (Cấp phát trong PSRAM)
static uint8_t *roi_buf = NULL;
// ================= GLOBAL RESOURCES =================
static QueueHandle_t frame_queue = NULL;
static SemaphoreHandle_t motion_mutex = NULL;
static uint8_t *prev_frame = NULL;
static int grid_w = 0, grid_h = 0;

// Database lấy từ http_stream.h
static object_database_t obj_db = {0};
static int next_object_id = 1;
static int consecutive_motion_count = 0;

//==============Biến dùng cho task nén jpeg======================
// Buffer trung gian để chứa ảnh Raw VGA (640x480 = 307200 bytes)
static uint8_t *shared_raw_buf = NULL;
static SemaphoreHandle_t compress_sem = NULL; // Cờ báo hiệu trạng thái
static TaskHandle_t compression_task_handle = NULL;

// Biến lưu thông tin kích thước để task nén biết đường xử lý
static int raw_width = 0;
static int raw_height = 0;

// Thêm biến này ở đầu file, ngoài các hàm
volatile bool camera_is_restarting = false;

// ================= HELPER STRUCTS & ARRAYS =================
typedef struct
{
    int min_x, min_y, max_x, max_y;
    int size;
} motion_region_t;

#define MAX_REGIONS 20
static int motion_regions_count = 0;
static motion_region_t motion_regions[MAX_REGIONS];
// static uint8_t visited[40 * 30] = {0}; // 320/8 * 240/8 = 40*30 grid

typedef struct
{
    motion_region_t regions[MAX_REGIONS];
    int region_count;
    bool has_motion;
} motion_result_t;

//===============LOG MEMORY==============================================
void log_memory_status()
{
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "MEM: Internal=%zu | PSRAM=%zu", free_internal, free_psram);
}
//----------CẤU HÌNH LOG TIME-------------------
#define TICK_US() ((int64_t)esp_timer_get_time())
#define ELAPSED_MS(start) ((float)(esp_timer_get_time() - (start)) / 1000.0f)

//=========== Hàm cắt ảnh (Crop) tối ưu tốc độ================
static void crop_image(const uint8_t *src, int src_w, int src_h,
                       uint8_t *dst,
                       int roi_x, int roi_y, int roi_w, int roi_h)
{
    // 1. Kiểm tra biên để tránh crash nếu config sai
    if (roi_x < 0)
        roi_x = 0;
    if (roi_y < 0)
        roi_y = 0;
    // Nếu vùng cắt tràn ra ngoài ảnh gốc thì thu gọn lại
    if (roi_x + roi_w > src_w)
        roi_w = src_w - roi_x;
    if (roi_y + roi_h > src_h)
        roi_h = src_h - roi_y;

    // 2. Copy từng dòng (Row-by-row copy)
    for (int y = 0; y < roi_h; y++)
    {
        // Tính địa chỉ nguồn: (Hàng bắt đầu + y) * Độ rộng ảnh gốc + Cột bắt đầu
        const uint8_t *src_row = src + ((roi_y + y) * src_w) + roi_x;

        // Tính địa chỉ đích: Hàng y * Độ rộng vùng cắt
        uint8_t *dst_row = dst + (y * roi_w);

        // Copy nguyên 1 dòng pixel
        memcpy(dst_row, src_row, roi_w);
    }
}
// ================= THUẬT TOÁN MOTION (CORE LOGIC) =================

// Hàm vẽ khung hình chữ nhật lên Frame Buffer (để xem trên Web)
static inline void draw_box(uint8_t *buf, int width, int height,
                            int min_x, int min_y, int max_x, int max_y)
{
    if (min_x >= max_x || min_y >= max_y)
        return;
    min_x = (min_x < 0) ? 0 : min_x;
    min_y = (min_y < 0) ? 0 : min_y;
    max_x = (max_x >= width) ? width - 1 : max_x;
    max_y = (max_y >= height) ? height - 1 : max_y;

    const uint8_t color = 255; // Màu trắng (trên ảnh Grayscale)
    // Vẽ cạnh ngang
    for (int x = min_x; x <= max_x; x++)
    {
        buf[min_y * width + x] = color;
        buf[max_y * width + x] = color;
    }
    // Vẽ cạnh dọc
    for (int y = min_y; y <= max_y; y++)
    {
        buf[y * width + min_x] = color;
        buf[y * width + max_x] = color;
    }
}

static inline void draw_box_with_id(uint8_t *buf, int width, int height,
                                    int min_x, int min_y, int max_x, int max_y, int obj_id)
{
    draw_box(buf, width, height, min_x, min_y, max_x, max_y);
    // Vẽ tâm
    int center_x = (min_x + max_x) / 2;
    int center_y = (min_y + max_y) / 2;
    if (center_x > 0 && center_x < width && center_y > 0 && center_y < height)
    {
        buf[center_y * width + center_x] = 255;
        if (center_x + 1 < width)
            buf[center_y * width + center_x + 1] = 255;
        if (center_y + 1 < height)
            buf[(center_y + 1) * width + center_x] = 255;
    }
}

// Tính trung bình khối (Bit shifting optimization)
static inline uint8_t get_block_average(const uint8_t *buf, int width, int bx, int by, int block_size)
{
    uint32_t sum = 0;
    const int start_y = by * block_size;
    const int start_x = bx * block_size;
    for (int y = 0; y < block_size; y++)
    {
        const int row_offset = (start_y + y) * width + start_x;
        for (int x = 0; x < block_size; x++)
            sum += buf[row_offset + x];
    }
    return (uint8_t)(sum >> 6); // Chia 64
}

// Flood Fill tìm vùng motion (Connected Component Analysis)
static void flood_fill_cca(int start_x, int start_y, uint8_t *motion_grid,
                           int grid_width, int grid_height, motion_region_t *region)
{
    typedef struct
    {
        int x, y;
    } point_t;

    // SỬA ĐỔI: Dùng Heap (malloc) thay vì Stack
    // Cấp phát bộ nhớ động cho stack xử lý flood fill
    int max_stack_size = MAX_REGIONS * 20;
    point_t *stack_arr = (point_t *)heap_caps_malloc(max_stack_size * sizeof(point_t), MALLOC_CAP_8BIT);

    if (stack_arr == NULL)
    {
        ESP_LOGE(TAG, "Flood fill malloc failed");
        return;
    }

    int stack_idx = 0;
    stack_arr[stack_idx++] = (point_t){start_x, start_y};

    // Đánh dấu điểm bắt đầu đã thăm
    motion_grid[start_y * grid_width + start_x] = 0;

    region->min_x = start_x;
    region->min_y = start_y;
    region->max_x = start_x;
    region->max_y = start_y;
    region->size = 0;

    while (stack_idx > 0)
    {
        point_t p = stack_arr[--stack_idx];

        region->size++;
        if (p.x < region->min_x)
            region->min_x = p.x;
        if (p.x > region->max_x)
            region->max_x = p.x;
        if (p.y < region->min_y)
            region->min_y = p.y;
        if (p.y > region->max_y)
            region->max_y = p.y;

        int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

        for (int i = 0; i < 8; i++)
        {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];

            if (nx >= 0 && nx < grid_width && ny >= 0 && ny < grid_height)
            {
                int idx = ny * grid_width + nx;
                if (motion_grid[idx])
                {
                    motion_grid[idx] = 0;
                    // Kiểm tra tràn stack mảng
                    if (stack_idx < max_stack_size - 1)
                    {
                        stack_arr[stack_idx++] = (point_t){nx, ny};
                    }
                }
            }
        }
    }

    // giải phóng bộ nhớ sau khi dùng xong
    free(stack_arr);
}

// Gộp các vùng gần nhau
static void merge_nearby_regions(void)
{
    if (motion_regions_count > 12)
        return;
    int merged = 1;
    while (merged && motion_regions_count > 1)
    {
        merged = 0;
        for (int i = 0; i < motion_regions_count - 1; i++)
        {
            for (int j = i + 1; j < motion_regions_count; j++)
            {
                int gap_x = 0, gap_y = 0;
                if (motion_regions[i].max_x < motion_regions[j].min_x)
                    gap_x = motion_regions[j].min_x - motion_regions[i].max_x;
                else if (motion_regions[j].max_x < motion_regions[i].min_x)
                    gap_x = motion_regions[i].min_x - motion_regions[j].max_x;

                if (motion_regions[i].max_y < motion_regions[j].min_y)
                    gap_y = motion_regions[j].min_y - motion_regions[i].max_y;
                else if (motion_regions[j].max_y < motion_regions[i].min_y)
                    gap_y = motion_regions[i].min_y - motion_regions[j].max_y;

                int max_gap = (gap_x > gap_y) ? gap_x : gap_y;
                if (max_gap <= REGION_MERGE_THRESHOLD)
                {
                    // Merge logic
                    if (motion_regions[j].min_x < motion_regions[i].min_x)
                        motion_regions[i].min_x = motion_regions[j].min_x;
                    if (motion_regions[j].min_y < motion_regions[i].min_y)
                        motion_regions[i].min_y = motion_regions[j].min_y;
                    if (motion_regions[j].max_x > motion_regions[i].max_x)
                        motion_regions[i].max_x = motion_regions[j].max_x;
                    if (motion_regions[j].max_y > motion_regions[i].max_y)
                        motion_regions[i].max_y = motion_regions[j].max_y;
                    motion_regions[i].size += motion_regions[j].size;

                    for (int k = j; k < motion_regions_count - 1; k++)
                        motion_regions[k] = motion_regions[k + 1];
                    motion_regions_count--;
                    merged = 1;
                    break;
                }
            }
            if (merged)
                break;
        }
    }
}

static void find_motion_regions(uint8_t *motion_grid, int grid_width, int grid_height)
{
    // memset(visited, 0, grid_width * grid_height);
    motion_regions_count = 0;
    for (int y = 0; y < grid_height; y++)
    {
        for (int x = 0; x < grid_width; x++)
        {
            if (motion_grid[y * grid_width + x])
            {
                if (motion_regions_count < MAX_REGIONS)
                {
                    flood_fill_cca(x, y, motion_grid, grid_width, grid_height, &motion_regions[motion_regions_count]);
                    motion_regions_count++;
                }
            }
        }
    }
    merge_nearby_regions();
}

static inline int distance(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

// Logic theo dõi đối tượng (Tracking)
static void match_and_update_objects(motion_result_t *motion_result, int frame_width, int frame_height)
{
    xSemaphoreTake(motion_mutex, portMAX_DELAY);

    // Đánh dấu tất cả là lost
    for (int i = 0; i < obj_db.count; i++)
    {
        obj_db.objects[i].active = false;
        obj_db.objects[i].lost_frames++;
    }

    for (int r = 0; r < motion_result->region_count; r++)
    {
        motion_region_t *region = &motion_result->regions[r];
        int region_width = region->max_x - region->min_x + 1;
        int region_height = region->max_y - region->min_y + 1;

        // Lọc nhiễu kích thước
        int region_area = region_width * region_height * BLOCK_SIZE * BLOCK_SIZE;

        // --- SỬ DỤNG MACRO DYNAMIC TẠI ĐÂY ---
        if (region_area < DYNAMIC_MIN_OBJ_SIZE(frame_width, frame_height) ||
            region_area > DYNAMIC_MAX_OBJ_SIZE(frame_width, frame_height))
        {
            continue; // Bỏ qua nếu quá nhỏ hoặc quá to
        }

        int center_x = (region->min_x + region->max_x) * BLOCK_SIZE / 2;
        int center_y = (region->min_y + region->max_y) * BLOCK_SIZE / 2;

        // Tìm object cũ khớp nhất
        int best_match = -1;
        int min_dist = DYNAMIC_DIST_THRESHOLD(frame_width);

        for (int i = 0; i < obj_db.count; i++)
        {
            if (obj_db.objects[i].active)
                continue;
            int dist = distance(center_x, center_y, obj_db.objects[i].center_x, obj_db.objects[i].center_y);
            if (dist < min_dist)
            {
                min_dist = dist;
                best_match = i;
            }
        }

        if (best_match >= 0)
        {
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
            if (movement <= STATIONARY_THRESHOLD)
            {
                if (!obj_db.objects[best_match].stationary)
                {
                    obj_db.objects[best_match].stationary = true;
                    obj_db.objects[best_match].stationary_frames = 1;
                }
                else
                    obj_db.objects[best_match].stationary_frames++;
            }
            else
            {
                obj_db.objects[best_match].stationary = false;
                obj_db.objects[best_match].stationary_frames = 0;
            }
        }
        else
        {
            // Tạo object mới
            if (obj_db.count < MAX_OBJECTS_TRACK)
            { // Defined in http_stream.h
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
    for (int i = obj_db.count - 1; i >= 0; i--)
    {
        int timeout = obj_db.objects[i].stationary ? STATIONARY_TIMEOUT : OBJECT_TIMEOUT;
        if (obj_db.objects[i].lost_frames > timeout)
        {
            ESP_LOGI(TAG, "Removed ID=%d", obj_db.objects[i].id);
            for (int j = i; j < obj_db.count - 1; j++)
                obj_db.objects[j] = obj_db.objects[j + 1];
            obj_db.count--;
        }
    }
    // --- THÊM ĐOẠN NÀY ĐỂ RESET ID ---
    // Nếu không còn đối tượng nào đang được theo dõi, reset ID về 1 cho đẹp
    if (obj_db.count == 0)
    {
        next_object_id = 1;
    }
    // ---------------------------------
    xSemaphoreGive(motion_mutex);
}

// Logic tính toán Motion
static void compute_motion(camera_fb_t *fb, motion_result_t *result)
{
    if (prev_frame == NULL)
    {
        grid_w = fb->width / BLOCK_SIZE;
        grid_h = fb->height / BLOCK_SIZE;
        prev_frame = (uint8_t *)heap_caps_malloc(grid_w * grid_h, MALLOC_CAP_8BIT);
        if (prev_frame)
        {
            for (int y = 0; y < grid_h; y++)
                for (int x = 0; x < grid_w; x++)
                    prev_frame[y * grid_w + x] = get_block_average(fb->buf, fb->width, x, y, BLOCK_SIZE);
        }
        result->has_motion = false;
        result->region_count = 0;
        return;
    }

    uint8_t *motion_grid = (uint8_t *)heap_caps_malloc(grid_w * grid_h, MALLOC_CAP_8BIT);
    if (!motion_grid)
        return;

    uint32_t diff_blocks = 0;
    for (int y = 0; y < grid_h; y++)
    {
        for (int x = 0; x < grid_w; x++)
        {
            const uint8_t current_avg = get_block_average(fb->buf, fb->width, x, y, BLOCK_SIZE);
            const uint8_t prev_avg = prev_frame[y * grid_w + x];
            if (abs((int)current_avg - (int)prev_avg) > MOTION_THRESHOLD)
            {
                motion_grid[y * grid_w + x] = 1;
                diff_blocks++;
            }
            else
                motion_grid[y * grid_w + x] = 0;
            prev_frame[y * grid_w + x] = current_avg;
        }
    }

    result->has_motion = (diff_blocks > ALERT_THRESHOLD);
    if (result->has_motion)
    {
        find_motion_regions(motion_grid, grid_w, grid_h);
        result->region_count = motion_regions_count;
        for (int i = 0; i < motion_regions_count && i < MAX_REGIONS; i++)
            result->regions[i] = motion_regions[i];
        consecutive_motion_count++;
    }
    else
    {
        consecutive_motion_count = 0;
        result->region_count = 0;
    }
    free(motion_grid);
}

// ================= MAIN TASKS =================
static void motion_detection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Motion Task Started");
    motion_result_t current_motion = {0};
    int local_frame_counter = 0;
    uint8_t ai_input[96 * 96];
    
    size_t max_roi_size = 750 * 1200;
    size_t max_raw_size = 1600 * 1200;

    // 1. CẤP PHÁT BUFFER CHO ROI
    roi_buf = (uint8_t *)heap_caps_malloc(max_roi_size, MALLOC_CAP_SPIRAM);
    // Cấp phát buffer trung gian trong PSRAM
    shared_raw_buf = (uint8_t *)heap_caps_malloc(max_raw_size, MALLOC_CAP_SPIRAM);
    if (!roi_buf || !shared_raw_buf)
    {
        ESP_LOGE(TAG, "Failed to alloc shared buffer");
        return;
    }

    compress_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(compress_sem); // Khởi tạo trạng thái rảnh

    // --- CÁC BIẾN LƯU THỐNG KÊ HIỆU NĂNG (Lưu thời gian lâu nhất) ---
    float max_cap_ms = 0.0f;
    float max_mot_ms = 0.0f;
    float max_ai_ms  = 0.0f;

    while (true)
    {
        if (camera_is_restarting)
        {
            ESP_LOGW(TAG, "Camera is restarting, motion detection task will delay");
            vTaskDelay(pdMS_TO_TICKS(500)); // Delay để chờ camera khởi động lại xong
            continue;
        }

        // BẮT ĐẦU ĐO TỔNG THỜI GIAN 1 FRAME
        int64_t t_frame_start = TICK_US();

        // -------------------------------------------------------------
        // ĐO THỜI GIAN CAPTURE ẢNH
        // -------------------------------------------------------------
        int64_t t_cap_start = TICK_US();
        camera_fb_t *fb = esp_camera_fb_get();
        
        float cap_time = ELAPSED_MS(t_cap_start);
        if (cap_time > max_cap_ms) max_cap_ms = cap_time; // Cập nhật kỷ lục Capture

        if (!fb)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        local_frame_counter++;
        if (local_frame_counter > 1000)
        {
            local_frame_counter = 0;
        }

        // -------------------------------------------------------------
        // ĐO THỜI GIAN MOTION DETECTION (Gồm cả Crop ảnh)
        // -------------------------------------------------------------
        int64_t t_mot_start = TICK_US();
        
        // BƯỚC 1: CROP ẢNH (Cắt dải 640x200 ở giữa)
        crop_image(fb->buf, fb->width, fb->height, roi_buf, ROI_X, ROI_Y, ROI_W, ROI_H);

        // 2. Tính toán Motion (Mỗi 2 frame làm 1 lần để giảm tải)
        if (local_frame_counter % 2 == 0)
        {
            camera_fb_t roi_fb;
            roi_fb.buf = roi_buf;  
            roi_fb.width = ROI_W;  
            roi_fb.height = ROI_H; 
            roi_fb.format = PIXFORMAT_GRAYSCALE;

            compute_motion(&roi_fb, &current_motion);
            match_and_update_objects(&current_motion, ROI_W, ROI_H);
            
            float mot_time = ELAPSED_MS(t_mot_start);
            if (mot_time > max_mot_ms) max_mot_ms = mot_time; // Cập nhật kỷ lục Motion
        }

        // -------------------------------------------------------------
        // ================== 3. AI PERSON DETECTION ==================
        // -------------------------------------------------------------
        int64_t t_ai_start = TICK_US();
        bool ai_actually_ran = false; // Cờ theo dõi xem CPU có thực sự chạy AI không

        if (current_motion.has_motion && local_frame_counter % 3 == 0) // giảm tải
        {
            if (xSemaphoreTake(motion_mutex, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                for (int i = 0; i < obj_db.count && i < 3; i++)
                {
                    if (!(obj_db.objects[i].active && obj_db.objects[i].lost_frames < 2))
                        continue;

                    int min_x = obj_db.objects[i].min_x;
                    int min_y = obj_db.objects[i].min_y;
                    int max_x = obj_db.objects[i].max_x;
                    int max_y = obj_db.objects[i].max_y;

                    int w = max_x - min_x + 1;
                    int h = max_y - min_y + 1;

                    if (w < 20 || h < 20)
                        continue;

                    // tránh vượt ROI
                    if (min_x < 0 || min_y < 0 ||
                        max_x >= ROI_W || max_y >= ROI_H)
                        continue;

                    // ===== Crop object =====
                    static uint8_t obj_buf[96 * 96]; // tạm
                    memset(obj_buf, 0, sizeof(obj_buf));

                    for (int y = 0; y < h && y < 96; y++)
                    {
                        int copy_w = (w > 96 ? 96 : w);
                        memcpy(&obj_buf[y * 96],
                               &roi_buf[(min_y + y) * ROI_W + min_x],
                               copy_w);
                    }

                    // ===== Resize + normalize =====
                    resize_96x96(obj_buf, 96, 96, ai_input);

                    // ===== Run AI =====
                    int score = ai_run(ai_input);
                    ai_actually_ran = true; // Đánh dấu là AI đã ngốn CPU thật

                    // ===== Threshold =====
                    ESP_LOGI(TAG, "AI score: %d", score);
                    if (score > 150)
                    {
                        obj_db.objects[i].is_person = true;
                    }
                    else
                    {
                        obj_db.objects[i].is_person = false;
                    }
                }
                xSemaphoreGive(motion_mutex);
            }
            
            // Chỉ tính giờ nếu AI có chạy
            float ai_time = ELAPSED_MS(t_ai_start);
            if (ai_actually_ran && ai_time > max_ai_ms) {
                max_ai_ms = ai_time; // Cập nhật kỷ lục AI
            }
        }

        // 2. Vẽ khung lên FrameBuffer (Để stream về web thấy được)
        if (xSemaphoreTake(motion_mutex, 0) == pdTRUE)
        {
            for (int i = 0; i < obj_db.count; i++)
            {
                if (obj_db.objects[i].active && obj_db.objects[i].lost_frames < 2)
                {
                    draw_box_with_id(fb->buf, fb->width, fb->height,
                                     obj_db.objects[i].min_x + ROI_X,
                                     obj_db.objects[i].min_y + ROI_Y,
                                     obj_db.objects[i].max_x + ROI_X,
                                     obj_db.objects[i].max_y + ROI_Y,
                                     obj_db.objects[i].id);
                }
            }
            xSemaphoreGive(motion_mutex);
        }

        if (xSemaphoreTake(compress_sem, 0) == pdTRUE)
        {
            // -- Nếu RẢNH: Copy dữ liệu sang kho chung --
            memcpy(shared_raw_buf, fb->buf, fb->len);

            // Cập nhật kích thước
            raw_width = fb->width;
            raw_height = fb->height;

            // Đánh thức Task nén
            xTaskNotifyGive(compression_task_handle);
        }

        // 4. Trả buffer cho Camera ngay lập tức
        esp_camera_fb_return(fb);
        // Delay ngắn để giữ FPS cao
        vTaskDelay(pdMS_TO_TICKS(1));

        // -------------------------------------------------------------
        // CHỐT SỐ LIỆU VÀ IN RA BẢNG (Mỗi 30 frame in 1 lần)
        // -------------------------------------------------------------
        if (local_frame_counter % 30 == 0)
        {
            // Tính tổng thời gian xấu nhất (WCET)
            float worst_case_total_ms = max_cap_ms + max_mot_ms + max_ai_ms;
            
            // Phòng ngừa lỗi chia cho 0
            float max_theoretical_fps = 0.0f;
            if (worst_case_total_ms > 0) {
                max_theoretical_fps = 1000.0f / worst_case_total_ms;
            }

            // In log y hệt định dạng để copy vào bảng
            ESP_LOGI(TAG, "--- PERFORMANCE STATS (Framesize: %dx%d) ---", fb->width, fb->height);
            ESP_LOGI(TAG, "| Capture: %5.1f ms | Motion: %5.1f ms | AI: %5.1f ms | Total: %5.1f ms | Max FPS: %4.1f |",
                     max_cap_ms, max_mot_ms, max_ai_ms, worst_case_total_ms, max_theoretical_fps);

            // Xóa kỷ lục để bắt đầu đo lại cho 30 frame tiếp theo
            max_cap_ms = 0.0f;
            max_mot_ms = 0.0f;
            max_ai_ms  = 0.0f;
        }
    }
}
//==============================TASK NÉN JPEG=======================
static void compression_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Compression Task Started");

    while (1)
    {
        // 1. Ngủ chờ thông báo từ Motion Task (Chờ tối đa vô hạn)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // int64_t t_start = TICK_US();

        // 2. Nén JPEG từ shared_raw_buf (Lúc này Motion Task đã đi làm việc khác rồi)
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;

        // Quality 40 để cân bằng tốc độ
        bool converted = fmt2jpg(shared_raw_buf, raw_width * raw_height,
                                 raw_width, raw_height, PIXFORMAT_GRAYSCALE, 50, &jpg_buf, &jpg_len);
        // int64_t t_finish = TICK_US();
        // ESP_LOGI(TAG, "Time nén jpeg: %.2f", (t_finish - t_start) / 1000.0f);
        // 3. Gửi vào Queue HTTP Stream
        if (converted && jpg_buf)
        {
            jpeg_frame_t *frame = (jpeg_frame_t *)malloc(sizeof(jpeg_frame_t));
            if (frame)
            {
                frame->jpg_buf = jpg_buf;
                frame->jpg_len = jpg_len;
                frame->timestamp = esp_timer_get_time();

                // Gửi queue (nếu đầy thì free frame cũ để nhét frame mới - ưu tiên ảnh mới nhất)
                if (xQueueSend(frame_queue, &frame, 0) != pdTRUE)
                {
                    // Cơ chế Drop frame cũ
                    jpeg_frame_t *old_frame = NULL;
                    if (xQueueReceive(frame_queue, &old_frame, 0) == pdTRUE)
                    {
                        free(old_frame->jpg_buf);
                        free(old_frame);
                    }
                    // Thử gửi lần 2
                    if (xQueueSend(frame_queue, &frame, 0) != pdTRUE)
                    {
                        // Vẫn thất bại? (Cực hiếm nhưng phải xử lý)
                        // Phải giải phóng frame hiện tại, không là mất bộ nhớ
                        ESP_LOGW(TAG, "Queue full, dropping current frame");
                        free(frame->jpg_buf);
                        free(frame);
                    }
                }
            }
            else
            {
                free(jpg_buf);
            }
        }

        // ESP_LOGI(TAG, "Compression Time: %.2f ms", (float)(esp_timer_get_time() - t_start)/1000);

        // 4. Báo hiệu "Tôi đã rảnh" (Give Semaphore)
        xSemaphoreGive(compress_sem);
    }
}

//====================================hàm oled task=======================
static void oled_display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OLED Display Task Started");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Chờ 1 giây trước khi bắt đầu
    while (1)
    {
        // lấy mutex từ motion_mutex
        if (xSemaphoreTake(motion_mutex, portMAX_DELAY) == pdTRUE)
        {
            oled_clear();
            oled_draw_string(3, 0, "Motion Monitor:");
            char line[32];
            // Dòng 2: Số lượng
            snprintf(line, sizeof(line), "Count: %d", obj_db.count);
            oled_draw_string(3, 10, line);

            // Dòng 3-5: Liệt kê object
            for (int i = 0; i < obj_db.count && i < 3; i++)
            {
                int w = obj_db.objects[i].max_x - obj_db.objects[i].min_x + 1;
                int h = obj_db.objects[i].max_y - obj_db.objects[i].min_y + 1;
                int area = w * h;

                char status = obj_db.objects[i].stationary ? 'S' : 'M';
                // Format ngắn gọn: ID[S]:Size
                snprintf(line, sizeof(line), "#%d[%c]:%d",
                         obj_db.objects[i].id, status, area);
                oled_draw_string(3, 20 + (i * 10), line);
            }
            oled_update();
            xSemaphoreGive(motion_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ================= APP MAIN =================
void app_main(void)
{
    ai_init(); // AI_person
    printf("Free RAM: %d bytes\n", (int)esp_get_free_heap_size());
    oled_init_driver(); // Khởi động OLED Driver
    oled_draw_string(3, 0, "ESP32-S3 Cam");
    // 1. NVS Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    oled_draw_string(3, 10, "NVS Initialized");
    oled_update();
    // 2. Init các Component
    if (camera_driver_init() != ESP_OK)
        return; // Khởi động Camera

    wifi_manager_init(); // Kết nối WiFi
    // wifi_init_softap();
    // Đợi một chút cho mạng ổn định (Optional nhưng tốt)
    vTaskDelay(pdMS_TO_TICKS(100));
    oled_draw_string(3, 20, "WiFi Connected");
    oled_update();

    start_ota_server(); // Khởi động OTA Server
    oled_draw_string(3, 30, "OTA Server Started");
    oled_update();

    // 3. Tạo tài nguyên chia sẻ
    frame_queue = xQueueCreate(4, sizeof(jpeg_frame_t *));
    motion_mutex = xSemaphoreCreateMutex();
    // Để Stack lớn (8192) vì nén JPEG tốn stack
    xTaskCreatePinnedToCore(compression_task, "compress_task", 8192, NULL, 1, &compression_task_handle, 1);
    // 4. Start Motion Task (Core 1)
    xTaskCreatePinnedToCore(motion_detection_task, "motion_task", 16384, NULL, 2, NULL, 1);

    // 5. Start HTTP Server
    start_http_server(frame_queue, motion_mutex, &obj_db);
    xTaskCreate(oled_display_task, "oled_task", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "System Started:");
}