#pragma once
#include <cstdint>

namespace asset::emotion {

struct DiffBlock {
    uint8_t x, y;           // Top-left corner
    uint8_t width, height;  // Block dimensions
    const uint8_t* data;    // 1-bit packed pixels
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
    const uint8_t* (*base_frame)();
    const FrameInfo* (*frames)();
};

} // namespace asset::emotion
