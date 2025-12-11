#pragma once

#include <cstdint>
#include <cstring>
#include <string>

/*
 * Framebuffer
 * -------------------------------------------------------------
 * - 16-bit RGB565 pixel buffer
 * - Used by DisplayManager and AnimationPlayer
 * - Draw functions manipulate the buffer, DisplayDriver flushes it
 */

class Framebuffer {
public:
    Framebuffer(int width, int height);
    ~Framebuffer();

    inline int width() const  { return width_; }
    inline int height() const { return height_; }

    // Direct buffer access
    inline uint16_t* data() { return pixels_; }

    // Clear entire screen
    void clear(uint16_t color = 0x0000);

    // Basic drawing
    void drawPixel(int x, int y, uint16_t color);

    // Draw raw bitmap (RGB565)
    void drawBitmap(int x, int y, int w, int h, const uint16_t* src);

    // Draw alpha-blended bitmap (A8 mask + RGB565 data)
    void drawBitmapAlpha(
        int x, int y,
        int w, int h,
        const uint16_t* rgb,
        const uint8_t* alpha
    );

    // Simple 8x8 bitmap font (used for toast)
    void drawText8x8(int x, int y, const char* text, uint16_t color);

private:
    int width_;
    int height_;
    uint16_t* pixels_ = nullptr;
};
