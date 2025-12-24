#include "AnimationPlayer.hpp"
#include "DisplayDriver.hpp"
#include "esp_log.h"
#include <cstring>
#include "assets/emotions/emotion_types.hpp"

static const char* TAG = "AnimationPlayer";

// RGB565 colors for black and white
static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_WHITE = 0xFFFF;

AnimationPlayer::AnimationPlayer(DisplayDriver* drv)
    : drv_(drv)
{
    if (!drv_) {
        ESP_LOGE(TAG, "AnimationPlayer created with null driver!");
    }
}

AnimationPlayer::~AnimationPlayer()
{
    if (scanline_buffer_) {
        free(scanline_buffer_);
        scanline_buffer_ = nullptr;
    }
}

void AnimationPlayer::setAnimation(const Animation1Bit& anim, int x, int y)
{
    if (!anim.valid()) {
        ESP_LOGW(TAG, "setAnimation: invalid animation");
        stop();
        return;
    }

    current_anim_ = anim;
    pos_x_ = x;
    pos_y_ = y;

    frame_index_ = 0;
    frame_timer_ = 0;
    paused_ = false;
    playing_ = true;

    if (anim.fps == 0) {
        frame_interval_ = 50;  // fallback 20 fps
    } else {
        frame_interval_ = 1000 / anim.fps;
    }

    // Allocate scanline buffer for streaming (8 rows max)
    size_t scanline_size = SCANLINE_ROWS * anim.width * sizeof(uint16_t);
    if (scanline_size > scanline_buf_size_) {
        if (scanline_buffer_) free(scanline_buffer_);
        scanline_buffer_ = (uint16_t*)malloc(scanline_size);
        scanline_buf_size_ = scanline_size;
        if (!scanline_buffer_) {
            ESP_LOGE(TAG, "Failed to allocate scanline buffer (%zu bytes)", scanline_size);
            stop();
            return;
        }
    }

    ESP_LOGI(TAG, "Animation set: %d frames (%dx%d), fps=%u, loop=%s | scanline=%zuB",
             anim.frame_count, anim.width, anim.height, anim.fps,
             anim.loop ? "true" : "false", scanline_size);
}

void AnimationPlayer::stop()
{
    playing_ = false;
    paused_  = false;
    frame_index_ = 0;
    frame_timer_ = 0;
}

void AnimationPlayer::pause()
{
    paused_ = true;
}

void AnimationPlayer::resume()
{
    paused_ = false;
}

void AnimationPlayer::update(uint32_t dt_ms)
{
    if (!playing_ || paused_ || !current_anim_.valid())
        return;

    frame_timer_ += dt_ms;

    // Update frame index
    while (frame_timer_ >= frame_interval_) {
        frame_timer_ -= frame_interval_;
        frame_index_++;

        if (frame_index_ >= (size_t)current_anim_.frame_count) {
            if (current_anim_.loop) {
                frame_index_ = 0;
            } else {
                frame_index_ = current_anim_.frame_count - 1;
                playing_ = false;
                break;
            }
        }
    }
}

void AnimationPlayer::decode1BitToRGB565(const uint8_t* packed_data, int width, int height)
{
    // This method is no longer used in streaming mode.
    // Kept for backward compatibility if needed.
    (void)packed_data;
    (void)width;
    (void)height;
}

void AnimationPlayer::decodeRLEScanline(const uint8_t* rle_data, int start_y, int num_rows, uint16_t* out_buffer)
{
    if (!rle_data || !out_buffer) return;

    int w = current_anim_.width;
    int h = current_anim_.height;
    int start_pixel = start_y * w;
    int end_pixel = (start_y + num_rows) * w;
    if (end_pixel > w * h) end_pixel = w * h;

    const uint8_t* src = rle_data;
    int px = 0;  // Current pixel index in frame
    int out_idx = 0;  // Output buffer index

    // RLE decode: [count, value] (2-bit grayscale, 4 levels)
    while (px < w * h) {
        uint8_t count = *src++;
        uint8_t value = *src++;
        if (count == 0) break;

        uint8_t gray2 = value & 0x03; // 2-bit value (0-3)
        uint8_t gray = gray2 * 85; // 0, 85, 170, 255
        uint16_t color = ((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3); // RGB565

        for (uint8_t i = 0; i < count && px < end_pixel; i++, px++) {
            if (px >= start_pixel) {
                out_buffer[out_idx++] = color;
            }
        }
        if (px >= end_pixel) break;
    }
}

void AnimationPlayer::decodeFullRLEFrame(const asset::emotion::DiffBlock* block)
{
    // Deprecated - no longer used
}

void AnimationPlayer::applyDiffBlock(const asset::emotion::DiffBlock* diff)
{
    // Deprecated - no longer used
}

void AnimationPlayer::render()
{
    if (!playing_ || !drv_ || !current_anim_.valid() || !scanline_buffer_) 
        return;

    // Get current frame's RLE data
    const asset::emotion::FrameInfo& frame_info = current_anim_.frames[frame_index_];
    if (!frame_info.diff || !frame_info.diff->data) return;

    int w = current_anim_.width;
    int h = current_anim_.height;

    // Set window once for entire animation frame
    drv_->setWindow(pos_x_, pos_y_, pos_x_ + w - 1, pos_y_ + h - 1);

    // Stream scanline-by-scanline: decode RLE directly to RGB565 and write
    for (int y = 0; y < h; y += SCANLINE_ROWS) {
        int rows_in_batch = (y + SCANLINE_ROWS > h) ? (h - y) : SCANLINE_ROWS;
        
        // Decode this scanline batch from RLE directly to RGB565
        decodeRLEScanline(frame_info.diff->data, y, rows_in_batch, scanline_buffer_);

        // Write scanline batch directly to display (no framebuffer!)
        drv_->writePixels(scanline_buffer_, w * rows_in_batch * sizeof(uint16_t));
    }
}
