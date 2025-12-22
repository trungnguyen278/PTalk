#include "AnimationPlayer.hpp"
#include "DisplayDriver.hpp"
#include "esp_log.h"
#include <cstring>
#include "assets/emotions/emotion_types.hpp"

static const char* TAG = "AnimationPlayer";

// RGB565 colors for black and white
static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_WHITE = 0xFFFF;

AnimationPlayer::AnimationPlayer(Framebuffer* fb, DisplayDriver* drv)
    : fb_(fb), drv_(drv)
{
    if (!fb_ || !drv_) {
        ESP_LOGE(TAG, "AnimationPlayer created with null fb or driver!");
    }
}

AnimationPlayer::~AnimationPlayer()
{
    if (scanline_buffer_) {
        free(scanline_buffer_);
        scanline_buffer_ = nullptr;
    }
    if (packed_frame_) {
        free(packed_frame_);
        packed_frame_ = nullptr;
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

    // Allocate packed frame buffer (1-bit: width*height/8 bytes)
    size_t packed_size = (anim.width * anim.height + 7) / 8;
    if (packed_size != packed_frame_size_) {
        if (packed_frame_) free(packed_frame_);
        packed_frame_ = (uint8_t*)malloc(packed_size);
        packed_frame_size_ = packed_size;
        if (!packed_frame_) {
            ESP_LOGE(TAG, "Failed to allocate packed frame buffer (%zu bytes)", packed_size);
            stop();
            return;
        }
    }

    // Copy base frame (frame 0) into packed buffer
    if (anim.base_frame) {
        memcpy(packed_frame_, anim.base_frame, packed_size);
    }

    ESP_LOGI(TAG, "Animation set: %d frames (%dx%d), fps=%u, loop=%s | scanline=%zuB, packed=%zuB",
             anim.frame_count, anim.width, anim.height, anim.fps,
             anim.loop ? "true" : "false", scanline_size, packed_size);
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
    if (!playing_ || paused_ || !current_anim_.valid() || !packed_frame_)
        return;

    frame_timer_ += dt_ms;

    // Update frame index & apply diffs
    while (frame_timer_ >= frame_interval_) {
        frame_timer_ -= frame_interval_;
        frame_index_++;

        if (frame_index_ >= (size_t)current_anim_.frame_count) {
            if (current_anim_.loop) {
                // Loop back to frame 0
                frame_index_ = 0;
                memcpy(packed_frame_, current_anim_.base_frame, packed_frame_size_);
            } else {
                // one-shot animation stops
                frame_index_ = current_anim_.frame_count - 1;
                playing_ = false;
                break;
            }
        } else {
            // Apply diff for new frame
            const asset::emotion::FrameInfo& frame_info = current_anim_.frames[frame_index_];
            if (frame_info.diff != nullptr) {
                applyDiffBlock(frame_info.diff);
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

void AnimationPlayer::applyDiffBlock(const asset::emotion::DiffBlock* diff)
{
    if (!diff || !diff->data || !packed_frame_) return;

    int anim_width = current_anim_.width;
    
    for (int dy = 0; dy < diff->height; dy++) {
        for (int dx = 0; dx < diff->width; dx++) {
            int bit_index = dy * diff->width + dx;
            int byte_index = bit_index / 8;
            int bit_offset = 7 - (bit_index % 8); // MSB first
            
            bool is_white = (diff->data[byte_index] >> bit_offset) & 1;
            
            int px = diff->x + dx;
            int py = diff->y + dy;
            
            if (px >= 0 && px < anim_width && py >= 0 && py < current_anim_.height) {
                // Update packed frame bit
                int target_bit = py * anim_width + px;
                int target_byte = target_bit / 8;
                int target_offset = 7 - (target_bit % 8);
                
                if (is_white) {
                    packed_frame_[target_byte] |= (1u << target_offset);
                } else {
                    packed_frame_[target_byte] &= ~(1u << target_offset);
                }
            }
        }
    }
}

void AnimationPlayer::render()
{
    if (!playing_ || !fb_ || !current_anim_.valid() || !packed_frame_ || !scanline_buffer_) 
        return;

    int w = current_anim_.width;
    int h = current_anim_.height;

    // Render scanline-by-scanline into Framebuffer (no direct LCD writes)
    // DisplayManager will call drv_->flush(fb) once per frame → no flickering
    for (int y = 0; y < h; y += SCANLINE_ROWS) {
        int rows_in_batch = (y + SCANLINE_ROWS > h) ? (h - y) : SCANLINE_ROWS;
        
        // Decode batch of rows from packed_frame_ into scanline_buffer_
        for (int row = 0; row < rows_in_batch; row++) {
            int py = y + row;
            for (int x = 0; x < w; x++) {
                int bit_index = py * w + x;
                int byte_index = bit_index / 8;
                int bit_offset = 7 - (bit_index % 8);
                bool is_white = (packed_frame_[byte_index] >> bit_offset) & 1;
                scanline_buffer_[row * w + x] = is_white ? COLOR_WHITE : COLOR_BLACK;
            }
        }

        // Draw scanline batch into Framebuffer
        fb_->drawBitmap(pos_x_, pos_y_ + y, w, rows_in_batch, scanline_buffer_);
    }
}
