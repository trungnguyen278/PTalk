#pragma once

#include <string>
#include <vector>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"


#include "DisplayTypes.hpp"
/*
===============================================================================
 Display Class
===============================================================================
    Nhiệm vụ của Display:
    1. Khởi tạo và cấu hình màn hình LCD qua SPI
    2. Cung cấp các hàm vẽ cơ bản: clear, fill, drawText, drawBitmapRGB565, drawScanline
    3. Quản lý độ sáng qua PWM
    4. Hỗ trợ xoay màn hình (0-3) và điều chỉnh kích thước logic tương ứng
    5. Đóng gói các handle ESP-IDF liên quan đến LCD và SPI
    6. Dễ dàng tích hợp với các module khác như DisplayAnimator
    7. Tối ưu hóa hiệu năng vẽ qua SPI
===============================================================================
*/


class Display {
public:
    explicit Display(const DisplayConfig& cfg);
    ~Display();

    void init();
    void setBrightness(uint8_t percent);
    
    // Hàm thiết lập góc xoay (0-3)
    void setRotation(uint8_t rotation);

    // Get kích thước hiện tại (Logic size)
    int width() const { return _width; }
    int height() const { return _height; }

    // Drawing
    void clear(uint16_t color = 0x0000);
    void fill(uint16_t color = 0x0000);
    void drawText(const std::string& text, int x, int y, int size = 2, uint16_t color = 0xFFFF);
    void drawBitmapRGB565(int x, int y, int w, int h, const uint16_t* data);
    void drawScanline(int y, const uint16_t* data, int width);
   

private:
    DisplayConfig cfg;
    
    // Kích thước logic hiện tại (thay đổi khi xoay)
    int _width;
    int _height;

    // ESP-IDF handles
    spi_device_handle_t spi_handle = nullptr;
    esp_lcd_panel_io_handle_t io_handle = nullptr;
    esp_lcd_panel_handle_t panel_handle = nullptr;

    void initSPI();
    void initPanel();
    void initBacklight();
};