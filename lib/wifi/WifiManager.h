#pragma once

#include <string>
#include <functional>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"

/** 
 *  WifiInfo
 *  -----------------------------
 *  Cấu trúc lưu thông tin mạng WiFi
 */

struct WifiInfo {
    std::string ssid;
    int rssi;
};

/**
 *  WifiManager
 *  -----------------------------
 *  Nhiệm vụ:
 *   - Khởi tạo WiFi (NVS, Netif, Event loop)
 *   - Tự động kết nối STA nếu có cấu hình
 *   - Nếu không có cấu hình → mở SoftAP Captive Portal
 *   - Cho phép hủy AutoConnect (theo yêu cầu của bạn)
 *   - Callback khi trạng thái WiFi thay đổi
 */

class WifiManager {
public:
    static WifiManager& instance() {
    static WifiManager inst;
    return inst;
}     // Singleton

    // ==== CORE APIs ====
    void init();                         // Khởi tạo NVS + Netif + WiFi
    bool autoConnect();                  // Thử kết nối WiFi (STA)
    void startCaptivePortal();           // Bật SoftAP + HTTP portal
    void stopCaptivePortal();            // Tắt portal
    void disconnect();                   // Ngắt WiFi STA
    void disableAutoConnect();           // User chủ động tắt auto-connect

    // ==== GETTERS ====
    bool isConnected() const;
    std::string getIp() const;
    std::string getSsid() const { return sta_ssid; }
    std::string getPassword() const { return sta_pass; }

    // ==== WIFI SCAN ====
    std::vector<WifiInfo> getAvailableNetworks() {
        std::vector<WifiInfo> networks;
        WifiManager::instance().scanNetworks(networks);
        return networks;
    }

    // ==== CREDENTIALS ====
    void connectWithCredentials(const char* ssid, const char* pass) {
        saveCredentials(ssid, pass);
        startSTA();
    }

    // ==== CALLBACK ====
    // callback(status_code)
    // status: 0=DISCONNECTED, 1=CONNECTING, 2=GOT_IP
    void onStatus(std::function<void(int)> cb);

private:
    WifiManager() = default;
    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;

    // ==== Internal helpers ====
    void loadCredentials();              // Đọc SSID/PASS từ NVS
    void saveCredentials(const char* ssid, const char* pass);
    void startSTA();
    void registerEvents();               // Đăng ký event handler
    void scanNetworks(std::vector<WifiInfo>& networks);

    // ==== Event handlers ====
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data);
    static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data);

private:
    std::string sta_ssid;
    std::string sta_pass;

    bool auto_connect_enabled = true;
    bool wifi_connected       = false;
    bool portal_running       = false;

    std::function<void(int)> status_callback = nullptr;

    esp_netif_t* sta_netif = nullptr;
    esp_netif_t* ap_netif  = nullptr;
    httpd_handle_t server  = nullptr;
};

