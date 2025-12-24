#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

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
    int max_packed_size = 0;                                // Max bytes for largest diff block
    const uint8_t* base_frame = nullptr;                    // Frame 0 full 1-bit bitmap
    const asset::emotion::FrameInfo* frames = nullptr;       // Array of frame infos

    bool valid() const { 
        // Support animations with diff-only frame 0 (no base_frame)
        return width > 0 && height > 0 && frame_count > 0 && 
               frames != nullptr; 
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
    AnimationPlayer(class DisplayDriver* drv);
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

    // Render frame directly to display (no framebuffer needed)
    void render();

private:
    // Decode RLE directly to RGB565 scanline buffer (no intermediate packed buffer)
    void decodeRLEScanline(const uint8_t* rle_data, int start_y, int num_rows, uint16_t* out_buffer);
    
    // Deprecated functions (kept for compatibility)
    void decode1BitToRGB565(const uint8_t* packed_data, int width, int height);
    void decodeFullRLEFrame(const asset::emotion::DiffBlock* block);
    void applyDiffBlock(const asset::emotion::DiffBlock* diff);

    DisplayDriver* drv_ = nullptr;

    Animation1Bit current_anim_;
    int pos_x_ = 0;
    int pos_y_ = 0;

    // Streaming scanline buffer: decode RLE directly to RGB565 scanline
    // (8 rows × width pixels × 2 bytes/pixel = 8 × 320 × 2 = 5120 bytes max)
    static constexpr int SCANLINE_ROWS = 8;
    uint16_t* scanline_buffer_ = nullptr;
    size_t scanline_buf_size_ = 0;

    // No packed frame buffer needed - decode RLE directly to scanline!

    uint32_t frame_timer_ = 0;   // tăng theo dt
    uint32_t frame_interval_ = 50; // ms/frame (20 fps)

    size_t frame_index_ = 0;

    bool paused_ = false;
    bool playing_ = false;
};
