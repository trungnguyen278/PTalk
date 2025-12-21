#pragma once
#include <memory>
#include <string>
#include <atomic>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/StateTypes.hpp"
#include "system/StateManager.hpp"

class WifiService;         // Low-level WiFi
class WebSocketClient;     // Low-level WebSocket

/**
 * NetworkManager
 * -------------------------------------------------------------------
 * Nhiệm vụ:
 *  - Điều phối WiFi → WebSocket
 *  - Publish ConnectivityState lên StateManager
 *  - Cầu nối WebSocket ↔ AppController (nhận text/binary message)
 *  - Quyết định retry logic của WebSocket (không để trong WS driver)
 * 
 * Không làm:
 *  - Không scan wifi
 *  - Không xử lý portal HTML
 *  - Không chứa logic kết nối driver-level
 */
class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    // ======================================================
    // Configuration
    // ======================================================
    struct Config {
        // Wi‑Fi station credentials (optional). If empty → use saved or portal
        std::string sta_ssid;            // target Wi‑Fi SSID
        std::string sta_pass;            // target Wi‑Fi password

        // Captive portal (AP) fallback settings
        std::string ap_ssid = "PTalk";  // AP SSID when opening portal
        uint8_t     ap_max_clients = 4;  // limit number of AP clients

        // WebSocket server endpoint
        std::string ws_url;              // e.g. ws://192.168.1.100:8080/ws
    };

    // ======================================================
    // INIT / START / STOP
    // ======================================================
    bool init();
    // Init with configuration (preferred)
    bool init(const Config& cfg);
    void start();
    void stop();

    // ======================================================
    // External API
    // ======================================================

    /// Cập nhật mỗi chu kỳ AppController loop
    void update(uint32_t dt_ms);

    /// Gán lại credentials khi user submit portal
    void setCredentials(const std::string& ssid, const std::string& pass);

    // Runtime config setters (optional, can be used before start)
    void setWsUrl(const std::string& url);
    void setApSsid(const std::string& apSsid);
    void setDeviceLimit(uint8_t maxClients);

    /// Gửi message lên server
    bool sendText(const std::string& text);
    bool sendBinary(const uint8_t* data, size_t len);

    /// Callback khi server gửi text message
    void onServerText(std::function<void(const std::string&)> cb);

    /// Callback khi server gửi binary
    void onServerBinary(std::function<void(const uint8_t*, size_t)> cb);
    
    /// Callback khi WebSocket disconnect (để flush buffer/reset state)
    void onDisconnect(std::function<void()> cb);
    
    /// Set WS immune mode - when true, prevents WS from closing on WiFi fluctuations
    /// Use during critical operations like audio streaming
    void setWSImmuneMode(bool immune);
    
    /// Check if currently in active speaking session
    bool isSpeakingSessionActive() const { return speaking_session_active; }
    
    /// Mark start of speaking session (prevents SPEAKING state spam)
    void startSpeakingSession() { speaking_session_active = true; }
    
    /// Mark end of speaking session (allows next TTS to trigger SPEAKING)
    void endSpeakingSession() { speaking_session_active = false; }

    // ======================================================
    // OTA Firmware Update Support
    // ======================================================
    /**
     * Request firmware update from server
     * Server should respond with binary firmware data
     * @param version Version to request (optional, "" = latest)
     * @return true if request sent
     */
    bool requestFirmwareUpdate(const std::string& version = "");

    /**
     * Callback when firmware data chunk received from server
     * Used internally during OTA download
     */
    void onFirmwareChunk(std::function<void(const uint8_t*, size_t)> cb);

    /**
     * Callback when firmware download completes
     */
    void onFirmwareComplete(std::function<void(bool success, const std::string& msg)> cb);

    // Control captive portal explicitly
    void stopPortal();

    // ======================================================
    // Emotion code parsing (from WebSocket messages)
    // ======================================================
    /// Parse emotion code from WebSocket message
    /// @param code 2-character emotion code ("01", "11", etc.)
    /// @return EmotionState, or NEUTRAL if code not recognized
    static state::EmotionState parseEmotionCode(const std::string& code);

private:
    // ======================================================
    // Internal handlers
    // ======================================================
    void handleWifiStatus(int status_code);
    void handleWsStatus(int status_code);
    
    // Retry logic for initial WiFi connection
    void retryWifiThenPortal();
    static void retryWifiTaskEntry(void* arg);

    // Receive message from WebSocketClient
    void handleWsTextMessage(const std::string& msg);
    void handleWsBinaryMessage(const uint8_t* data, size_t len);

    // Push connectivity state lên StateManager
    void publishState(state::ConnectivityState s);

    static void taskEntry(void* arg);

private:
    TaskHandle_t task_handle = nullptr;
    uint32_t update_interval_ms = 33; // ~30 FPS tick

private:
    // ======================================================
    // Components
    // ======================================================
    std::unique_ptr<WifiService> wifi;
    std::unique_ptr<WebSocketClient> ws;

    // ======================================================
    // Config storage
    // ======================================================
    Config config_{}; // holds init-time configuration

    // ======================================================
    // Runtime flags
    // ======================================================
    std::atomic<bool> started {false};

    // WiFi status flags
    bool wifi_ready = false;      // đã có IP hay chưa

    // WS runtime control
    bool ws_should_run = false;   // Manager muốn WS chạy
    bool ws_running    = false;   // WS thực sự open chưa
    bool ws_immune_mode = false;  // Prevent WS close during critical operations (e.g. audio streaming)
    bool speaking_session_active = false;  // Prevent SPEAKING state spam per TTS session

    // Retry timer (ms)
    uint32_t ws_retry_timer = 0;

    uint32_t tick_ms = 0;

    // ======================================================
    // App-level callbacks
    // ======================================================
    std::function<void(const std::string&)> on_text_cb = nullptr;
    std::function<void(const uint8_t*, size_t)> on_binary_cb = nullptr;
    std::function<void()> on_disconnect_cb = nullptr;

    // ======================================================
    // OTA Callbacks
    // ======================================================
    std::function<void(const uint8_t*, size_t)> on_firmware_chunk_cb = nullptr;
    std::function<void(bool, const std::string&)> on_firmware_complete_cb = nullptr;

    // OTA state
    bool firmware_download_active = false;
    uint32_t firmware_bytes_received = 0;
    
    // WiFi retry state
    TaskHandle_t wifi_retry_task = nullptr;
};
