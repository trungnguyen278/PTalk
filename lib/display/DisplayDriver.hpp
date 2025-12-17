#pragma once

#include <cstdint>
#include <string>
#include "driver/spi_master.h"

// Forward declaration
class Framebuffer;

/*
 * DisplayDriver = Raw ST7789 Driver
 * -------------------------------------------------------------
 * - Initializes SPI + ST7789 panel
 * - Exposes basic drawing primitives
 * - Provides flush(Framebuffer*) used by DisplayManager
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
        uint16_t height = 240;

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
    
    // Hold backlight pin state during deep sleep (requires RTC-capable GPIO)
    void holdBacklightDuringDeepSleep(bool enable);

    // Flush full framebuffer to screen (most common usage)
    void flush(Framebuffer* fb);

    // Drawing primitives
    void fillScreen(uint16_t color);
    void drawPixel(int x, int y, uint16_t color);
    void drawBitmap(int x, int y, int w, int h, const uint16_t* pixels);

    // Convenient helpers
    void drawText(Framebuffer* fb, const char* text, uint16_t color, int x, int y);
    void drawTextCenter(Framebuffer* fb, const char* text, uint16_t color, int cx, int cy);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }

private:
    // Low-level ST7789 commands
    void sendCommand(uint8_t cmd);
    void sendData(const uint8_t* data, size_t len);
    void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

private:
    Config cfg_;
    spi_device_handle_t spi_dev = nullptr;

    uint16_t width_  = 240;
    uint16_t height_ = 240;

    bool initialized = false;
};
