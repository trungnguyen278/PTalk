// File: Version.hpp
#pragma once
#include "esp_system.h"
#include <string>
#include <sstream>
#include <iomanip>
namespace app_meta {
    static constexpr const char* APP_VERSION = "1.0.5";
    static constexpr const char* DEVICE_MODEL = "PTalk-V1";
    static constexpr const char* BUILD_DATE = __DATE__; // Tự động lấy ngày biên dịch
}


inline uint64_t getEfuseMac()
{
    uint64_t mac;
    esp_efuse_mac_get_default((uint8_t*)&mac);
    return mac;
}

inline std::string getDeviceEfuseID()
{
    uint64_t mac = getEfuseMac();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(2) << ((mac >> 40) & 0xFF)
        << std::setw(2) << ((mac >> 32) & 0xFF)
        << std::setw(2) << ((mac >> 24) & 0xFF)
        << std::setw(2) << ((mac >> 16) & 0xFF)
        << std::setw(2) << ((mac >> 8)  & 0xFF)
        << std::setw(2) << ( mac        & 0xFF);

    return oss.str(); // ví dụ: "24a160ff3b9c"
}

/**
 * @brief Get device MAC address as 6-byte array
 * Uses same efuse source as getDeviceEfuseID() for consistency
 * @param out_mac Output array of 6 bytes
 */
inline void getDeviceMacBytes(uint8_t out_mac[6])
{
    uint64_t mac = getEfuseMac();
    out_mac[0] = (mac >> 40) & 0xFF;
    out_mac[1] = (mac >> 32) & 0xFF;
    out_mac[2] = (mac >> 24) & 0xFF;
    out_mac[3] = (mac >> 16) & 0xFF;
    out_mac[4] = (mac >> 8) & 0xFF;
    out_mac[5] = mac & 0xFF;
}