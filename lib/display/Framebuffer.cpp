// Framebuffer.cpp
#include "Framebuffer.hpp"
#include <new>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "Font8x8.hpp"





// Helper convert functions
static inline void rgb565_to_rgb888(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (uint8_t)((c >> 11) & 0x1F);
    g = (uint8_t)((c >> 5) & 0x3F);
    b = (uint8_t)(c & 0x1F);

    // expand to 8-bit
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
}

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t rr = (r >> 3) & 0x1F;
    uint16_t gg = (g >> 2) & 0x3F;
    uint16_t bb = (b >> 3) & 0x1F;
    return (uint16_t)((rr << 11) | (gg << 5) | bb);
}

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height), pixels_(nullptr)
{
    assert(width_ > 0 && height_ > 0);
    // allocate continuous buffer
    size_t bytes = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(uint16_t);
    pixels_ = static_cast<uint16_t*>(malloc(bytes));
    if (pixels_) {
        // default clear to black
        clear(0x0000);
    }
    
}

Framebuffer::~Framebuffer()
{
    if (pixels_) {
        free(pixels_);
        pixels_ = nullptr;
    }
}

void Framebuffer::clear(uint16_t color)
{
    if (!pixels_) return;
    size_t pixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    // fill 16-bit words
    for (size_t i = 0; i < pixels; ++i) {
        pixels_[i] = color;
    }
}

void Framebuffer::drawPixel(int x, int y, uint16_t color)
{
    if (!pixels_) return;
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    pixels_[y * width_ + x] = color;
}

void Framebuffer::drawBitmap(int x, int y, int w, int h, const uint16_t* src)
{
    if (!pixels_ || !src) return;
    if (w <= 0 || h <= 0) return;

    // clip
    int sx = 0, sy = 0;
    int dx = x, dy = y;
    if (dx < 0) { sx = -dx; dx = 0; }
    if (dy < 0) { sy = -dy; dy = 0; }

    int draw_w = w - sx;
    int draw_h = h - sy;
    if (dx + draw_w > width_)  draw_w = width_ - dx;
    if (dy + draw_h > height_) draw_h = height_ - dy;
    if (draw_w <= 0 || draw_h <= 0) return;

    for (int row = 0; row < draw_h; ++row) {
        const uint16_t* srow = src + (sy + row) * w + sx;
        uint16_t* dst = pixels_ + (dy + row) * width_ + dx;
        memcpy(dst, srow, draw_w * sizeof(uint16_t));
    }
}

void Framebuffer::drawBitmapAlpha(
    int x, int y,
    int w, int h,
    const uint16_t* rgb,
    const uint8_t* alpha)
{
    if (!pixels_ || !rgb || !alpha) return;
    if (w <= 0 || h <= 0) return;

    // clipping similar to drawBitmap
    int sx = 0, sy = 0;
    int dx = x, dy = y;
    if (dx < 0) { sx = -dx; dx = 0; }
    if (dy < 0) { sy = -dy; dy = 0; }

    int draw_w = w - sx;
    int draw_h = h - sy;
    if (dx + draw_w > width_)  draw_w = width_ - dx;
    if (dy + draw_h > height_) draw_h = height_ - dy;
    if (draw_w <= 0 || draw_h <= 0) return;

    for (int row = 0; row < draw_h; ++row) {
        const uint16_t* srow = rgb + (sy + row) * w + sx;
        const uint8_t* arow = alpha + (sy + row) * w + sx;
        uint16_t* dst = pixels_ + (dy + row) * width_ + dx;

        for (int col = 0; col < draw_w; ++col) {
            uint8_t sa = arow[col]; // 0..255
            if (sa == 0xFF) {
                // fully opaque
                dst[col] = srow[col];
            } else if (sa == 0x00) {
                // fully transparent: do nothing
            } else {
                uint8_t sr, sg, sb;
                uint8_t dr, dg, db;
                rgb565_to_rgb888(srow[col], sr, sg, sb);
                rgb565_to_rgb888(dst[col], dr, dg, db);

                uint16_t r = (uint16_t)((sr * sa + dr * (255 - sa)) / 255);
                uint16_t g = (uint16_t)((sg * sa + dg * (255 - sa)) / 255);
                uint16_t b = (uint16_t)((sb * sa + db * (255 - sa)) / 255);

                dst[col] = rgb888_to_rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
            }
        }
    }
}

void Framebuffer::drawText8x8(int x, int y, const char* text, uint16_t color)
{
    if (!pixels_ || !text) return;

    int cx = x;
    const char* p = text;
    while (*p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c > 127) {
            // unknown: advance by 8
            cx += 8;
            ++p;
            continue;
        }
        const uint8_t* glyph = FONT8x8[c - 32];


        for (int row = 0; row < 8; ++row) {
            uint8_t bits = glyph[row];
            int yy = y + row;
            if (yy < 0 || yy >= height_) continue;
            for (int col = 0; col < 8; ++col) {
                int xx = cx + col;
                if (xx < 0 || xx >= width_) continue;
                if (bits & (1 << (7 - col))) {
                    pixels_[yy * width_ + xx] = color;
                }
            }
        }

        cx += 8;
        ++p;
    }
}
