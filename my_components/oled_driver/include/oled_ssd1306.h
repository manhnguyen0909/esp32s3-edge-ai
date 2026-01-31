#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include <stdint.h>
#include "driver/i2c.h"

// --- CẤU HÌNH PHẦN CỨNG (Sửa lại nếu chân khác) ---
#define OLED_I2C_PORT       I2C_NUM_0
#define OLED_I2C_SDA        9   
#define OLED_I2C_SCL        10  
#define OLED_I2C_FREQ       400000
#define OLED_ADDR           0x3C

#define OLED_WIDTH          128
#define OLED_HEIGHT         64

// --- HÀM NGƯỜI DÙNG GỌI ---
// 1. Khởi tạo I2C và màn hình
void oled_init_driver(void);

// 2. Xóa bộ nhớ đệm (làm màn hình đen)
void oled_clear(void);

// 3. Đẩy dữ liệu từ bộ nhớ đệm ra màn hình (Cần gọi lệnh này mới thấy hình)
void oled_update(void);

// 4. Vẽ một chuỗi ký tự tại tọa độ x, y
void oled_draw_string(int x, int y, const char *str);

// 5. Vẽ pixel đơn lẻ (dùng để vẽ hình custom)
void oled_draw_pixel(int x, int y, int color);

#endif