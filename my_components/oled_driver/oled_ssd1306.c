#include "oled_ssd1306.h"
#include "font5x7.h"  // <--- Include bộ font đầy đủ mới tạo
#include "esp_log.h"
#include <string.h>

static const char *TAG = "OLED_DRIVER";

// Nếu màn hình bị lệch chữ (mất 2 cột đầu), đổi số này thành 2 (cho chip SH1106)
#define OFFSET_X  0 

static uint8_t oled_framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];

static void oled_cmd(uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd};
    i2c_master_write_to_device(OLED_I2C_PORT, OLED_ADDR, data, 2, pdMS_TO_TICKS(100));
}

static void oled_data(uint8_t *data, size_t len) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x40, true);
    i2c_master_write(h, data, len, true);
    i2c_master_stop(h);
    i2c_master_cmd_begin(OLED_I2C_PORT, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}

void oled_init_driver(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_I2C_SDA,
        .scl_io_num = OLED_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ
    };
    i2c_param_config(OLED_I2C_PORT, &conf);
    i2c_driver_install(OLED_I2C_PORT, conf.mode, 0, 0, 0);

    // Sequence khởi tạo chuẩn, NHƯNG chưa bật màn hình ngay
    oled_cmd(0xAE); // Display OFF (Tắt trước để tránh nhiễu)
    oled_cmd(0x20); oled_cmd(0x00); // Horizontal addressing mode
    oled_cmd(0xB0);
    oled_cmd(0xC8);
    oled_cmd(0x00);
    oled_cmd(0x10);
    oled_cmd(0x40);
    oled_cmd(0x81); oled_cmd(0x7F); // Contrast
    oled_cmd(0xA1);
    oled_cmd(0xA6);
    oled_cmd(0xA8); oled_cmd(0x3F);
    oled_cmd(0xA4);
    oled_cmd(0xD3); oled_cmd(0x00);
    oled_cmd(0xD5); oled_cmd(0x80);
    oled_cmd(0xD9); oled_cmd(0xF1);
    oled_cmd(0xDA); oled_cmd(0x12);
    oled_cmd(0xDB); oled_cmd(0x40);
    oled_cmd(0x8D); oled_cmd(0x14);

    // [QUAN TRỌNG] Xóa sạch RAM rác trước khi bật màn hình
    oled_clear();
    oled_update(); 

    // Bây giờ mới bật -> Sẽ sạch bong
    oled_cmd(0xAF); // Display ON
    
    ESP_LOGI(TAG, "OLED Initialized & Cleared");
}

void oled_clear(void) {
    memset(oled_framebuffer, 0x00, sizeof(oled_framebuffer));
}

void oled_update(void) {
    for (uint8_t page = 0; page < 8; page++) {
        oled_cmd(0xB0 + page); 
        oled_cmd(0x00 + OFFSET_X); // Áp dụng Offset nếu là chip SH1106
        oled_cmd(0x10);        
        oled_data(&oled_framebuffer[OLED_WIDTH * page], OLED_WIDTH);
    }
}

void oled_draw_pixel(int x, int y, int color) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int index = x + (y / 8) * OLED_WIDTH;
    if (color)
        oled_framebuffer[index] |= (1 << (y % 8));
    else
        oled_framebuffer[index] &= ~(1 << (y % 8));
}

// Hàm vẽ ký tự đã sửa để dùng font5x7 đầy đủ
static void oled_draw_char(int x, int y, char c) {
    if (c < 32 || c > 127) c = '?'; // Fallback nếu ký tự lạ
    
    // Map vào bảng font (c - 32)
    const uint8_t *glyph = font5x7[c - 32]; 

    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                oled_draw_pixel(x + col, y + row, 1);
            }
        }
    }
}

void oled_draw_string(int x, int y, const char *str) {
    while (*str) {
        oled_draw_char(x, y, *str++);
        x += 6; // 5 pixel font + 1 pixel khoảng cách
        if (x > OLED_WIDTH - 6) break;
    }
}