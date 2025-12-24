#pragma once
#include <cstdint>

namespace asset::emotion {

struct DiffBlock {
    uint8_t x, y;             // Top-left corner
    uint16_t width, height;   // Block dimensions (support up to 320)
    const uint8_t* data;      // 4-bit grayscale pixels (RLE encoded)
};

struct FrameInfo {
    const DiffBlock* diff;  // nullptr for frame 0 or no-change frames
};

struct Animation {
    int width;
    int height;
    int frame_count;
    int fps;
    bool loop;
    int max_packed_size;  // Max bytes needed for largest diff block (1-bit packed)
    const uint8_t* (*base_frame)();
    const FrameInfo* (*frames)();
};

} // namespace asset::emotion
