#pragma once
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "DisplaySpiConfig.hpp"

// ============================================================================
//  DisplayDriver: driver mức thấp cho ST7789 (raw SPI, event-friendly)
// ============================================================================
//
//  Nhiệm vụ:
//   - Init SPI + GPIO + panel ST7789 (mã lệnh trong .cpp)
//   - Cung cấp API tối thiểu:
//       + init()
//       + setRotation()
//       + setWindow(x0,y0,x1,y1)
//       + pushPixels(data, count)
//       + fill(), drawBitmap(), drawScanline() tiện dùng cho layer cao
//
//  Không chứa:
//   - Logic UI
//   - Emotion, animation
//   - Random idle
// ============================================================================
class DisplayDriver {
public:
    explicit DisplayDriver(const DisplaySpiConfig& cfg);
    ~DisplayDriver();

    // Khởi tạo SPI bus + LCD (gọi 1 lần ở app_main)
    esp_err_t init();

    // Reset cứng panel (tùy config pin_rst)
    void hwReset();

    // Đổi hướng xoay (0..3), sẽ set lại MADCTL + width/height logic
    void setRotation(uint8_t rotation);

    // Độ sáng backlight (0..100), chỉ hoạt động nếu pin_bl hợp lệ
    void setBrightness(uint8_t percent);

    // ========================================================================
    //  API THẤP NHẤT DÙNG CHO RENDERING
    // ========================================================================

    // Chỉ định vùng vẽ trên LCD (tọa độ tuyệt đối, đã tính rotation/offset)
    // Thường gọi 1 lần trước khi pushPixels nhiều lần (streaming)
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    // Gửi một chuỗi pixel 16-bit RGB565 (đã setWindow trước đó)
    // count = số pixel, chứ không phải số byte
    void pushPixels(const uint16_t* data, size_t count);

    // Tiện ích: đổ một màu lặp lại n pixel (không bắt buộc phải dùng)
    void pushColor(uint16_t color, size_t count);

    // ========================================================================
    //  TIỆN ÍCH VẼ CƠ BẢN (DÀNH CHO LAYER CAO DÙNG TRỰC TIẾP)
    // ========================================================================

    // Fill toàn màn 1 màu (dùng setWindow + pushColor())
    void fill(uint16_t color);

    // Vẽ một ảnh RGB565 (w x h) tại (x,y)
    void drawBitmap(uint16_t x,
                    uint16_t y,
                    uint16_t w,
                    uint16_t h,
                    const uint16_t* pixels);

    // Vẽ một scanline ngang (width pixel) tại dòng y
    void drawScanline(uint16_t y,
                      const uint16_t* pixels,
                      uint16_t width);

    // Getter kích thước logic
    inline uint16_t width()  const { return _width;  }
    inline uint16_t height() const { return _height; }

private:
    // Nội bộ: khởi tạo SPI bus & device handle
    esp_err_t initSpiBus();
    esp_err_t initSpiDevice();
    esp_err_t initPanelST7789();
    void      initBacklight();

    // Gửi command / data raw cho panel
    void sendCommand(uint8_t cmd);
    void sendData(const void* data, size_t len);

    // Gửi 1 byte data helper
    inline void sendData8(uint8_t data) {
        sendData(&data, 1);
    }

    // Thiết lập MADCTL (orientation, RGB/BGR, mirror...)
    void updateMadctl();

private:
    DisplaySpiConfig cfg;

    spi_device_handle_t spi_dev = nullptr;

    uint16_t _width  = 0;   // kích thước logic hiện tại (thay đổi theo rotation)
    uint16_t _height = 0;

    uint8_t  _rotation = 0;
};