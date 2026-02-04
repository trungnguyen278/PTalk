#pragma once
#include <memory>
#include <string>
#include <atomic>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "freertos/ringbuf.h"
#include "freertos/stream_buffer.h"

#include "system/StateTypes.hpp"
#include "system/StateManager.hpp"
#include "BluetoothService.hpp"
#include "meo/MeoFeature.hpp"

class WifiService;     // Low-level WiFi
class WebSocketClient; // Low-level WebSocket
class MqttClient;
struct WifiInfo;
class PowerManager;

// NetworkManager coordinates Wi‑Fi and WebSocket, publishes connectivity state,
// and bridges WS messages to the app. It owns retry logic; Wi‑Fi scanning/portal
// and driver-level connection details stay in WifiService.
class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    // ======================================================
    // Configuration
    // ======================================================
    struct Config
    {
        // Wi‑Fi station credentials (optional). If empty → use saved or portal
        std::string sta_ssid; // target Wi‑Fi SSID
        std::string sta_pass; // target Wi‑Fi password

        // Captive portal (AP) fallback settings
        std::string ap_ssid = "PTalk"; // AP SSID when opening portal
        uint8_t ap_max_clients = 4;    // limit number of AP clients

        // WebSocket server endpoint
        std::string ws_url; // e.g. ws://192.168.1.100:8080/ws
        // MQTT broker endpoint
        std::string mqtt_url; // e.g. mqtt://broker.hivemq.com:
        
        // MEO SDK credentials (from BLE provisioning)
        std::string user_id;   // MEO user ID (namespace for multi-tenant)
        std::string tx_key;    // MEO transmit key (MQTT password)
    };

    // ======================================================
    // INIT / START / STOP
    // ======================================================
    // Initialize Wi‑Fi and WebSocket services; returns false on allocation failure.
    bool init();
    // Init with configuration (preferred)
    // Initialize using provided config; returns false on failure.
    bool init(const Config &cfg);

    // Start Wi‑Fi connection workflow and WS task; no-op if already started.
    void start();

    // Stop WS, Wi‑Fi, and internal tasks.
    void stop();

    // ======================================================
    // External API
    // ======================================================

    // Tick the manager (used by internal task); dt_ms is elapsed milliseconds.
    void update(uint32_t dt_ms);

    // Update credentials when user submits portal form.
    void setCredentials(const std::string &ssid, const std::string &pass);

    // Runtime config setters (optional, can be used before start)
    void setWsUrl(const std::string &url);
    void setApSsid(const std::string &apSsid);
    void setDeviceLimit(uint8_t maxClients);

    // Set mic encoded stream buffer (for uplink audio task)
    void setMicBuffer(StreamBufferHandle_t sb) { mic_encoded_sb = sb; }

    // Send text message to server; returns false if WS not running.
    bool sendText(const std::string &text);
    // Send binary message to server; returns false if WS not running.
    bool sendBinary(const uint8_t *data, size_t len);

    // Register callback for incoming WS text.
    void onServerText(std::function<void(const std::string &)> cb);

    // Register callback for incoming WS binary.
    void onServerBinary(std::function<void(const uint8_t *, size_t)> cb);

    // Register callback on WS disconnect (to flush buffers/reset state).
    void onDisconnect(std::function<void()> cb);

    // When true, keep WS alive across minor Wi‑Fi drops (e.g., during audio streaming).
    void setWSImmuneMode(bool immune);

    // Set BluetoothService for BLE config mode handoff.
    void setBluetoothService(std::shared_ptr<BluetoothService> ble) { ble_service = ble; }

    // Set external manager references for handling config updates
    void setManagers(class AudioManager *audio, class DisplayManager *display);
    void setPowerManager(PowerManager *power) { power_manager = power; }

    // Check if a speaking session is active (prevents SPEAKING spam).
    bool isSpeakingSessionActive() const { return speaking_session_active; }

    // Mark start of speaking session (debounces SPEAKING state).
    void startSpeakingSession() { speaking_session_active = true; }

    // Mark end of speaking session (allows next TTS to trigger SPEAKING).
    void endSpeakingSession() { speaking_session_active = false; }

    // ======================================================
    // OTA Firmware Update Support
    // ======================================================
    // Request firmware update; server should respond with binary firmware stream.
    
    uint32_t getFirmwareExpectedSize() const { return firmware_expected_size; }
    std::string getFirmwareExpectedChecksum() const { return firmware_expected_sha256; }

    // Register callback for incoming firmware data chunks during OTA.
    void onFirmwareChunk(std::function<void(const uint8_t *, size_t)> cb);

    // Register callback for firmware download completion (success flag, message).
    void onFirmwareComplete(std::function<void(bool success, const std::string &msg)> cb);

    // Register callback when server initiates OTA (to setup OTA handlers before data arrives)
    void onServerOTARequest(std::function<void()> cb);

    // Control captive portal explicitly
    void stopPortal();

    void startBLEConfigMode();

    // Open BLE config mode with WiFi scan (can be called proactively)
    void openBLEConfigMode();

    // Deferred BLE config mode (called from separate task to avoid callback issues)
    void openBLEConfigModeDeferred();

    // ======================================================
    // Emotion code parsing (from WebSocket messages)
    // ======================================================
    /// Parse emotion code from WebSocket message
    /// @param code 2-character emotion code ("01", "11", etc.)
    /// @return EmotionState, or NEUTRAL if code not recognized
    static state::EmotionState parseEmotionCode(const std::string &code);

    // ======================================================
    // Real-time WebSocket Configuration Support
    // ======================================================
    /// Send device handshake to server (called on WS connection)
    bool sendDeviceHandshake();

    /// Register callback for external config change requests (from server or internal)
    /// Useful for AppController to respond to config updates
    void onConfigUpdate(std::function<void(const std::string &, const std::string &)> cb);

    /// Apply volume configuration (0-100)
    bool applyVolumeConfig(uint8_t volume);

    /// Apply brightness configuration (0-100)
    bool applyBrightnessConfig(uint8_t brightness);

    /// Apply device name configuration
    bool applyDeviceNameConfig(const std::string &name);

    /// Apply WiFi configuration (triggers reboot after save)
    bool applyWiFiConfig(const std::string &ssid, const std::string &password);

    /// Get current device status for status query response
    std::string getCurrentStatusJson() const;

    // ======================================================
    // MEO Feature Layer API
    // ======================================================
    /// Register a feature handler
    void registerFeature(const std::string& name, meo::FeatureHandler handler);
    
    /// Unregister a feature handler
    void unregisterFeature(const std::string& name);
    
    /// Publish an event to server
    bool publishEvent(const std::string& event_name, const meo::FeatureParams& data = {});
    
    /// Publish feature response
    bool publishFeatureResponse(const meo::FeatureResponse& response);
    
    /// Get MEO feature manager for advanced usage
    meo::MeoFeatureManager& getFeatureManager() { return feature_manager_; }

private:
    // ======================================================
    // Internal handlers
    // ======================================================
    // React to Wi‑Fi status changes from WifiService.
    void handleWifiStatus(int status_code);

    // React to WebSocket status changes from WebSocketClient.
    void handleWsStatus(int status_code);

    bool sendWSDeviceHandshake();

    // Retry logic for initial WiFi connection
    void retryWifiThenPortal();
    void retryWifiThenBLE();
    static void retryWifiTaskEntry(void *arg);

    // Receive message from WebSocketClient
    // Handle inbound WS text and dispatch to callbacks/state updates.
    void handleWsTextMessage(const std::string &msg);

    // Process configuration command from WebSocket message
    void handleConfigCommand(const std::string &json_msg);

    // MEO Feature Layer handlers
    void handleMeoFeatureInvoke(const std::string& payload, const std::string& feature_from_topic);
    void registerBuiltinFeatures();

    // Handle inbound WS binary payloads (firmware or app data).
    void handleWsBinaryMessage(const uint8_t *data, size_t len);
    void handleOtaBinaryChunk(const uint8_t *data, size_t len);
    // OTA chunk protocol ACK/NACK helpers
    void sendOtaAck(uint32_t seq);
    void sendOtaNack(uint32_t seq);
    // MQTT setup and status publishing
    void setupMqtt();
    void publishMqttStatus();

    // Uplink task for sending microphone data
    void uplinkTaskLoop();
    static void uplinkTaskEntry(void *arg);
    // Push connectivity state lên StateManager
    void publishState(state::ConnectivityState s);
    // Start/stop uplink task based on interaction state.
    void handleInteractionState(state::InteractionState s);

    static void taskEntry(void *arg);

private:
    TaskHandle_t task_handle = nullptr;

    uint32_t update_interval_ms = 33; // ~30 FPS tick
    int sub_interaction_id = -1;

private:
    // ======================================================
    // Components
    // ======================================================
    std::unique_ptr<WifiService> wifi;
    std::unique_ptr<WebSocketClient> ws;
    // Bluetooth service for BLE config mode
    std::shared_ptr<BluetoothService> ble_service;

    // External manager references for config updates (optional, set via setManagers)
    class AudioManager *audio_manager = nullptr;
    class DisplayManager *display_manager = nullptr;
    PowerManager *power_manager = nullptr;

    // ======================================================
    // Config storage
    // ======================================================
    Config config_{}; // holds init-time configuration

    // ======================================================
    // Runtime flags
    // ======================================================
    std::atomic<bool> started{false};

    // WiFi status flags
    bool wifi_ready = false; // đã có IP hay chưa

    std::vector<WifiInfo> cached_networks; // Cached WiFi scan results

    // WS runtime control
    bool ws_should_run = false;           // Manager muốn WS chạy
    bool ws_running = false;              // WS thực sự open chưa
    bool ws_immune_mode = false;          // Prevent WS close during critical operations (e.g. audio streaming)
    bool speaking_session_active = false; // Prevent SPEAKING state spam per TTS session
                                          // Mqtt client for telemetry
    std::unique_ptr<MqttClient> mqtt;
    std::string mqtt_base_topic; // Ví dụ: "ptalk/DEVICE_ID"
    
    // MEO SDK topic patterns
    std::string meo_user_id_;              // User ID from provisioning
    std::string meo_device_id_;            // Device ID (MAC-based)
    std::string meo_invoke_topic_;         // meo/{userId}/{deviceId}/feature
    std::string meo_legacy_invoke_prefix_; // meo/{deviceId}/feature/
    std::string meo_event_topic_;          // meo/{userId}/{deviceId}/event
    bool meo_cloud_compatible_ = true;     // Use cloud-compatible topic pattern
    
    // MEO Feature Manager
    meo::MeoFeatureManager feature_manager_;
    //
    StreamBufferHandle_t mic_encoded_sb = nullptr;
    TaskHandle_t uplink_task_handle = nullptr;

    // Retry timer (ms)
    uint32_t ws_retry_timer = 0;

    uint32_t tick_ms = 0;

    // ======================================================
    // App-level callbacks
    // ======================================================
    std::function<void(const std::string &)> on_text_cb = nullptr;
    std::function<void(const uint8_t *, size_t)> on_binary_cb = nullptr;
    std::function<void()> on_disconnect_cb = nullptr;
    std::function<void(const std::string &, const std::string &)> on_config_update_cb = nullptr;

    // ======================================================
    // OTA Callbacks
    // ======================================================
    std::function<void(const uint8_t *, size_t)> on_firmware_chunk_cb = nullptr;
    std::function<void(bool, const std::string &)> on_firmware_complete_cb = nullptr;
    std::function<void()> on_server_ota_request_cb = nullptr;

    // OTA state
    bool firmware_download_active = false;
    uint32_t firmware_bytes_received = 0;
    uint32_t firmware_expected_size = 0;
    std::string firmware_expected_sha256;

    // OTA chunk protocol state
    uint32_t ota_expected_seq = 0;    // Next expected sequence number
    uint32_t ota_chunk_size = 2048;   // Chunk data size from server
    uint32_t ota_total_chunks = 0;    // Total chunks expected
    uint32_t ota_chunks_received = 0; // Chunks successfully received
    uint32_t ota_chunks_failed = 0;   // Chunks with CRC errors

    // WiFi retry state
    TaskHandle_t wifi_retry_task = nullptr;

    // BLE config task (deferred from callback context)
    static void bleConfigTaskEntry(void *arg);
    TaskHandle_t ble_config_task = nullptr;
};
