#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "Framebuffer.hpp"

// Forward declare emotion types
namespace asset { namespace emotion {
    struct DiffBlock;
    struct FrameInfo;
}}

/*
 * Animation1Bit
 * ---------------------------------------------------------
 * 1-bit black/white animation with diff encoding:
 *  - Frame 0: full 1-bit bitmap (width * height / 8 bytes)
 *  - Frame 1+: single DiffBlock per frame (bounding box + packed data)
 */
struct Animation1Bit {
    int width = 0;
    int height = 0;
    int frame_count = 0;
    uint16_t fps = 20;
    bool loop = true;
    const uint8_t* base_frame = nullptr;                    // Frame 0 full 1-bit bitmap
    const asset::emotion::FrameInfo* frames = nullptr;       // Array of frame infos

    bool valid() const { 
        return width > 0 && height > 0 && frame_count > 0 && 
               base_frame != nullptr && frames != nullptr; 
    }
};


/*
 * AnimationPlayer
 * ---------------------------------------------------------
 * Plays 1-bit black/white animations with diff encoding:
 *  - Decodes base frame (1-bit) to RGB565 working buffer
 *  - Applies diff blocks for subsequent frames
 *  - Renders to framebuffer at specified position
 *
 * Note: Không dùng timer riêng — DisplayManager sẽ gọi update().
 */

class AnimationPlayer {
public:
    AnimationPlayer(Framebuffer* fb, class DisplayDriver* drv);
    ~AnimationPlayer();

    // Set animation mới (1-bit format)
    void setAnimation(const Animation1Bit& anim, int x = 0, int y = 0);

    // Stop (không vẽ gì)
    void stop();

    // Resume/Pause
    void pause();
    void resume();
    bool isPaused() const { return paused_; }

    // Update theo thời gian (ms)
    void update(uint32_t dt_ms);

    // Vẽ frame hiện tại vào framebuffer
    void render();

private:
    // Decode 1-bit packed data to RGB565 working buffer
    void decode1BitToRGB565(const uint8_t* packed_data, int width, int height);
    
    // Apply diff block to current working buffer
    void applyDiffBlock(const asset::emotion::DiffBlock* diff);

    Framebuffer* fb_ = nullptr;
    DisplayDriver* drv_ = nullptr;

    Animation1Bit current_anim_;
    int pos_x_ = 0;
    int pos_y_ = 0;

    // Streaming scanline buffer: decode+render hàng một
    // (8 rows × width pixels × 2 bytes/pixel = 8 × 240 × 2 = 3840 bytes max)
    static constexpr int SCANLINE_ROWS = 8;
    uint16_t* scanline_buffer_ = nullptr;
    size_t scanline_buf_size_ = 0;

    // Packed 1-bit frame buffer (not decoded; only ~6–8 KB)
    uint8_t* packed_frame_ = nullptr;
    size_t packed_frame_size_ = 0;

    uint32_t frame_timer_ = 0;   // tăng theo dt
    uint32_t frame_interval_ = 50; // ms/frame (20 fps)

    size_t frame_index_ = 0;

    bool paused_ = false;
    bool playing_ = false;
};
