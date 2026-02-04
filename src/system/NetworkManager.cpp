#include "NetworkManager.hpp"
#include "WifiService.hpp"
#include "WebSocketClient.hpp"
#include "MqttClient.hpp"
#include "meo/MeoFeature.hpp"
#include "system/AudioManager.hpp"
#include "system/DisplayManager.hpp"
#include "system/PowerManager.hpp"
#include "system/MQTTConfig.hpp"

#include "esp_mac.h"
#include <sstream>
#include <iomanip>
#include "Version.hpp"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crc.h" // For CRC32 verification


static const char *TAG = "NetworkManager";

// Forward declarations for NVS storage utility functions
static bool nmgr_save_u8(const char *key, uint8_t value);
static bool nmgr_save_str(const char *key, const std::string &value);
static uint8_t nmgr_load_u8(const char *key, uint8_t def_val);
static std::string nmgr_load_str(const char *key, const char *def_val);

NetworkManager::NetworkManager() = default;

NetworkManager::~NetworkManager()
{
    stop();
}

// ============================================================================
// INIT
// ============================================================================
bool NetworkManager::init()
{
    ESP_LOGI(TAG, "Init NetworkManager");

    wifi = std::make_unique<WifiService>();
    ws = std::make_unique<WebSocketClient>();

    if (!wifi || !ws)
    {
        ESP_LOGE(TAG, "Failed to allocate WifiService or WebSocketClient");
        return false;
    }

    wifi->init();
    ws->init();

    // Apply configured WS URL if provided
    if (!config_.ws_url.empty())
    {
        ws->setUrl(config_.ws_url);
    }

    // --------------------------------------------------------------------
    // WiFi Status Callback
    // --------------------------------------------------------------------
    wifi->onStatus([this](int status)
                   { this->handleWifiStatus(status); });

    // --------------------------------------------------------------------
    // WebSocket Status Callback
    // --------------------------------------------------------------------
    ws->onStatus([this](int status)
                 { this->handleWsStatus(status); });

    // --------------------------------------------------------------------
    // WebSocket Message Callbacks
    // --------------------------------------------------------------------
    ws->onText([this](const std::string &msg)
               { this->handleWsTextMessage(msg); });

    ws->onBinary([this](const uint8_t *data, size_t len)
                 { this->handleWsBinaryMessage(data, len); });

    // Subscribe to interaction state updates
    sub_interaction_id = StateManager::instance().subscribeInteraction(
        [this](state::InteractionState s, state::InputSource src)
        {
            this->handleInteractionState(s);
        });

    mqtt = std::make_unique<MqttClient>();
    mqtt->init();

    // Setup MEO SDK topic patterns
    meo_device_id_ = getDeviceEfuseID();
    meo_user_id_ = config_.user_id.empty() ? "default" : config_.user_id;
    
    // Default tx_key for demo/development (server should allow this or require provisioning)
    std::string effective_tx_key = config_.tx_key.empty() ? "ptalk_default_key" : config_.tx_key;
    config_.tx_key = effective_tx_key;
    
    // MEO topic patterns
    // Cloud-compatible: meo/{userId}/{deviceId}/feature
    // Legacy: meo/{deviceId}/feature/{featureName}/invoke
    meo_invoke_topic_ = "meo/" + meo_user_id_ + "/" + meo_device_id_ + "/feature";
    meo_legacy_invoke_prefix_ = "meo/" + meo_device_id_ + "/feature/";
    meo_event_topic_ = "meo/" + meo_user_id_ + "/" + meo_device_id_ + "/event";
    
    // Legacy topic for backward compatibility
    mqtt_base_topic = "devices/" + meo_device_id_;

    setupMqtt();
    registerBuiltinFeatures();
    ESP_LOGI(TAG, "NetworkManager init OK");
    return true;
}

// Overload: init with configuration
bool NetworkManager::init(const Config &cfg)
{
    config_ = cfg;
    return init();
}

// ============================================================================
// START / STOP
// ============================================================================
void NetworkManager::start()
{
    if (started)
        return;
    started = true;

    ESP_LOGI(TAG, "NetworkManager start()");

    // Prefer explicit credentials if provided in config
    if (!config_.sta_ssid.empty() && !config_.sta_pass.empty())
    {
        wifi->connectWithCredentials(config_.sta_ssid.c_str(), config_.sta_pass.c_str());
        publishState(state::ConnectivityState::CONNECTING_WIFI);
        // Spawn retry task to open portal if connection fails
        if (wifi_retry_task == nullptr)
        {
            xTaskCreate(&NetworkManager::retryWifiTaskEntry, "wifi_retry", 4096, this, 5, &wifi_retry_task);
        }
    }
    else
    {
        // Try auto-connect (saved creds). If available, still spawn retry task for fallback.
        wifi->autoConnect();
        publishState(state::ConnectivityState::CONNECTING_WIFI);

        // Always spawn retry task - even with autoConnect, if WiFi fails to actually connect,
        // we need the portal fallback after 5 seconds
        if (wifi_retry_task == nullptr)
        {
            ESP_LOGI(TAG, "Spawning WiFi retry task for fallback to portal if connection fails");
            xTaskCreate(&NetworkManager::retryWifiTaskEntry, "wifi_retry", 4096, this, 5, &wifi_retry_task);
        }
    }

    // Spawn internal update task so callers don't need to tick manually
    if (task_handle == nullptr)
    {
        BaseType_t rc = xTaskCreatePinnedToCore(
            &NetworkManager::taskEntry,
            "NetworkLoop",
            8192, // Increased from 4096 to prevent stack overflow
            this,
            5,
            &task_handle,
            tskNO_AFFINITY);
        if (rc != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create NetworkLoop task (%d)", (int)rc);
            task_handle = nullptr;
        }
    }
}

void NetworkManager::stop()
{
    if (!started)
        return;
    started = false;

    ESP_LOGW(TAG, "NetworkManager stop()");

    ws_should_run = false;
    ws_running = false;

    if (ws)
        ws->close();
    if (wifi)
        wifi->disconnect();

    if (task_handle)
    {
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
    if (!started)
        return;

    tick_ms += dt_ms;
    if (ws_running && !ws->isConnected())
    {
        ws->close();
    }
    // --------------------------------------------------------------------
    // Retry WebSocket when Wi‑Fi is connected
    // --------------------------------------------------------------------
    if (ws_should_run && !ws_running)
    {
        if (ws_retry_timer > 0)
        {
            ws_retry_timer = (ws_retry_timer > dt_ms) ? (ws_retry_timer - dt_ms) : 0;
            return;
        }

        ESP_LOGI(TAG, "NetworkManager → Trying WebSocket connect...");
        publishState(state::ConnectivityState::CONNECTING_WS);

        // Ensure WS URL is configured before connecting
        if (!config_.ws_url.empty())
        {
            ws->setUrl(config_.ws_url);
        }
        ws->connect();

        ws_retry_timer = 5000; // 5 second delay between reconnect attempts
    }
}

void NetworkManager::taskEntry(void *arg)
{
    auto *self = static_cast<NetworkManager *>(arg);
    if (!self)
    {
        vTaskDelete(nullptr);
        return;
    }

    TickType_t prev = xTaskGetTickCount();
    for (;;)
    {
        if (!self->started.load())
        {
            // Clear our own handle before self-deleting
            self->task_handle = nullptr;
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
void NetworkManager::setCredentials(const std::string &ssid, const std::string &pass)
{
    if (wifi)
    {
        wifi->connectWithCredentials(ssid.c_str(), pass.c_str());
    }
}

// ============================================================================
// SEND MESSAGE TO WS
// ============================================================================
bool NetworkManager::sendText(const std::string &text)
{
    if (!ws_running)
        return false;
    return ws->sendText(text);
}

bool NetworkManager::sendBinary(const uint8_t *data, size_t len)
{
    if (!ws_running)
        return false;
    return ws->sendBinary(data, len);
}

// ============================================================================
// CALLBACK REGISTRATION
// ============================================================================
void NetworkManager::onServerText(std::function<void(const std::string &)> cb)
{
    on_text_cb = cb;
}

void NetworkManager::onServerBinary(std::function<void(const uint8_t *, size_t)> cb)
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
    if (immune)
    {
        ESP_LOGI(TAG, "WS immune mode ENABLED - WS will ignore WiFi fluctuations");
    }
    else
    {
        ESP_LOGI(TAG, "WS immune mode DISABLED - normal WS behavior");
    }
}

// ============================================================================
// RUNTIME CONFIG SETTERS
// ============================================================================
void NetworkManager::setWsUrl(const std::string &url)
{
    config_.ws_url = url;
    if (ws && !url.empty())
    {
        ws->setUrl(url);
    }
}

void NetworkManager::setApSsid(const std::string &apSsid)
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
        mqtt->stop();
        // Only close WS if NOT in immune mode (during audio streaming, keep WS alive)
        if (!ws_immune_mode)
        {
            ws_should_run = false;
            ws_running = false;
            if (ws)
                ws->close();
            publishState(state::ConnectivityState::OFFLINE);
        }
        else
        {
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

        if (wifi_retry_task)
        {
            vTaskDelete(wifi_retry_task);
            wifi_retry_task = nullptr;
        }
        wifi_ready = true;
        ws_should_run = true;
        ws_retry_timer = 500; // Wait 500ms for WiFi to stabilize before WS connect
        mqtt->start();
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
        ESP_LOGW(TAG, "WS → CLOSED");

        ws_running = false;

        if (on_disconnect_cb)
            on_disconnect_cb();

        if (wifi_ready)
        {
            ws_should_run = true;
            ws_retry_timer = 1500;
            publishState(state::ConnectivityState::CONNECTING_WS);
        }
        else
        {
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

        // Send device handshake to server with all info and device_id for linking
        sendWSDeviceHandshake();

        break;
    }
}
bool NetworkManager::sendWSDeviceHandshake()
{
    if (!ws_running)
        return false;

    std::string device_id = getDeviceEfuseID();
    std::string app_version = app_meta::APP_VERSION;
    std::string device_name = nmgr_load_str("device_name", "PTalk");
    uint8_t battery = power_manager ? power_manager->getPercent() : 85;

    // Build handshake message with device info
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "device_handshake");
    cJSON_AddStringToObject(root, "device_id", device_id.c_str());
    cJSON_AddStringToObject(root, "firmware_version", app_version.c_str());
    cJSON_AddStringToObject(root, "device_name", device_name.c_str());
    cJSON_AddNumberToObject(root, "battery_percent", battery);
    cJSON_AddStringToObject(root, "connectivity_state", "ONLINE");

    char *json_str = cJSON_Print(root);
    bool result = sendText(json_str);

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Device handshake sent to server");
    return result;
}

// ============================================================================
// MESSAGE FROM WEBSOCKET
// ============================================================================
void NetworkManager::handleWsTextMessage(const std::string &msg)
{
    ESP_LOGI(TAG, "WS Text Message: %s", msg.c_str());

    // ❌ XÓA: Không còn parse JSON config commands từ WS
    // cJSON *json = cJSON_Parse(msg.c_str());
    // if (json) { ... handleConfigCommand(msg); ... }

    // ✅ GIỮ LẠI: Chỉ xử lý emotion codes và audio control
    if (msg.length() == 2)
    {
        auto emotion = parseEmotionCode(msg);
        StateManager::instance().setEmotionState(emotion);
        ESP_LOGI(TAG, "Emotion code: %s → %d", msg.c_str(), (int)emotion);
    }


        if (on_text_cb)
            on_text_cb(msg);
    
}

void NetworkManager::handleWsBinaryMessage(const uint8_t *data, size_t len)
{
    // ❌ XÓA: OTA logic đã chuyển sang MQTT
    // if (firmware_download_active) { ... }

    // ✅ CHỈ GIỮ: Audio streaming downlink
    if (on_binary_cb)
    {
        on_binary_cb(data, len);
    }
}

void NetworkManager::handleOtaBinaryChunk(const uint8_t *data, size_t len)
{
    if (!firmware_download_active)
    {
        ESP_LOGW(TAG, "OTA not active, ignoring binary chunk");
        sendOtaNack(0);
        return;
    }

    const size_t HEADER_SIZE = 12;
    if (len < HEADER_SIZE)
    {
        ESP_LOGE(TAG, "OTA chunk too small: %zu bytes", len);
        return;
    }

    // Parse header: [4B seq][4B size][4B crc32][data...]
    uint32_t seq = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    uint32_t chunk_size = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    uint32_t recv_crc = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

    const uint8_t *chunk_data = data + HEADER_SIZE;
    size_t actual_data_len = len - HEADER_SIZE;

    // Verify size
    if (actual_data_len != chunk_size)
    {
        ESP_LOGE(TAG, "Chunk size mismatch: header=%u, actual=%zu", chunk_size, actual_data_len);
        sendOtaNack(seq);
        return;
    }

    // Verify CRC32
    uint32_t calc_crc = esp_crc32_le(0, chunk_data, chunk_size);
    if (calc_crc != recv_crc)
    {
        ESP_LOGE(TAG, "Chunk %u CRC mismatch: recv=0x%08X, calc=0x%08X",
                 seq, recv_crc, calc_crc);
        ota_chunks_failed++;
        sendOtaNack(seq);
        return;
    }

    // Check sequence
    if (seq != ota_expected_seq)
    {
        if (seq < ota_expected_seq)
        {
            ESP_LOGW(TAG, "Duplicate chunk %u (expected %u) - ACKing", seq, ota_expected_seq);
            sendOtaAck(seq);
            return;
        }
        else
        {
            ESP_LOGE(TAG, "Sequence gap: got %u, expected %u", seq, ota_expected_seq);
            sendOtaNack(seq);
            return;
        }
    }

    // ✅ Chunk OK - forward to OTA handler
    firmware_bytes_received += chunk_size;
    ota_chunks_received++;
    ota_expected_seq++;

    // Log progress
    if (firmware_expected_size > 0)
    {
        uint32_t percent = (firmware_bytes_received * 100) / firmware_expected_size;
        static uint32_t last_percent = 0;
        if (percent >= last_percent + 10 || percent == 100)
        {
            ESP_LOGI(TAG, "OTA: %u%% (%u/%u bytes, chunk %u/%u)",
                     percent, firmware_bytes_received, firmware_expected_size,
                     ota_chunks_received, ota_total_chunks);
            last_percent = (percent / 10) * 10;
        }
    }

    // Pass to AppController
    if (on_firmware_chunk_cb)
    {
        on_firmware_chunk_cb(chunk_data, chunk_size);
    }

    // Send ACK via MQTT
    sendOtaAck(seq);

    if (firmware_bytes_received >= firmware_expected_size)
    {
        ESP_LOGI(TAG, "OTA download complete: %u bytes in %u chunks (%u failed)",
                 firmware_bytes_received, ota_chunks_received, ota_chunks_failed);
        firmware_download_active = false;
        if (on_firmware_complete_cb)
        {
            on_firmware_complete_cb(true, "Download complete");
        }
    }
}

// Task loop sending microphone data to server
void NetworkManager::uplinkTaskLoop()
{
    const size_t SEND_SIZE = 512;
    uint8_t send_buf[SEND_SIZE];
    size_t acc = 0;

    while (started)
    {
        bool is_listening = (StateManager::instance().getInteractionState() == state::InteractionState::LISTENING);

        if (!ws_running)
            break;

        // Exit when not listening and buffer is empty
        if (!is_listening && xStreamBufferIsEmpty(mic_encoded_sb) && acc == 0)
            break;

        // Read data, waiting up to 100ms to batch enough bytes (non-busy wait)
        size_t want = SEND_SIZE - acc;
        size_t got = xStreamBufferReceive(mic_encoded_sb, send_buf + acc, want, pdMS_TO_TICKS(100));

        if (got > 0)
        {
            acc += got;
        }

        // Send immediately when 512 bytes are ready
        if (acc == SEND_SIZE)
        {
            ws->sendBinary(send_buf, SEND_SIZE);
            acc = 0;
            // Không vTaskDelay ở đây để có thể gửi liên tiếp nếu buffer đang đầy
        }

        // Flush remaining buffer once capture stops
        if (!is_listening && acc > 0 && xStreamBufferIsEmpty(mic_encoded_sb))
        {
            memset(send_buf + acc, 0, SEND_SIZE - acc);
            ws->sendBinary(send_buf, SEND_SIZE);
            break;
        }
    }
    // 4. Dọn dẹp an toàn
    if (mic_encoded_sb)
    {
        xStreamBufferReset(mic_encoded_sb);
    }
    uplink_task_handle = nullptr;
    ESP_LOGW(TAG, "Uplink task deleted");
    vTaskDelete(nullptr);
}

void NetworkManager::uplinkTaskEntry(void *arg)
{
    // Cast back to instance pointer
    auto *self = static_cast<NetworkManager *>(arg);
    if (self)
    {
        self->uplinkTaskLoop(); // Run loop (non-static)
    }
}

// ============================================================================
// PUSH STATE TO STATEMANAGER
// ============================================================================
void NetworkManager::publishState(state::ConnectivityState s)
{
    StateManager::instance().setConnectivityState(s);
}

void NetworkManager::handleInteractionState(state::InteractionState s)
{
    if (s == state::InteractionState::LISTENING)
    {
        if (uplink_task_handle == nullptr)
        {
            ESP_LOGI(TAG, "Starting Uplink Task (State: LISTENING)");
            xTaskCreatePinnedToCore(
                &NetworkManager::uplinkTaskEntry,
                "WsUplink",
                4096,
                this,
                5,
                &uplink_task_handle,
                1 // Core 0
            );
        }
    }
    else
    {
        // Khi không còn LISTENING, chúng ta không delete task từ đây
        // mà để task loop tự kiểm tra và thoát để đảm bảo an toàn dữ liệu.
    }
}

// ============================================================================
// OTA CHUNK PROTOCOL ACK/NACK
// ============================================================================
void NetworkManager::sendOtaAck(uint32_t seq)
{
    if (!mqtt || !mqtt->isConnected())
    {
        ESP_LOGW(TAG, "MQTT not connected, cannot send OTA ACK");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ota_ack", seq);
    char *json_str = cJSON_PrintUnformatted(root);

    mqtt->publish(mqtt_base_topic + "/ota_ack", json_str, 1, false);

    cJSON_free(json_str);
    cJSON_Delete(root);
}

void NetworkManager::sendOtaNack(uint32_t seq)
{
    if (!mqtt || !mqtt->isConnected())
    {
        ESP_LOGW(TAG, "MQTT not connected, cannot send OTA NACK");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ota_nack", seq);
    cJSON_AddNumberToObject(root, "expected_seq", ota_expected_seq);
    char *json_str = cJSON_PrintUnformatted(root);

    mqtt->publish(mqtt_base_topic + "/ota_ack", json_str, 1, false);

    ESP_LOGW(TAG, "Sent NACK for chunk %u (expected %u)", seq, ota_expected_seq);

    cJSON_free(json_str);
    cJSON_Delete(root);
}

void NetworkManager::setupMqtt()
{
    mqtt->setUri(config_.mqtt_url);
    mqtt->setClientId("PTalk_" + meo_device_id_);
    
    // MEO SDK authentication: device_id as username, tx_key as password
    if (!config_.tx_key.empty())
    {
        mqtt->setCredentials(meo_device_id_, config_.tx_key);
        ESP_LOGI(TAG, "MQTT auth enabled with tx_key");
    }

    mqtt->onConnected([this]()
                      {
        ESP_LOGI(TAG, "MQTT Connected - subscribing to MEO topics");
        
        // ============================================================
        // MEO SDK Topic Subscriptions
        // ============================================================
        
        // 1. Cloud-compatible feature invoke: meo/{userId}/{deviceId}/feature
        mqtt->subscribe(meo_invoke_topic_, 1);
        ESP_LOGI(TAG, "Subscribed to MEO invoke: %s", meo_invoke_topic_.c_str());
        
        // 2. Legacy feature invoke: meo/{deviceId}/feature/+/invoke (wildcard)
        std::string legacy_wildcard = meo_legacy_invoke_prefix_ + "+/invoke";
        mqtt->subscribe(legacy_wildcard, 1);
        ESP_LOGI(TAG, "Subscribed to MEO legacy: %s", legacy_wildcard.c_str());
        
        // ============================================================
        // Legacy PTalk Topics (backward compatibility)
        // ============================================================
        mqtt->subscribe(mqtt_base_topic + "/cmd", 1);
        mqtt->subscribe(mqtt_base_topic + "/ota_data", 1);
        mqtt->subscribe(mqtt_base_topic + "/ota_ack", 0);
        
        // Send device handshake on connect
        sendDeviceHandshake();
        
        // Publish initial status event
        publishEvent(meo::events::STATUS, {
            {"connectivity", "online"},
            {"firmware_version", app_meta::APP_VERSION}
        }); });

    mqtt->onMessage([this](const std::string &topic, const std::string &payload)
                    {
                        ESP_LOGD(TAG, "MQTT message: %s (%zu bytes)", topic.c_str(), payload.size());

                        // ============================================================
                        // MEO SDK Feature Invoke Handling
                        // ============================================================
                        
                        // Cloud-compatible invoke: meo/{userId}/{deviceId}/feature
                        if (topic == meo_invoke_topic_)
                        {
                            handleMeoFeatureInvoke(payload, "");
                            return;
                        }
                        
                        // Legacy invoke: meo/{deviceId}/feature/{featureName}/invoke
                        if (topic.find(meo_legacy_invoke_prefix_) == 0 && 
                            topic.find("/invoke") != std::string::npos)
                        {
                            // Extract feature name from topic
                            size_t start = meo_legacy_invoke_prefix_.length();
                            size_t end = topic.find("/invoke");
                            std::string feature_name = topic.substr(start, end - start);
                            handleMeoFeatureInvoke(payload, feature_name);
                            return;
                        }
                        
                        // ============================================================
                        // Legacy PTalk Topics
                        // ============================================================
                        if (topic == mqtt_base_topic + "/cmd")
                        {
                            handleConfigCommand(payload);
                        }
                        else if (topic == mqtt_base_topic + "/ota_data")
                        {
                            handleOtaBinaryChunk((const uint8_t *)payload.data(), payload.size());
                        }
                    });

    mqtt->onDisconnected([this]()
                         {
                             ESP_LOGW(TAG, "MQTT Disconnected");
                             // Don't stop OTA if WiFi is still up - will reconnect
                         });
}

void NetworkManager::publishMqttStatus()
{
    // Gửi heartbeat/status định kỳ hoặc khi có thay đổi
    std::string status_json = getCurrentStatusJson();
    mqtt->publish(mqtt_base_topic + "/status", status_json, 1, true);
}

void NetworkManager::onFirmwareChunk(std::function<void(const uint8_t *, size_t)> cb)
{
    on_firmware_chunk_cb = cb;
}

void NetworkManager::onFirmwareComplete(std::function<void(bool, const std::string &)> cb)
{
    on_firmware_complete_cb = cb;
}

void NetworkManager::onServerOTARequest(std::function<void()> cb)
{
    on_server_ota_request_cb = cb;
}

// Stop captive portal if running (used for low-battery mode)
void NetworkManager::stopPortal()
{
    if (wifi)
    {
        wifi->stopCaptivePortal();
    }
}

// ============================================================================
// WIFI RETRY LOGIC
// ============================================================================
void NetworkManager::retryWifiTaskEntry(void *arg)
{
    auto *self = static_cast<NetworkManager *>(arg);
    if (self)
    {
        self->retryWifiThenBLE();
    }
    vTaskDelete(nullptr);
}

void NetworkManager::retryWifiThenPortal()
{
    const int max_retries = 10; // 5 seconds / 0.5s = 10 attempts

    ESP_LOGI(TAG, "Starting WiFi retry phase (5 seconds, 10 attempts)");

    for (int attempt = 0; attempt < max_retries; attempt++)
    {
        // Check if WiFi connected during retry
        if (wifi && wifi->isConnected())
        {
            ESP_LOGI(TAG, "WiFi connected during retry phase - cancelling portal");
            wifi_retry_task = nullptr;
            return;
        }

        ESP_LOGI(TAG, "WiFi retry attempt %d/10", attempt + 1);

        // Wait 500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Check one final time after all retries
    if (wifi && wifi->isConnected())
    {
        ESP_LOGI(TAG, "WiFi connected after retry phase - cancelling portal");
        wifi_retry_task = nullptr;
        return;
    }

    // 5 seconds elapsed without connection - scan then open portal
    ESP_LOGI(TAG, "WiFi retry phase complete - no connection. Scanning then opening portal...");

    if (wifi)
    {
        // Disconnect STA first to allow scanning
        wifi->disconnect();
        vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay to ensure WiFi is stopped

        // Start STA mode and scan
        wifi->ensureStaStarted();
        vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay before scanning

        // Scan and cache networks before opening portal
        wifi->scanAndCache();

        // Open portal with stop_wifi_first=true to ensure clean state
        wifi->startCaptivePortal(config_.ap_ssid, config_.ap_max_clients, true);
        publishState(state::ConnectivityState::WIFI_PORTAL);
    }

    wifi_retry_task = nullptr;
}

void NetworkManager::retryWifiThenBLE()
{
    const int max_retries = 10;

    ESP_LOGI(TAG, "Starting WiFi retry phase (5 seconds)");

    for (int i = 0; i < max_retries; i++)
    {
        if (wifi && wifi->isConnected())
        {
            ESP_LOGI(TAG, "WiFi connected during retry");
            wifi_retry_task = nullptr;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (wifi && wifi->isConnected())
    {
        ESP_LOGI(TAG, "WiFi connected after retry");
        wifi_retry_task = nullptr;
        return;
    }

    ESP_LOGW(TAG, "WiFi unavailable after retry - switching to BLE config mode");

    // 1. Disconnect first to stop STA from connecting
    if (wifi)
    {
        wifi->disconnect();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for disconnect to complete
    }

    // 2. Start STA and scan networks (no longer connecting, so scan will work)
    if (wifi)
    {
        wifi->ensureStaStarted();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for STA to be ready

        cached_networks = wifi->scanNetworks();
        ESP_LOGI(TAG, "Scanned and cached %d networks before BLE mode", cached_networks.size());
    }

    // 3. Now STOP WiFi completely to free RF for BLE
    if (wifi)
    {
        wifi->disconnect();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for disconnect to complete

        // Properly stop WiFi before deinit
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for WiFi to stop

        esp_wifi_deinit();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for deinit to complete

        ESP_LOGI(TAG, "WiFi fully stopped and deinitialized for BLE");
    }

    // 3. Publish state (BLE will be started later when RAM is freed)
    publishState(state::ConnectivityState::CONFIG_BLE);

    wifi_retry_task = nullptr;
    vTaskDelete(NULL);
}

void NetworkManager::startBLEConfigMode()
{
    if (ble_service)
    {
        ESP_LOGW(TAG, "Start BLE Config Mode now (RAM should be free)");
        ESP_LOGI(TAG, "Passing %d cached networks to BLE service", cached_networks.size());

        // Prepare current device configuration to restore in BLE
        BluetoothService::ConfigData current_cfg;
        current_cfg.device_name = nmgr_load_str("device_name", "PTalk");
        current_cfg.volume = nmgr_load_u8("volume", 60);
        current_cfg.brightness = nmgr_load_u8("brightness", 100);
        current_cfg.ws_url = nmgr_load_str("ws_url", "");

        ble_service->init(config_.ap_ssid, cached_networks, &current_cfg);
        ble_service->start();
    }
}

// Static task entry for deferred BLE config
void NetworkManager::bleConfigTaskEntry(void *arg)
{
    auto *self = static_cast<NetworkManager *>(arg);
    if (self)
    {
        self->openBLEConfigModeDeferred();
    }
    self->ble_config_task = nullptr;
    vTaskDelete(nullptr);
}

void NetworkManager::openBLEConfigMode()
{
    ESP_LOGI(TAG, "Opening BLE config mode - spawning deferred task");

    // CRITICAL: Cannot cleanup WebSocket from within its own callback!
    // Spawn a separate task to handle the cleanup safely
    if (ble_config_task != nullptr)
    {
        ESP_LOGW(TAG, "BLE config task already running");
        return;
    }

    xTaskCreate(
        &NetworkManager::bleConfigTaskEntry,
        "BLEConfig",
        6144,
        this,
        5,
        &ble_config_task);
}

void NetworkManager::openBLEConfigModeDeferred()
{
    ESP_LOGI(TAG, "Opening BLE config mode (deferred task)");

    // 0. STOP NETWORK MANAGER COMPLETELY
    ESP_LOGI(TAG, "Stopping all network operations...");

    started = false; // Signal network loop task to exit gracefully
    ws_should_run = false;
    ws_running = false;

    // Wait for network loop task to exit gracefully (started=false signals it)
    // Task will set task_handle = nullptr before self-deleting
    vTaskDelay(pdMS_TO_TICKS(500));

    // Now safe to destroy WebSocket from outside callback context
    if (ws)
    {
        ESP_LOGI(TAG, "Destroying WebSocket...");
        ws->close();                    // This can now safely destroy the client
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for WebSocket task to fully terminate
        ESP_LOGI(TAG, "WebSocket destroyed");
    }
    // Stop mqtt client
    if (mqtt)
    {
        ESP_LOGI(TAG, "Stopping MQTT client...");
        mqtt->stop();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for MQTT to fully terminate
        ESP_LOGI(TAG, "MQTT client stopped");
    }

    // Only try to delete task if it hasn't already self-deleted
    // (task clears its own handle before vTaskDelete(nullptr))
    if (task_handle)
    {
        ESP_LOGI(TAG, "Force deleting network task...");
        TaskHandle_t th = task_handle;
        task_handle = nullptr;
        vTaskDelete(th);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else
    {
        ESP_LOGI(TAG, "Network task already exited");
    }

    ESP_LOGI(TAG, "Network operations stopped");

    // 1. Disconnect WiFi first to stop STA from connecting
    if (wifi)
    {
        wifi->disconnect();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for disconnect to complete
    }

    // 2. Start STA and scan networks (no longer connecting, so scan will work)
    if (wifi)
    {
        wifi->ensureStaStarted();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for STA to be ready

        cached_networks = wifi->scanNetworks();
        ESP_LOGI(TAG, "Scanned and cached %d networks before BLE mode", cached_networks.size());
    }

    // 3. Now STOP WiFi completely to free RF for BLE
    if (wifi)
    {
        wifi->disconnect();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for disconnect to complete

        // Properly stop WiFi before deinit
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for WiFi to stop

        esp_wifi_deinit();
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for deinit to complete

        ESP_LOGI(TAG, "WiFi fully stopped and deinitialized for BLE");
    }

    // 4. Publish state (BLE will be started later when RAM is freed)
    publishState(state::ConnectivityState::CONFIG_BLE);

    ESP_LOGI(TAG, "BLE config mode ready - call startBLEConfigMode() when RAM is freed");
}

// ============================================================================
// EMOTION CODE PARSING
// ============================================================================
state::EmotionState NetworkManager::parseEmotionCode(const std::string &code)
{
    // Map WebSocket emotion codes to EmotionState
    // Format expected: "01", "11", etc. (2-char codes)

    if (code == "00" || code.empty())
    {
        return state::EmotionState::NEUTRAL;
    }
    if (code == "01")
    {
        return state::EmotionState::HAPPY; // Happy, cheerful
    }
    if (code == "02")
    {
        return state::EmotionState::ANGRY; // Angry, urgent
    }
    if (code == "03")
    {
        return state::EmotionState::EXCITED; // Excited, enthusiastic
    }
    if (code == "10")
    {
        return state::EmotionState::SAD; // Sad, empathetic
    }
    if (code == "12")
    {
        return state::EmotionState::CONFUSED; // Confused, uncertain
    }
    if (code == "13")
    {
        return state::EmotionState::CALM; // Calm, soothing
    }
    if (code == "99")
    {
        return state::EmotionState::THINKING; // Thinking, processing
    }

    ESP_LOGW(TAG, "Unknown emotion code: %s", code.c_str());
    return state::EmotionState::NEUTRAL;
}
// ============================================================================
// REAL-TIME WEBSOCKET CONFIGURATION
// ============================================================================

static bool nmgr_save_u8(const char *key, uint8_t value)
{
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

static bool nmgr_save_str(const char *key, const std::string &value)
{
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_set_str(h, key, value.c_str());
    nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

static uint8_t nmgr_load_u8(const char *key, uint8_t def_val)
{
    uint8_t v = def_val;
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_u8(h, key, &v);
        nvs_close(h);
    }
    return v;
}

static std::string nmgr_load_str(const char *key, const char *def_val)
{
    std::string out;
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK)
    {
        size_t required = 0;
        if (nvs_get_str(h, key, nullptr, &required) == ESP_OK && required > 0)
        {
            out.resize(required);
            if (nvs_get_str(h, key, out.data(), &required) == ESP_OK)
            {
                if (!out.empty() && out.back() == '\0')
                    out.pop_back();
            }
        }
        nvs_close(h);
    }
    if (out.empty())
        out = def_val;
    return out;
}

void NetworkManager::setManagers(class AudioManager *audio, class DisplayManager *display)
{
    audio_manager = audio;
    display_manager = display;
}

void NetworkManager::onConfigUpdate(std::function<void(const std::string &, const std::string &)> cb)
{
    on_config_update_cb = cb;
}

bool NetworkManager::sendDeviceHandshake()
{
    if (!mqtt || !mqtt->isConnected())
        return false;

    std::string status_json = getCurrentStatusJson();
    return mqtt->publish(mqtt_base_topic + "/status", status_json, 1, true); // Retain = true
}

void NetworkManager::handleConfigCommand(const std::string &json_msg)
{
    // ✅ THÊM: Check MQTT connection trước khi xử lý
    if (!mqtt || !mqtt->isConnected())
    {
        ESP_LOGW(TAG, "MQTT not connected, ignoring command");
        return;
    }

    cJSON *root = cJSON_Parse(json_msg.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Invalid JSON config command: %s", json_msg.c_str());
        return;
    }

    cJSON *cmd_obj = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_obj || !cmd_obj->valuestring)
    {
        ESP_LOGE(TAG, "Config command missing 'cmd' field");
        cJSON_Delete(root);
        return;
    }

    std::string cmd_str = cmd_obj->valuestring;
    mqtt_config::ConfigCommand cmd = mqtt_config::parseCommandString(cmd_str);

    ESP_LOGI(TAG, "Processing MQTT config command: %s", cmd_str.c_str());

    // Process command
    switch (cmd)
    {
    case mqtt_config::ConfigCommand::SET_AUDIO_VOLUME:
    {
        cJSON *vol_obj = cJSON_GetObjectItem(root, "volume");
        if (vol_obj && vol_obj->type == cJSON_Number)
        {
            uint8_t volume = (uint8_t)vol_obj->valueint;
            applyVolumeConfig(volume);
        }
        break;
    }

    case mqtt_config::ConfigCommand::SET_BRIGHTNESS:
    {
        cJSON *bright_obj = cJSON_GetObjectItem(root, "brightness");
        if (bright_obj && bright_obj->type == cJSON_Number)
        {
            uint8_t brightness = (uint8_t)bright_obj->valueint;
            applyBrightnessConfig(brightness);
        }
        break;
    }

    case mqtt_config::ConfigCommand::SET_DEVICE_NAME:
    {
        cJSON *name_obj = cJSON_GetObjectItem(root, "device_name");
        if (name_obj && name_obj->valuestring)
        {
            applyDeviceNameConfig(name_obj->valuestring);
        }
        break;
    }

    case mqtt_config::ConfigCommand::SET_WIFI:
    {
        // Wi‑Fi configuration is NOT allowed via WebSocket; use BLE portal instead.
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", mqtt_config::statusToString(mqtt_config::ResponseStatus::NOT_SUPPORTED));
        cJSON_AddStringToObject(resp, "message", "WiFi config not supported over WebSocket. Use BLE.");
        char *resp_str = cJSON_Print(resp);
        mqtt->publish(mqtt_base_topic + "/status", resp_str, 1, false);
        cJSON_free(resp_str);
        cJSON_Delete(resp);
        break;
    }

    case mqtt_config::ConfigCommand::REQUEST_STATUS:
    {
        // Send current device status
        std::string status_json = getCurrentStatusJson();
        mqtt->publish(mqtt_base_topic + "/status", status_json.c_str(), 1, false);
        break;
    }

    case mqtt_config::ConfigCommand::REBOOT:
    {
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "status", "ok");
        cJSON_AddStringToObject(ack, "message", "Rebooting...");
        char *ack_str = cJSON_Print(ack);
        mqtt->publish(mqtt_base_topic + "/status", ack_str, 1, false);    
        cJSON_free(ack_str);
        cJSON_Delete(ack);

        // Delay before reboot to send ack
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        break;
    }

    case mqtt_config::ConfigCommand::REQUEST_OTA:
    {
        // Server is initiating OTA - prepare to receive binary data
        uint32_t fw_size = 0;
        cJSON *size_obj = cJSON_GetObjectItem(root, "size");
        if (size_obj && cJSON_IsNumber(size_obj))
        {
            fw_size = static_cast<uint32_t>(size_obj->valuedouble);
        }

        std::string fw_sha256;
        cJSON *sha_obj = cJSON_GetObjectItem(root, "sha256");
        if (sha_obj && sha_obj->valuestring)
        {
            fw_sha256 = sha_obj->valuestring;
        }

        // Parse chunk protocol info
        uint32_t chunk_size = 2048; // Default
        cJSON *chunk_obj = cJSON_GetObjectItem(root, "chunk_size");
        if (chunk_obj && cJSON_IsNumber(chunk_obj))
        {
            chunk_size = static_cast<uint32_t>(chunk_obj->valuedouble);
        }

        uint32_t total_chunks = 0;
        cJSON *total_obj = cJSON_GetObjectItem(root, "total_chunks");
        if (total_obj && cJSON_IsNumber(total_obj))
        {
            total_chunks = static_cast<uint32_t>(total_obj->valuedouble);
        }

        // Setup OTA state to receive binary data
        firmware_download_active = true;
        firmware_bytes_received = 0;
        firmware_expected_size = fw_size;
        firmware_expected_sha256 = fw_sha256;

        // Initialize chunk protocol state
        ota_expected_seq = 0;
        ota_chunk_size = chunk_size;
        ota_total_chunks = total_chunks;
        ota_chunks_received = 0;
        ota_chunks_failed = 0;

        ESP_LOGI(TAG, "OTA initiated: size=%u, chunks=%u, chunk_size=%u, sha256=%s",
                 fw_size, total_chunks, chunk_size, fw_sha256.c_str());

        // CRITICAL: Notify AppController to setup OTA callbacks BEFORE sending ACK
        // This ensures callbacks are registered before binary data arrives
        if (on_server_ota_request_cb)
        {
            ESP_LOGI(TAG, "Calling server OTA request callback to setup handlers...");
            on_server_ota_request_cb();
        }
        else
        {
            ESP_LOGW(TAG, "No server OTA request callback registered - OTA may fail!");
        }

        // Send ACK - ready to receive firmware
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddStringToObject(resp, "message", "Ready to receive firmware");
        if (fw_size > 0)
            cJSON_AddNumberToObject(resp, "size", fw_size);
        if (!fw_sha256.empty())
            cJSON_AddStringToObject(resp, "sha256", fw_sha256.c_str());
        cJSON_AddStringToObject(resp, "device_id", getDeviceEfuseID().c_str());

        char *resp_str = cJSON_Print(resp);
        mqtt->publish(mqtt_base_topic + "/status", resp_str, 1, false);
        cJSON_free(resp_str);
        cJSON_Delete(resp);
        break;
    }

    case mqtt_config::ConfigCommand::REQUEST_BLE_CONFIG:
    {
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "status", "ok");
        cJSON_AddStringToObject(ack, "message", "Opening BLE config mode...");
        cJSON_AddStringToObject(ack, "device_id", getDeviceEfuseID().c_str());
        char *ack_str = cJSON_Print(ack);
        mqtt->publish(mqtt_base_topic + "/status", ack_str, 1, false);
        cJSON_free(ack_str);
        cJSON_Delete(ack);

        // Delay before opening BLE to send ack
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Open BLE config mode (this will scan WiFi and prepare for BLE)
        openBLEConfigMode();
        break;
    }

    default:
        ESP_LOGW(TAG, "Unknown config command: %s", cmd_str.c_str());
        break;
    }

    cJSON_Delete(root);
}

bool NetworkManager::applyVolumeConfig(uint8_t volume)
{
    if (volume > 100)
        volume = 100;
    ESP_LOGI(TAG, "Applying volume config: %d%%", volume);
    if (audio_manager)
        audio_manager->setVolume(volume);
    nmgr_save_u8("volume", volume);

    // Gửi phản hồi qua MQTT thay vì WS
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "volume", volume);
    char *resp_str = cJSON_PrintUnformatted(response);

    mqtt->publish(mqtt_base_topic + "/status", resp_str, 1, false);

    free(resp_str);
    cJSON_Delete(response);
    return true;
}

bool NetworkManager::applyBrightnessConfig(uint8_t brightness)
{
    if (brightness > 100)
        brightness = 100;

    ESP_LOGI(TAG, "Applying brightness config: %d%%", brightness);

    if (display_manager)
    {
        display_manager->setBrightness(brightness);
    }

    // Persist to NVS for next boot
    nmgr_save_u8("brightness", brightness);

    // Send response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "brightness", brightness);
    char *resp_str = cJSON_PrintUnformatted(response);

    mqtt->publish(mqtt_base_topic + "/status", resp_str, 1, false);

    free(resp_str);
    cJSON_Delete(response);

    return true;
}

bool NetworkManager::applyDeviceNameConfig(const std::string &name)
{
    ESP_LOGI(TAG, "Applying device name config: %s", name.c_str());

    // Persist to NVS for next boot
    nmgr_save_str("device_name", name);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "device_name", name.c_str());
    char *resp_str = cJSON_PrintUnformatted(response);

    mqtt->publish(mqtt_base_topic + "/status", resp_str, 1, false);

    free(resp_str);
    cJSON_Delete(response);

    return true;
}

bool NetworkManager::applyWiFiConfig(const std::string &ssid, const std::string &password)
{
    ESP_LOGI(TAG, "Applying WiFi config: SSID=%s", ssid.c_str());

    // Save to NVS (via setCredentials)
    setCredentials(ssid, password);

    // Send response before restart
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "WiFi configured, restarting...");
    char *resp_str = cJSON_PrintUnformatted(response);

    mqtt->publish(mqtt_base_topic + "/status", resp_str, 1, false);

    free(resp_str);
    cJSON_Delete(response);

    // Delay before restart
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return true;
}

std::string NetworkManager::getCurrentStatusJson() const
{
    std::string device_id = getDeviceEfuseID();
    std::string app_version = app_meta::APP_VERSION;
    std::string device_name = nmgr_load_str("device_name", "PTalk");
    uint8_t volume = nmgr_load_u8("volume", 60);
    uint8_t brightness = nmgr_load_u8("brightness", 100);
    uint8_t battery = power_manager ? power_manager->getPercent() : 85;
    uint32_t uptime_sec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "device_id", device_id.c_str());
    cJSON_AddStringToObject(root, "device_name", device_name.c_str());
    cJSON_AddNumberToObject(root, "battery_percent", battery);
    cJSON_AddStringToObject(root, "connectivity_state", "ONLINE");
    cJSON_AddStringToObject(root, "firmware_version", app_version.c_str());
    cJSON_AddNumberToObject(root, "volume", volume);         // From NVS (WS/BLE persisted)
    cJSON_AddNumberToObject(root, "brightness", brightness); // From NVS (WS/BLE persisted)
    cJSON_AddNumberToObject(root, "uptime_sec", uptime_sec);

    char *json_str = cJSON_Print(root);
    std::string result(json_str);

    cJSON_free(json_str);
    cJSON_Delete(root);

    return result;
}

// ============================================================================
// MEO FEATURE LAYER IMPLEMENTATION
// ============================================================================

void NetworkManager::registerFeature(const std::string& name, meo::FeatureHandler handler)
{
    feature_manager_.registerFeature(name, handler);
}

void NetworkManager::unregisterFeature(const std::string& name)
{
    feature_manager_.unregisterFeature(name);
}

bool NetworkManager::publishEvent(const std::string& event_name, const meo::FeatureParams& data)
{
    if (!mqtt || !mqtt->isConnected())
    {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish event: %s", event_name.c_str());
        return false;
    }

    meo::DeviceEvent event;
    event.event_name = event_name;
    event.device_id = meo_device_id_;
    event.data = data;

    std::string json = meo::MeoFeatureManager::serializeEvent(event);
    std::string topic = meo_event_topic_ + "/" + event_name;

    ESP_LOGI(TAG, "Publishing MEO event: %s → %s", event_name.c_str(), topic.c_str());
    return mqtt->publish(topic, json, 1, false);
}

bool NetworkManager::publishFeatureResponse(const meo::FeatureResponse& response)
{
    if (!mqtt || !mqtt->isConnected())
    {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish feature response");
        return false;
    }

    std::string json = meo::MeoFeatureManager::serializeResponse(response);
    std::string topic = meo_event_topic_ + "/" + meo::events::FEATURE_RESPONSE;

    ESP_LOGI(TAG, "Publishing feature response: %s → %s", 
             response.feature_name.c_str(), topic.c_str());
    return mqtt->publish(topic, json, 1, false);
}

void NetworkManager::handleMeoFeatureInvoke(const std::string& payload, const std::string& feature_from_topic)
{
    ESP_LOGI(TAG, "Handling MEO feature invoke (topic_feature=%s)", feature_from_topic.c_str());

    // Parse the invoke payload
    meo::FeatureCall call = meo::MeoFeatureManager::parseInvokePayload(payload, feature_from_topic);
    call.device_id = meo_device_id_;

    if (call.feature_name.empty())
    {
        ESP_LOGW(TAG, "Feature invoke missing feature name");
        meo::FeatureResponse error_resp{
            "unknown",
            meo_device_id_,
            false,
            "Missing feature name in invoke",
            call.invoke_id,
            {}
        };
        publishFeatureResponse(error_resp);
        return;
    }

    // Invoke the feature handler
    meo::FeatureResponse response = feature_manager_.invokeFeature(call);

    // Publish the response
    publishFeatureResponse(response);
}

void NetworkManager::registerBuiltinFeatures()
{
    ESP_LOGI(TAG, "Registering built-in MEO features");

    // ============================================================
    // Device Configuration Features
    // ============================================================

    registerFeature(meo::features::SET_VOLUME, [this](const meo::FeatureCall& call) {
        uint8_t volume = 60;
        auto it = call.params.find("volume");
        if (it != call.params.end())
        {
            volume = static_cast<uint8_t>(std::stoi(it->second));
        }
        
        bool success = applyVolumeConfig(volume);
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            success,
            success ? "Volume set to " + std::to_string(volume) : "Failed to set volume",
            call.invoke_id,
            {{"volume", std::to_string(volume)}}
        };
    });

    registerFeature(meo::features::SET_BRIGHTNESS, [this](const meo::FeatureCall& call) {
        uint8_t brightness = 100;
        auto it = call.params.find("brightness");
        if (it != call.params.end())
        {
            brightness = static_cast<uint8_t>(std::stoi(it->second));
        }
        
        bool success = applyBrightnessConfig(brightness);
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            success,
            success ? "Brightness set to " + std::to_string(brightness) : "Failed to set brightness",
            call.invoke_id,
            {{"brightness", std::to_string(brightness)}}
        };
    });

    registerFeature(meo::features::SET_DEVICE_NAME, [this](const meo::FeatureCall& call) {
        std::string name = "PTalk";
        auto it = call.params.find("device_name");
        if (it != call.params.end())
        {
            name = it->second;
        }
        
        bool success = applyDeviceNameConfig(name);
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            success,
            success ? "Device name set to " + name : "Failed to set device name",
            call.invoke_id,
            {{"device_name", name}}
        };
    });

    // ============================================================
    // Device Control Features
    // ============================================================

    registerFeature(meo::features::REQUEST_STATUS, [this](const meo::FeatureCall& call) {
        std::string status = getCurrentStatusJson();
        
        // Parse status JSON and add to response data
        meo::FeatureParams data;
        cJSON* root = cJSON_Parse(status.c_str());
        if (root)
        {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, root)
            {
                if (item->string)
                {
                    if (cJSON_IsString(item))
                        data[item->string] = item->valuestring;
                    else if (cJSON_IsNumber(item))
                    {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d", item->valueint);
                        data[item->string] = buf;
                    }
                }
            }
            cJSON_Delete(root);
        }
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            true,
            "Status retrieved",
            call.invoke_id,
            data
        };
    });

    registerFeature(meo::features::REBOOT, [this](const meo::FeatureCall& call) {
        ESP_LOGI(TAG, "Reboot requested via MEO feature");
        
        // Schedule reboot after response is sent
        xTaskCreate([](void*) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }, "reboot", 2048, nullptr, 1, nullptr);
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            true,
            "Rebooting...",
            call.invoke_id,
            {}
        };
    });

    registerFeature(meo::features::REQUEST_BLE_CONFIG, [this](const meo::FeatureCall& call) {
        ESP_LOGI(TAG, "BLE config mode requested via MEO feature");
        
        // Trigger BLE config mode (deferred)
        openBLEConfigMode();
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            true,
            "Opening BLE config mode...",
            call.invoke_id,
            {}
        };
    });

    registerFeature(meo::features::SET_WIFI, [this](const meo::FeatureCall& call) {
        // WiFi config not supported over MQTT for security
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            false,
            "WiFi config not supported over MQTT. Use BLE provisioning.",
            call.invoke_id,
            {}
        };
    });

    // ============================================================
    // PTalk-Specific Features
    // ============================================================

    registerFeature(meo::features::SET_EMOTION, [this](const meo::FeatureCall& call) {
        std::string emotion_code = "00";
        auto it = call.params.find("code");
        if (it != call.params.end())
        {
            emotion_code = it->second;
        }
        
        auto emotion = parseEmotionCode(emotion_code);
        StateManager::instance().setEmotionState(emotion);
        
        return meo::FeatureResponse{
            call.feature_name,
            call.device_id,
            true,
            "Emotion set",
            call.invoke_id,
            {{"emotion_code", emotion_code}}
        };
    });

    ESP_LOGI(TAG, "Registered %d built-in features", 
             (int)feature_manager_.getRegisteredFeatures().size());
}