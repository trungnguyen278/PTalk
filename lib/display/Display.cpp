#include "Display.hpp"
#include "font5x7.hpp"
#include <cstring>
#include <vector>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



static const char* TAG = "Display";

// ============================================================================
// CONSTRUCTOR
// ============================================================================

Display::Display(const DisplayConfig& config) : cfg(config) {
    // Mặc định kích thước logic bằng kích thước vật lý
    _width = cfg.width;
    _height = cfg.height;
}

Display::~Display() {
    if (panel_handle) esp_lcd_panel_del(panel_handle);
    if (io_handle) esp_lcd_panel_io_del(io_handle);
}

// ============================================================================
// INIT
// ============================================================================

void Display::init() {
    ESP_LOGI(TAG, "Initializing Display...");
    initSPI();
    initPanel();
    initBacklight();
    
    // Áp dụng góc xoay từ config
    setRotation(cfg.rotation);
    
    fill(cfg.default_bg);
}

void Display::initSPI() {
    // (Giữ nguyên code SPI cũ của bạn ở đây)
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = cfg.pin_sclk;
    buscfg.mosi_io_num = cfg.pin_mosi;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = cfg.width * cfg.height * sizeof(uint16_t) + 100;

    esp_err_t ret = spi_bus_initialize(cfg.host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
}

void Display::initPanel() {
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = cfg.pin_dc;
    io_config.cs_gpio_num = cfg.pin_cs;
    io_config.pclk_hz = cfg.spi_speed_hz;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg.host, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = cfg.pin_rst;
    panel_config.color_space = ESP_LCD_COLOR_SPACE_RGB; //panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // ST7789 thường cần đảo màu (IPS)
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

// ============================================================================
// ROTATION LOGIC
// ============================================================================

void Display::setRotation(uint8_t rotation) {
    rotation = rotation % 4;
    cfg.rotation = rotation;

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    
    // Các biến lưu khoảng lệch (gap)
    int x_gap = 0;
    int y_gap = 0;

    // Giả sử màn hình vật lý là 240x240 và RAM driver là 240x320.
    // Nếu màn hình của bạn khác, hãy chỉnh số liệu ở case tương ứng.
    switch (rotation) {
        case 0: // Portrait
            swap_xy = false;
            mirror_x = false;
            mirror_y = false;
            _width = cfg.width;
            _height = cfg.height;
            
            // Thường góc 0 chuẩn không bị lệch
            x_gap = 0; 
            y_gap = 0;
            break;

        case 1: // Landscape (90 độ)
            swap_xy = true;
            mirror_x = true;
            mirror_y = false;
            _width = cfg.height;
            _height = cfg.width;

            // Xoay 90 độ thường không bị lệch với màn 240x240
            x_gap = 0; 
            y_gap = 0;
            break;

        case 2: // Inverted Portrait (180 độ)
            swap_xy = false;
            mirror_x = true; // Lưu ý: ESP-IDF thường cần mirror X ở case này
            mirror_y = true; // Lưu ý: ESP-IDF thường cần mirror Y ở case này
            _width = cfg.width;
            _height = cfg.height;

            // Màn 240x240 trên RAM 320: Dư 80 pixel chiều dọc
            x_gap = 0; 
            y_gap = 80; 
            break;

        case 3: // Inverted Landscape (270 độ)
            swap_xy = true;
            mirror_x = false;
            mirror_y = true;
            _width = cfg.height;
            _height = cfg.width;

            // Màn 240x240 trên RAM 320: Dư 80 pixel chiều ngang (lúc này là dọc của RAM)
            x_gap = 80; 
            y_gap = 0;
            break;
    }

    ESP_LOGI(TAG, "Set Rotation: %d (Size: %dx%d) Gap: x=%d y=%d", rotation, _width, _height, x_gap, y_gap);
    
    // 1. Cài đặt hoán đổi trục và phản chiếu
    esp_lcd_panel_swap_xy(panel_handle, swap_xy);
    esp_lcd_panel_mirror(panel_handle, mirror_x, mirror_y);

    // 2. QUAN TRỌNG: Cài đặt khoảng lệch bộ nhớ (Offset)
    esp_lcd_panel_set_gap(panel_handle, x_gap, y_gap);
}
// ============================================================================
// DRAWING (Đã cập nhật để dùng _width / _height)
// ============================================================================

void Display::drawBitmapRGB565(int x, int y, int w, int h, const uint16_t* data) {
    // Clip (cắt) nếu vẽ ra ngoài màn hình
    if (x >= _width || y >= _height) return;
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;

    // x2, y2 là exclusive (không bao gồm điểm cuối)
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, data);
}

void Display::fill(uint16_t color) {
    int chunk_height = 20;
    int chunk_size = _width * chunk_height; // Dùng _width thay vì cfg.width
    std::vector<uint16_t> buffer(chunk_size, color);

    for (int y = 0; y < _height; y += chunk_height) { // Dùng _height
        int current_height = chunk_height;
        if (y + current_height > _height) {
            current_height = _height - y;
        }
        
        esp_lcd_panel_draw_bitmap(panel_handle, 
                                  0, y, 
                                  _width, y + current_height, 
                                  buffer.data());
    }
}

void Display::drawText(const std::string& text, int x, int y, int size, uint16_t color) {
    if (size < 1) size = 1;

    int cursor_x = x;
    int cursor_y = y;
    
    // Kích thước font gốc
    const int FONT_W = 5;
    const int FONT_H = 7;
    const int CHAR_SPACING = 1 * size;

    // Buffer màu nhỏ dùng để vẽ từng khối (size x size)
    // Ví dụ size=2 -> 4 pixels, size=3 -> 9 pixels.
    // Driver sẽ copy mảng nhỏ này an toàn.
    std::vector<uint16_t> block_buffer(size * size, color);

    for (char c : text) {
        if (c < 32 || c > 126) continue;
        
        // Tính kích thước thực của ký tự
        int char_w_scaled = FONT_W * size;
        int char_h_scaled = FONT_H * size;

        // Kiểm tra xuống dòng nếu tràn màn hình
        if (cursor_x + char_w_scaled > _width) {
            cursor_x = x;
            cursor_y += char_h_scaled + (2 * size);
        }
        
        // Nếu đã vẽ ra ngoài màn hình phía dưới thì dừng
        if (cursor_y + char_h_scaled > _height) break;

        const uint8_t* glyph = font5x7[c - 32];

        // Duyệt từng cột của font gốc
        for (int col = 0; col < FONT_W; col++) {
            uint8_t column_bits = glyph[col];
            
            // Duyệt từng hàng của font gốc
            for (int row = 0; row < FONT_H; row++) {
                // Nếu bit tại vị trí này bật (có pixel)
                if (column_bits & (1 << row)) {
                    
                    // Tính tọa độ trên màn hình
                    int px = cursor_x + (col * size);
                    int py = cursor_y + (row * size);

                    // Vẽ một khối hình vuông màu kích thước size*size tại đó
                    // Thay vì fill cả buffer ký tự, ta vẽ từng "pixel to"
                    drawBitmapRGB565(px, py, size, size, block_buffer.data());
                }
            }
        }

        cursor_x += char_w_scaled + CHAR_SPACING;
    }
}


// (Giữ nguyên phần Init Backlight và setBrightness như cũ)
void Display::initBacklight() {
    if (cfg.pin_bl < 0) return;
    ledc_timer_config_t ledc_timer_cfg = {};
    ledc_timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer_cfg.timer_num = cfg.ledc_timer;
    ledc_timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
    ledc_timer_cfg.freq_hz = 5000;
    ledc_timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer_cfg); // OK to call multiple times

    ledc_channel_config_t ledc_channel_cfg = {};
    ledc_channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel_cfg.channel = cfg.ledc_channel;
    ledc_channel_cfg.timer_sel = cfg.ledc_timer;
    ledc_channel_cfg.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_cfg.gpio_num = cfg.pin_bl;
    ledc_channel_cfg.duty = 0;
    ledc_channel_cfg.hpoint = 0;
    ledc_channel_config(&ledc_channel_cfg);

    setBrightness(100);
}

void Display::setBrightness(uint8_t percent) {
    if (cfg.pin_bl < 0) return;
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, cfg.ledc_channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, cfg.ledc_channel);
}

void Display::clear(uint16_t color) {
    fill(color);
}