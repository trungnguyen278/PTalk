#pragma once

#include <cstdint>
#include <string>
#include "driver/spi_master.h"

/*
 * DisplayDriver = Raw ST7789 Driver
 * -------------------------------------------------------------
 * - Initializes SPI + ST7789 panel
 * - Exposes scanline-based drawing for efficient rendering
 * - No framebuffer needed - direct streaming to display
 *
 * Color format: RGB565
 */

class DisplayDriver {
public:
    struct Config {
        spi_host_device_t spi_host = SPI2_HOST;
        int pin_cs   = -1;
        int pin_dc   = -1;
        int pin_rst  = -1;
        int pin_bl   = -1;
        int pin_mosi = -1;
        int pin_sclk = -1;

        int dma_chan = 1;

        uint16_t width  = 240;
        uint16_t height = 320;

        // Some ST7789 panels require memory window offsets (e.g., 240x240 in 240x320 RAM)
        uint16_t x_offset = 0;
        uint16_t y_offset = 0;

        uint32_t spi_speed_hz = 40 * 1000 * 1000; // 40 MHz
    };

public:
    DisplayDriver();
    ~DisplayDriver();

    bool init(const Config& cfg);
    // Backlight control
    void setBacklight(bool on);
    // Set backlight brightness (0-100%). Uses LEDC PWM if pin_bl is valid.
    void setBacklightLevel(uint8_t percent);
    
    // Hold backlight pin state during deep sleep (requires RTC-capable GPIO)
    void holdBacklightDuringDeepSleep(bool enable);

    // Drawing primitives
    void fillScreen(uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);  // Fill rectangle area
    void drawPixel(int x, int y, uint16_t color);
    void drawBitmap(int x, int y, int w, int h, const uint16_t* pixels);

    // Text rendering (direct to display, no framebuffer needed)
    void drawText(const char* text, uint16_t color, int x, int y, int scale = 1);
    void drawTextCenter(const char* text, uint16_t color, int cx, int cy, int scale = 1);

    // Set address window for streaming/scanline rendering
    // x0, y0: top-left; x1, y1: bottom-right (inclusive)
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    // Write raw pixel buffer to current window (used after setWindow)
    // buffer: RGB565 pixels, len_bytes: size in bytes (width * height * 2)
    void writePixels(const uint16_t* buffer, size_t len_bytes);

    // Display rotation (0, 1, 2, 3 = 0°, 90°, 180°, 270°)
    // With automatic offset adjustment for ST7789 panels with physical offset
    // For 0°/180°: y_offset applies; For 90°/270°: x_offset applies (80px shifts to X axis)
    void setRotation(uint8_t rotation);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }

private:
    // Low-level ST7789 commands
    void sendCommand(uint8_t cmd);
    void sendData(const uint8_t* data, size_t len);
    void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    // Backlight PWM init helper
    void initBacklightPwm();

private:
    Config cfg_;
    spi_device_handle_t spi_dev = nullptr;

    uint16_t width_  = 240;
    uint16_t height_ = 320;

    uint8_t rotation_ = 0;

    // Backlight PWM state
    bool bl_pwm_ready_ = false;
    uint8_t bl_level_percent_ = 100;

    bool initialized = false;
};
