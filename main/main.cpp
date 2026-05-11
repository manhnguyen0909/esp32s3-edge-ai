#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ESP-IDF headers
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

// C++ headers
#include "JPEGDEC.h" // JPEG decoder library
#include "ai_person.h"

// Local C headers
extern "C"
{
#include "ota_server.h"
#include "oled_ssd1306.h"
#include "wifi_manager.h"
#include "esp32_camera_driver.h"
#include "http_stream.h"
#include "image_utils.h"
}
static const char *TAG = "MAIN_APP";

// Motion detection settings
#define BLOCK_SIZE 8
#define MOTION_THRESHOLD 15
#define ALERT_THRESHOLD 10
#define CONSECUTIVE_FRAMES 4
#define REGION_MERGE_THRESHOLD 3
#define OBJECT_TIMEOUT 6
#define STATIONARY_TIMEOUT 10
#define STATIONARY_THRESHOLD 10

#define DYNAMIC_DIST_THRESHOLD(w) ((w) / 10)
#define DYNAMIC_MIN_OBJ_SIZE(w, h) (((w) * (h)) / 400)
#define DYNAMIC_MAX_OBJ_SIZE(w, h) (((w) * (h)) / 3)

// Dynamic ROI configuration
static int ROI_X = 0;
static int ROI_Y = 0;
static int ROI_W = 0;
static int ROI_H = 0;

#define CALC_ROI_W(frame_w) ((frame_w) / 2)
#define CALC_ROI_H(frame_h) (frame_h)
#define CALC_ROI_X(frame_w) ((frame_w - CALC_ROI_W(frame_w)) / 2)
#define CALC_ROI_Y(frame_h) (0)

// ROI buffer allocated in PSRAM
static uint8_t *roi_buf = NULL;

// Global resources
static QueueHandle_t frame_queue = NULL;
static SemaphoreHandle_t motion_mutex = NULL;
static uint8_t *prev_frame = NULL;
static int grid_w = 0, grid_h = 0;

// Object tracking database
static object_database_t obj_db = {};
static int next_object_id = 1;
static int consecutive_motion_count = 0;

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

// Motion detection core logic

// Compute average intensity for a block using bit-shift optimization
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
    return (uint8_t)(sum >> 6); // Divide by 64
}

// Flood fill segmentation for motion region extraction
static void flood_fill_cca(int start_x, int start_y, uint8_t *motion_grid,
                           int grid_width, int grid_height, motion_region_t *region)
{
    typedef struct
    {
        int x, y;
    } point_t;

    // Allocate dynamic stack memory for flood fill traversal
    int max_stack_size = MAX_REGIONS * 20;
    point_t *stack_arr = (point_t *)heap_caps_malloc(max_stack_size * sizeof(point_t), MALLOC_CAP_8BIT);

    if (stack_arr == NULL)
    {
        ESP_LOGE(TAG, "Flood fill malloc failed");
        return;
    }

    int stack_idx = 0;
    stack_arr[stack_idx++] = (point_t){start_x, start_y};

    // Mark starting cell as visited
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
                    // Prevent dynamic stack overflow
                    if (stack_idx < max_stack_size - 1)
                    {
                        stack_arr[stack_idx++] = (point_t){nx, ny};
                    }
                }
            }
        }
    }

    // Free dynamic stack memory
    free(stack_arr);
}

// Merge adjacent motion regions
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

// Object tracking logic
static void match_and_update_objects(motion_result_t *motion_result, int frame_width, int frame_height)
{
    xSemaphoreTake(motion_mutex, portMAX_DELAY);

    // Mark all tracked objects inactive until reassigned
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

        // Use dynamic size thresholds for motion filtering
        if (region_area < DYNAMIC_MIN_OBJ_SIZE(frame_width, frame_height) ||
            region_area > DYNAMIC_MAX_OBJ_SIZE(frame_width, frame_height))
        {
            continue; // Skip regions that are too small or too large
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

            // Check if object is stationary
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
            // Create a new tracked object
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

    // Remove expired tracked objects
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
    // Reset object ID counter when tracking list is empty
    if (obj_db.count == 0)
    {
        next_object_id = 1;
    }
    // ---------------------------------
    xSemaphoreGive(motion_mutex);
}

// Motion computation logic
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

// Fast JPEG decode using JPEGDEC
static JPEGDEC jpeg;

// Callback invoked for each decoded MCU block
int JPEGDraw(JPEGDRAW *pDraw)
{
    // Current MCU block coordinates
    int mcu_min_x = pDraw->x;
    int mcu_max_x = pDraw->x + pDraw->iWidth - 1;
    int mcu_min_y = pDraw->y;
    int mcu_max_y = pDraw->y + pDraw->iHeight - 1;

    // ROI boundaries from current configuration
    int roi_min_x = ROI_X;
    int roi_max_x = ROI_X + ROI_W - 1;
    int roi_min_y = ROI_Y;
    int roi_max_y = ROI_Y + ROI_H - 1;

    // Skip blocks that do not intersect the ROI
    if (mcu_max_x < roi_min_x || mcu_min_x > roi_max_x ||
        mcu_max_y < roi_min_y || mcu_min_y > roi_max_y)
    {
        return 1; // Ignore this block
    }

    // Compute ROI and MCU overlap
    int inter_min_x = (roi_min_x > mcu_min_x) ? roi_min_x : mcu_min_x;
    int inter_max_x = (roi_max_x < mcu_max_x) ? roi_max_x : mcu_max_x;
    int inter_min_y = (roi_min_y > mcu_min_y) ? roi_min_y : mcu_min_y;
    int inter_max_y = (roi_max_y < mcu_max_y) ? roi_max_y : mcu_max_y;

    uint8_t *pixels = (uint8_t *)pDraw->pPixels; // Grayscale pixel data for the current MCU block

    // Copy the overlapping region into the ROI buffer
    for (int y = inter_min_y; y <= inter_max_y; y++)
    {
        int dest_y = y - roi_min_y; // Destination Y in ROI buffer
        int src_y = y - mcu_min_y;  // Source Y in MCU block

        for (int x = inter_min_x; x <= inter_max_x; x++)
        {
            int dest_x = x - roi_min_x; // Destination X in ROI buffer
            int src_x = x - mcu_min_x;  // Source X in MCU block

            // Write grayscale pixel to ROI buffer
            roi_buf[dest_y * ROI_W + dest_x] = pixels[src_y * pDraw->iWidth + src_x];
        }
    }

    return 1; // Allow decoder to continue
}
// ================= MAIN TASKS =================
static void motion_detection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Motion Task Started");
    motion_result_t current_motion = {};
    int local_frame_counter = 0;
    uint8_t ai_input[96 * 96];

    // Allocate only ROI buffer in PSRAM. No large shared raw frame buffer is required.
    // Initial dynamic ROI setup on the first captured frame.
    int roi_calc_done = 0;

    roi_buf = (uint8_t *)heap_caps_malloc(640 * 480, MALLOC_CAP_SPIRAM); // Temporary ROI allocation
    if (!roi_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate ROI buffer");
        return;
    }

    // compress_sem = xSemaphoreCreateBinary();
    // xSemaphoreGive(compress_sem); // Initialize compression semaphore state

    // Performance statistics variables (track maximum latency)
    float max_cap_ms = 0.0f;
    float max_mot_ms = 0.0f;
    float max_ai_ms = 0.0f;
    // Use this timer to measure hardware capture latency
    static int64_t last_hw_timestamp = 0;
    while (true)
    {
        if (camera_is_restarting)
        {
            ESP_LOGW(TAG, "Camera is restarting, motion detection task will delay");
            vTaskDelay(pdMS_TO_TICKS(500)); // Wait while camera restarts
            continue;
        }

        // Start total frame timer
        int64_t t_frame_start = TICK_US();

        // -------------------------------------------------------------
        // Measure capture latency
        // -------------------------------------------------------------
        int64_t t_cap_start = TICK_US();
        camera_fb_t *fb = esp_camera_fb_get();
        // =======================================================
        // Measure actual camera hardware latency
        // =======================================================
        int64_t current_hw_timestamp = (int64_t)fb->timestamp.tv_sec * 1000000LL + fb->timestamp.tv_usec;
        float true_hardware_ms = 0.0f;

        if (last_hw_timestamp != 0)
        {
            true_hardware_ms = (float)(current_hw_timestamp - last_hw_timestamp) / 1000.0f;
        }
        last_hw_timestamp = current_hw_timestamp;

        // This can be logged or used for performance reporting
        //ESP_LOGI(TAG, "time to capture: %.1f ms", true_hardware_ms);
        int64_t t_cap_end = TICK_US();

        float cap_time = ELAPSED_MS(t_cap_start);
        if (cap_time > max_cap_ms)
            max_cap_ms = cap_time; // Update capture max latency

        if (!fb)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        local_frame_counter++;

        // ===== Initial dynamic ROI calculation =====
        if (!roi_calc_done)
        {
            int frame_w = fb->width;
            int frame_h = fb->height;

            // Compute ROI parameters
            ROI_W = CALC_ROI_W(frame_w); // 50% chiều rộng frame
            ROI_H = CALC_ROI_H(frame_h); // 100% chiều cao frame
            ROI_X = CALC_ROI_X(frame_w); // Căn giữa theo chiều ngang
            ROI_Y = CALC_ROI_Y(frame_h); // Top-aligned vertically

            // Reallocate ROI buffer at the target resolution
            heap_caps_free(roi_buf);
            roi_buf = (uint8_t *)heap_caps_malloc(ROI_W * ROI_H, MALLOC_CAP_SPIRAM);
            if (!roi_buf)
            {
                ESP_LOGE(TAG, "Failed to alloc roi buffer");
                return;
            }

            ESP_LOGI(TAG, "ROI Config: X=%d, Y=%d, W=%d, H=%d (Frame: %dx%d)",
                     ROI_X, ROI_Y, ROI_W, ROI_H, frame_w, frame_h);
            roi_calc_done = 1;
        }

        // 2. GỬI ẢNH JPEG GỐC CHO WEB STREAM (Siêu nhanh ~1ms)
        // =======================================================
        jpeg_frame_t *frame = (jpeg_frame_t *)malloc(sizeof(jpeg_frame_t));
        if (frame)
        {
            // Copy JPEG payload to queue and release camera framebuffer quickly
            frame->jpg_buf = (uint8_t *)malloc(fb->len);
            if (frame->jpg_buf)
            {
                memcpy(frame->jpg_buf, fb->buf, fb->len);
                frame->jpg_len = fb->len;
                frame->timestamp = esp_timer_get_time();

                // Enqueue frame, dropping oldest frame on overflow
                if (xQueueSend(frame_queue, &frame, 0) != pdTRUE)
                {
                    jpeg_frame_t *old_frame;
                    if (xQueueReceive(frame_queue, &old_frame, 0))
                    {
                        free(old_frame->jpg_buf);
                        free(old_frame);
                    }
                    if (xQueueSend(frame_queue, &frame, 0) != pdTRUE)
                    {
                        free(frame->jpg_buf);
                        free(frame);
                    }
                }
            }
            else
            {
                free(frame);
            }
        }
        // =======================================================
        // 3. Decode JPEG directly to grayscale ROI buffer
        // =======================================================
        int64_t t_decode_start = TICK_US();
        if (jpeg.openRAM((uint8_t *)fb->buf, fb->len, JPEGDraw))
        {
            jpeg.setPixelType(EIGHT_BIT_GRAYSCALE); // Use grayscale output only
            jpeg.decode(0, 0, 0);                   // This invokes JPEGDraw repeatedly for each MCU block
            jpeg.close();
        }
        float decode_ms = ELAPSED_MS(t_decode_start);
        static float max_decode_ms = 0.0f;
        if (decode_ms > max_decode_ms)
            max_decode_ms = decode_ms; // Update peak decode latency

        // Return framebuffer to the camera hardware
        // esp_camera_fb_return(fb);
        // -------------------------------------------------------------
        // Measure motion detection latency (including ROI setup)
        // -------------------------------------------------------------
        int64_t t_mot_start = TICK_US();

        // Step 1: ROI cropping can be performed here if needed
        // crop_image(fb->buf, fb->width, fb->height, roi_buf, ROI_X, ROI_Y, ROI_W, ROI_H);

        // 2. Perform motion analysis every two frames to reduce load
        bool motion_ran = (local_frame_counter % 2 == 0);
        if (motion_ran)
        {
            camera_fb_t roi_fb;
            roi_fb.buf = roi_buf;
            roi_fb.width = ROI_W;
            roi_fb.height = ROI_H;
            roi_fb.format = PIXFORMAT_GRAYSCALE;

            compute_motion(&roi_fb, &current_motion);
            match_and_update_objects(&current_motion, ROI_W, ROI_H);
        }

        float mot_time = ELAPSED_MS(t_mot_start);
        // Record worst-case motion processing latency only when active
        if (motion_ran && mot_time > max_mot_ms)
            max_mot_ms = mot_time; // Update peak motion latency

        // -------------------------------------------------------------
        // ================== 3. AI PERSON DETECTION ==================
        // -------------------------------------------------------------
        bool ai_actually_ran = false; // Flag indicating whether AI inference executed

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

                    // ===== Run AI inference and measure latency per object =====
                    int64_t t_ai_start = TICK_US(); // Record AI inference start time for this object
                    int score = ai_run(ai_input);
                    float ai_time = ELAPSED_MS(t_ai_start);

                    ai_actually_ran = true; // Mark that AI inference actually ran

                    // ===== Threshold =====
                    ESP_LOGI(TAG, "AI score: %d (time: %.1f ms)", score, ai_time);
                    if (score > 150)
                    {
                        obj_db.objects[i].is_person = true;
                    }
                    else
                    {
                        obj_db.objects[i].is_person = false;
                    }

                    // Update maximum AI latency
                    if (ai_time > max_ai_ms)
                        max_ai_ms = ai_time;
                }
                xSemaphoreGive(motion_mutex);
            }
        }

        // 4. Release camera framebuffer immediately
        int fb_width = fb->width;
        int fb_height = fb->height;
        esp_camera_fb_return(fb);
        int64_t t_frame_end = TICK_US();
        float frame_time = ELAPSED_MS(t_frame_start);
        // Short delay to avoid starving other tasks
        vTaskDelay(pdMS_TO_TICKS(1));

        // -------------------------------------------------------------
        // Print performance summary every 30 frames
        // -------------------------------------------------------------
        if (local_frame_counter % 30 == 0)
        {
            // Compute worst-case total latency (WCET)
            float worst_case_total_ms = max_cap_ms + max_decode_ms + max_mot_ms + max_ai_ms;

            // Guard against division by zero
            float max_theoretical_fps = 0.0f;
            if (worst_case_total_ms > 0)
            {
                max_theoretical_fps = 1000.0f / worst_case_total_ms;
            }

            // Log performance metrics in a spreadsheet-friendly format
            ESP_LOGI(TAG, "--- PERFORMANCE STATS (Framesize: %dx%d) ---", fb_width, fb_height);
            ESP_LOGI(TAG, "| Capture: %5.1f ms| true_hardware: %5.1f ms| Decode: %5.1f ms | Motion: %5.1f ms | AI: %5.1f ms | Total: %5.1f ms | Max FPS: %4.1f |",
                     max_cap_ms, true_hardware_ms, max_decode_ms, max_mot_ms, max_ai_ms, worst_case_total_ms, max_theoretical_fps);

            // Reset latency records before next logging interval
            max_cap_ms = 0.0f;
            true_hardware_ms = 0.0f;
            max_decode_ms = 0.0f;
            max_mot_ms = 0.0f;
            max_ai_ms = 0.0f;
        }
    }
}
// OLED display task
static void oled_display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OLED Display Task Started");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second before starting
    while (1)
    {
        // Acquire motion mutex
        if (xSemaphoreTake(motion_mutex, portMAX_DELAY) == pdTRUE)
        {
            oled_clear();
            oled_draw_string(3, 0, "Motion Monitor:");
            char line[32];
            // Line 2: tracked object count
            snprintf(line, sizeof(line), "Count: %d", obj_db.count);
            oled_draw_string(3, 10, line);

            // Lines 3-5: list tracked objects
            for (int i = 0; i < obj_db.count && i < 3; i++)
            {
                int w = obj_db.objects[i].max_x - obj_db.objects[i].min_x + 1;
                int h = obj_db.objects[i].max_y - obj_db.objects[i].min_y + 1;
                int area = w * h;

                char status = obj_db.objects[i].stationary ? 'S' : 'M';
                // Compact format: ID[status]:area
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
extern "C" void app_main(void)
{
    ai_init(); // Initialize AI inference engine
    printf("Free RAM: %d bytes\n", (int)esp_get_free_heap_size());
    oled_init_driver(); // Initialize OLED display driver
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
        return; // Camera startup failed

    wifi_manager_init(); // Initialize WiFi connection
    // wifi_init_softap();
    // Wait briefly for network stabilization
    vTaskDelay(pdMS_TO_TICKS(100));
    oled_draw_string(3, 20, "WiFi Connected");
    oled_update();

    start_ota_server(); // Start OTA server
    oled_draw_string(3, 30, "OTA Server Started");
    oled_update();

    // 3. Create shared resources
    frame_queue = xQueueCreate(4, sizeof(jpeg_frame_t *));
    motion_mutex = xSemaphoreCreateMutex();
    // Reserve large stack for JPEG compression if enabled
    // xTaskCreatePinnedToCore(compression_task, "compress_task", 8192, NULL, 1, &compression_task_handle, 1);
    // 4. Start Motion Task (Core 1)
    xTaskCreatePinnedToCore(motion_detection_task, "motion_task", 16384, NULL, 2, NULL, 1);

    // 5. Start HTTP Server
    start_http_server(frame_queue, motion_mutex, &obj_db);
    xTaskCreate(oled_display_task, "oled_task", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "System Started:");
}