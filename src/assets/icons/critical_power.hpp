#pragma once
#include <cstdint>

namespace asset::icon {

#ifndef ICON
#define ICON
struct Icon {
    int w;
    int h;
    const uint8_t* rle_data;
};
#endif

extern const Icon CRITICAL_POWER;

} // namespace asset::icon
