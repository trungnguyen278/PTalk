#pragma once

#include <string>
#include <functional>
#include <vector>
#include <cstdint>

// NimBLE headers
#include "host/ble_hs.h"
#undef min
#undef max

#include "Version.hpp"

class BluetoothService
{
public:
    struct ConfigData {
        std::string device_name = "PTalk";
        uint8_t volume = 30;
        uint8_t brightness = 100;
        std::string ssid;
        std::string pass;
    };

    using OnConfigComplete = std::function<void(const ConfigData&)>;

    BluetoothService();
    ~BluetoothService();

    bool init(const std::string& adv_name);
    void start();
    void stop();
    void onConfigComplete(OnConfigComplete cb) { config_cb = cb; }

    // Callback xử lý GATT (Phải để public để struct bên ngoài truy cập được)
    static int gatt_svr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

    // UUIDs
    static constexpr uint16_t SVC_UUID_CONFIG      = 0xFF01;
    static constexpr uint16_t CHR_UUID_DEVICE_NAME = 0xFF02;
    static constexpr uint16_t CHR_UUID_VOLUME      = 0xFF03;
    static constexpr uint16_t CHR_UUID_BRIGHTNESS  = 0xFF04;
    static constexpr uint16_t CHR_UUID_WIFI_SSID   = 0xFF05;
    static constexpr uint16_t CHR_UUID_WIFI_PASS   = 0xFF06;
    static constexpr uint16_t CHR_UUID_APP_VERSION = 0xFF07;
    static constexpr uint16_t CHR_UUID_BUILD_INFO  = 0xFF08;
    static constexpr uint16_t CHR_UUID_SAVE_CMD    = 0xFF09;

private:
    static void on_stack_sync();

    static ConfigData temp_cfg;
    static OnConfigComplete config_cb;
    static std::string s_adv_name;
    bool started = false;
};