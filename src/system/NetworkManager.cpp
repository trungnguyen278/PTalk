#include "NetworkManager.hpp"
#include "WifiService.hpp"
#include "WebSocketClient.hpp"

#include "esp_log.h"

static const char* TAG = "NetworkManager";

NetworkManager::NetworkManager() = default;

NetworkManager::~NetworkManager() {
    stop();
}

// ============================================================================
// INIT
// ============================================================================
bool NetworkManager::init()
{
    ESP_LOGI(TAG, "Init NetworkManager");

    wifi = std::make_unique<WifiService>();
    ws   = std::make_unique<WebSocketClient>();

    if (!wifi || !ws) {
        ESP_LOGE(TAG, "Failed to allocate WifiService or WebSocketClient");
        return false;
    }

    wifi->init();
    ws->init();

    // Apply configured WS URL if provided
    if (!config_.ws_url.empty()) {
        ws->setUrl(config_.ws_url);
    }

    // --------------------------------------------------------------------
    // WiFi Status Callback
    // --------------------------------------------------------------------
    wifi->onStatus([this](int status){
        this->handleWifiStatus(status);
    });

    // --------------------------------------------------------------------
    // WebSocket Status Callback
    // --------------------------------------------------------------------
    ws->onStatus([this](int status){
        this->handleWsStatus(status);
    });

    // --------------------------------------------------------------------
    // WebSocket Message Callbacks
    // --------------------------------------------------------------------
    ws->onText([this](const std::string& msg){
        this->handleWsTextMessage(msg);
    });

    ws->onBinary([this](const uint8_t* data, size_t len){
        this->handleWsBinaryMessage(data, len);
    });

    ESP_LOGI(TAG, "NetworkManager init OK");
    return true;
}

// Overload: init with configuration
bool NetworkManager::init(const Config& cfg)
{
    config_ = cfg;
    return init();
}

// ============================================================================
// START / STOP
// ============================================================================
void NetworkManager::start()
{
    if (started) return;
    started = true;

    ESP_LOGI(TAG, "NetworkManager start()");

    // Prefer explicit credentials if provided in config
    if (!config_.sta_ssid.empty() && !config_.sta_pass.empty()) {
        wifi->connectWithCredentials(config_.sta_ssid.c_str(), config_.sta_pass.c_str());
        publishState(state::ConnectivityState::CONNECTING_WIFI);
        // Spawn retry task to open portal if connection fails
        if (wifi_retry_task == nullptr) {
            xTaskCreate(&NetworkManager::retryWifiTaskEntry, "wifi_retry", 4096, this, 5, &wifi_retry_task);
        }
    } else {
        // Try auto-connect (saved creds). If available, still spawn retry task for fallback.
        wifi->autoConnect();
        publishState(state::ConnectivityState::CONNECTING_WIFI);
        
        // Always spawn retry task - even with autoConnect, if WiFi fails to actually connect,
        // we need the portal fallback after 5 seconds
        if (wifi_retry_task == nullptr) {
            ESP_LOGI(TAG, "Spawning WiFi retry task for fallback to portal if connection fails");
            xTaskCreate(&NetworkManager::retryWifiTaskEntry, "wifi_retry", 4096, this, 5, &wifi_retry_task);
        }
    }

    // Spawn internal update task so callers don't need to tick manually
    if (task_handle == nullptr) {
        BaseType_t rc = xTaskCreatePinnedToCore(
            &NetworkManager::taskEntry,
            "NetworkLoop",
            8192,  // Increased from 4096 to prevent stack overflow
            this,
            5,
            &task_handle,
            tskNO_AFFINITY
        );
        if (rc != pdPASS) {
            ESP_LOGE(TAG, "Failed to create NetworkLoop task (%d)", (int)rc);
            task_handle = nullptr;
        }
    }
}

void NetworkManager::stop()
{
    if (!started) return;
    started = false;

    ESP_LOGW(TAG, "NetworkManager stop()");

    ws_should_run = false;
    ws_running = false;

    if (ws) ws->close();
    if (wifi) wifi->disconnect();

    if (task_handle) {
        TaskHandle_t th = task_handle;
        task_handle = nullptr;
        vTaskDelete(th);
    }
}

// ============================================================================
// UPDATE LOOP
// ============================================================================

void NetworkManager::update(uint32_t dt_ms)
{
    if (!started) return;

    tick_ms += dt_ms;

    // --------------------------------------------------------------------
    // Retry WebSocket nếu WiFi đã kết nối
    // --------------------------------------------------------------------
    if (ws_should_run && !ws_running)
    {
        if (ws_retry_timer > 0) {
            ws_retry_timer = (ws_retry_timer > dt_ms) ? (ws_retry_timer - dt_ms) : 0;
            return;
        }

        ESP_LOGI(TAG, "NetworkManager → Trying WebSocket connect...");
        publishState(state::ConnectivityState::CONNECTING_WS);

        // Ensure WS URL is configured before connecting
        if (!config_.ws_url.empty()) {
            ws->setUrl(config_.ws_url);
        }
        ws->connect();

        ws_retry_timer = 5000;  // 5 second delay between reconnect attempts
    }
}

void NetworkManager::taskEntry(void* arg)
{
    auto* self = static_cast<NetworkManager*>(arg);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }

    TickType_t prev = xTaskGetTickCount();
    for (;;) {
        if (!self->started.load()) {
            vTaskDelete(nullptr);
        }

        TickType_t now = xTaskGetTickCount();
        uint32_t dt_ms = (now - prev) * portTICK_PERIOD_MS;
        prev = now;

        self->update(dt_ms ? dt_ms : self->update_interval_ms);

        vTaskDelay(pdMS_TO_TICKS(self->update_interval_ms));
    }
}

// ============================================================================
// SET CREDENTIALS
// ============================================================================
void NetworkManager::setCredentials(const std::string& ssid, const std::string& pass)
{
    if (wifi) {
        wifi->connectWithCredentials(ssid.c_str(), pass.c_str());
    }
}

// ============================================================================
// SEND MESSAGE TO WS
// ============================================================================
bool NetworkManager::sendText(const std::string& text)
{
    if (!ws_running) return false;
    return ws->sendText(text);
}

bool NetworkManager::sendBinary(const uint8_t* data, size_t len)
{
    if (!ws_running) return false;
    return ws->sendBinary(data, len);
}

// ============================================================================
// CALLBACK REGISTRATION
// ============================================================================
void NetworkManager::onServerText(std::function<void(const std::string&)> cb)
{
    on_text_cb = cb;
}

void NetworkManager::onServerBinary(std::function<void(const uint8_t*, size_t)> cb)
{
    on_binary_cb = cb;
}

void NetworkManager::onDisconnect(std::function<void()> cb)
{
    on_disconnect_cb = cb;
}

void NetworkManager::setWSImmuneMode(bool immune)
{
    ws_immune_mode = immune;
    if (immune) {
        ESP_LOGI(TAG, "WS immune mode ENABLED - WS will ignore WiFi fluctuations");
    } else {
        ESP_LOGI(TAG, "WS immune mode DISABLED - normal WS behavior");
    }
}

// ============================================================================
// RUNTIME CONFIG SETTERS
// ============================================================================
void NetworkManager::setWsUrl(const std::string& url)
{
    config_.ws_url = url;
    if (ws && !url.empty()) {
        ws->setUrl(url);
    }
}

void NetworkManager::setApSsid(const std::string& apSsid)
{
    config_.ap_ssid = apSsid;
}

void NetworkManager::setDeviceLimit(uint8_t maxClients)
{
    config_.ap_max_clients = maxClients;
}

// ============================================================================
// WIFI STATUS HANDLER
// ============================================================================
//
// WifiService status code:
//   0 = DISCONNECTED
//   1 = CONNECTING
//   2 = GOT_IP
//
void NetworkManager::handleWifiStatus(int status)
{
    ESP_LOGI(TAG, "handleWifiStatus called with status=%d", status);
    
    switch (status)
    {
    case 0: // DISCONNECTED
        ESP_LOGW(TAG, "WiFi → DISCONNECTED");

        wifi_ready = false;

        // Only close WS if NOT in immune mode (during audio streaming, keep WS alive)
        if (!ws_immune_mode) {
            ws_should_run = false;
            ws_running = false;
            if (ws) ws->close();
            publishState(state::ConnectivityState::OFFLINE);
        } else {
            ESP_LOGI(TAG, "WS immune mode active - ignoring WiFi disconnect, keeping WS alive");
            // Keep ws_should_run and ws_running unchanged
            // WS will survive temporary WiFi fluctuations
        }
        break;

    case 1: // CONNECTING
        ESP_LOGI(TAG, "WiFi → CONNECTING");
        publishState(state::ConnectivityState::CONNECTING_WIFI);
        break;

    case 2: // GOT_IP
        ESP_LOGI(TAG, "WiFi → GOT_IP");

        wifi_ready = true;
        ws_should_run = true;
        ws_retry_timer = 500; // Wait 500ms for WiFi to stabilize before WS connect

        publishState(state::ConnectivityState::CONNECTING_WS);
        break;
    }
    
    ESP_LOGI(TAG, "handleWifiStatus completed");
}

// ============================================================================
// WEBSOCKET STATUS HANDLER
// ============================================================================
//
// WebSocketClient status code:
//   0 = CLOSED
//   1 = CONNECTING
//   2 = OPEN
//
void NetworkManager::handleWsStatus(int status)
{
    switch (status)
    {
    case 0: // CLOSED
        if (ws_immune_mode) {
              ESP_LOGW(TAG, "WS → CLOSED during immune mode - forcing cleanup");
              // Server disconnect is FATAL - must cleanup even during audio
              ws_immune_mode = false;  // Disable immune mode to allow cleanup
        }
        
        ESP_LOGW(TAG, "WS → CLOSED");
        ws_running = false;
        
        // Notify disconnect callback to flush audio buffer
        if (on_disconnect_cb) {
            on_disconnect_cb();
        }

        if (ws_should_run) {
            ws_retry_timer = 1500;  // retry nhẹ nhàng
            publishState(state::ConnectivityState::CONNECTING_WS);
        } else {
            publishState(state::ConnectivityState::OFFLINE);
        }
        break;

    case 1: // CONNECTING
        ESP_LOGI(TAG, "WS → CONNECTING");
        publishState(state::ConnectivityState::CONNECTING_WS);
        break;

    case 2: // OPEN
        ESP_LOGI(TAG, "WS → OPEN");
        ws_running = true;
        publishState(state::ConnectivityState::ONLINE);
        break;
    }
}

// ============================================================================
// MESSAGE FROM WEBSOCKET
// ============================================================================
void NetworkManager::handleWsTextMessage(const std::string& msg)
{
    ESP_LOGI(TAG, "WS Text Message: %s", msg.c_str());
    
    // Try parsing emotion code if message is simple 2-char format
    if (msg.length() == 2) {
        auto emotion = parseEmotionCode(msg);
        StateManager::instance().setEmotionState(emotion);
        ESP_LOGI(TAG, "Emotion code: %s → %d", msg.c_str(), (int)emotion);
        // Don't return - still call on_text_cb for logging/debugging
    }
    
    if (on_text_cb) on_text_cb(msg);
}

void NetworkManager::handleWsBinaryMessage(const uint8_t* data, size_t len)
{
    //ESP_LOGI(TAG, "WS Binary Message (%zu bytes)", len);
    
    // Check if this is firmware data during OTA download
    if (firmware_download_active) {
        firmware_bytes_received += len;
        ESP_LOGI(TAG, "Firmware chunk: %zu bytes (total: %u bytes)", len, firmware_bytes_received);
        
        // Notify OTA updater
        if (on_firmware_chunk_cb) {
            on_firmware_chunk_cb(data, len);
        }
    } else {
        // Regular binary message
        if (on_binary_cb) {
            on_binary_cb(data, len);
        }
    }
}

// ============================================================================
// PUSH STATE TO STATEMANAGER
// ============================================================================
void NetworkManager::publishState(state::ConnectivityState s)
{
    StateManager::instance().setConnectivityState(s);
}

// ============================================================================
// OTA FIRMWARE UPDATE SUPPORT
// ============================================================================
bool NetworkManager::requestFirmwareUpdate(const std::string& version)
{
    if (!ws || !ws->isConnected()) {
        ESP_LOGE(TAG, "WebSocket not connected, cannot request firmware");
        return false;
    }

    firmware_download_active = true;
    firmware_bytes_received = 0;

    // Create request message (JSON format)
    std::string request = "{\"action\":\"update_firmware\"";
    if (!version.empty()) {
        request += ",\"version\":\"" + version + "\"";
    }
    request += "}";

    ESP_LOGI(TAG, "Requesting firmware update: %s", request.c_str());
    return sendText(request);
}

void NetworkManager::onFirmwareChunk(std::function<void(const uint8_t*, size_t)> cb)
{
    on_firmware_chunk_cb = cb;
}

void NetworkManager::onFirmwareComplete(std::function<void(bool, const std::string&)> cb)
{
    on_firmware_complete_cb = cb;
}

// Stop captive portal if running (used for low-battery mode)
void NetworkManager::stopPortal()
{
    if (wifi) {
        wifi->stopCaptivePortal();
    }
}

// ============================================================================
// WIFI RETRY LOGIC
// ============================================================================
void NetworkManager::retryWifiTaskEntry(void* arg)
{
    auto* self = static_cast<NetworkManager*>(arg);
    if (self) {
        self->retryWifiThenPortal();
    }
    vTaskDelete(nullptr);
}

void NetworkManager::retryWifiThenPortal()
{
    const int max_retries = 10;  // 5 seconds / 0.5s = 10 attempts
    
    ESP_LOGI(TAG, "Starting WiFi retry phase (5 seconds, 10 attempts)");
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        // Check if WiFi connected during retry
        if (wifi && wifi->isConnected()) {
            ESP_LOGI(TAG, "WiFi connected during retry phase - cancelling portal");
            wifi_retry_task = nullptr;
            return;
        }
        
        ESP_LOGI(TAG, "WiFi retry attempt %d/10", attempt + 1);
        
        // Wait 500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Check one final time after all retries
    if (wifi && wifi->isConnected()) {
        ESP_LOGI(TAG, "WiFi connected after retry phase - cancelling portal");
        wifi_retry_task = nullptr;
        return;
    }
    
    // 5 seconds elapsed without connection - scan then open portal
    ESP_LOGI(TAG, "WiFi retry phase complete - no connection. Scanning then opening portal...");
    
    if (wifi) {
        wifi->ensureStaStarted();
        // Scan and cache networks before opening portal
        wifi->scanAndCache();
        
        // Open portal with stop_wifi_first=true to ensure clean state
        wifi->startCaptivePortal(config_.ap_ssid, config_.ap_max_clients, true);
        publishState(state::ConnectivityState::WIFI_PORTAL);
    }
    
    wifi_retry_task = nullptr;
}

// ============================================================================
// EMOTION CODE PARSING
// ============================================================================
state::EmotionState NetworkManager::parseEmotionCode(const std::string& code)
{
    // Map WebSocket emotion codes to EmotionState
    // Format expected: "01", "11", etc. (2-char codes)
    
    if (code == "00" || code.empty()) {
        return state::EmotionState::NEUTRAL;
    }
    if (code == "01") {
        return state::EmotionState::HAPPY;      // Happy, cheerful
    }
    if (code == "02") {
        return state::EmotionState::ANGRY;      // Angry, urgent
    }
    if (code == "03") {
        return state::EmotionState::EXCITED;    // Excited, enthusiastic
    }
    if (code == "11") {
        return state::EmotionState::SAD;        // Sad, empathetic
    }
    if (code == "12") {
        return state::EmotionState::CONFUSED;   // Confused, uncertain
    }
    if (code == "13") {
        return state::EmotionState::CALM;       // Calm, soothing
    }
    if (code == "99") {
        return state::EmotionState::THINKING;   // Thinking, processing
    }
    
    ESP_LOGW(TAG, "Unknown emotion code: %s", code.c_str());
    return state::EmotionState::NEUTRAL;
}

