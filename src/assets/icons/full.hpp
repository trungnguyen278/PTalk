#pragma once
#include <cstdint>

namespace asset::icon {

struct Icon {
    int w;
    int h;
    const uint8_t* rle_data;
};

extern const Icon FULL;

} // namespace asset::icon
