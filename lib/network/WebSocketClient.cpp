#include "WebSocketClient.hpp"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"

static const char* TAG = "WebSocketClient";

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() {
    close();
}

void WebSocketClient::init() {
    // Nothing heavy here. Actual client created in connect()
}

void WebSocketClient::setUrl(const std::string& url) {
    ws_url = url;
}

void WebSocketClient::connect() {
    if (ws_url.empty()) {
        ESP_LOGE(TAG, "WebSocket URL not set");
        return;
    }

    if (client != nullptr) {
        ESP_LOGW(TAG, "WS already created, closing old instance");
        close();
    }

    esp_websocket_client_config_t cfg = {};
    cfg.uri = ws_url.c_str();
    cfg.buffer_size = 4096;  // Reduced from 8KB - we now have freed 115KB from Framebuffer removal
    cfg.disable_auto_reconnect = true;

    client = esp_websocket_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init websocket");
        return;
    }

    ESP_ERROR_CHECK(esp_websocket_register_events(
        client,
        WEBSOCKET_EVENT_ANY,
        &WebSocketClient::eventHandlerStatic,
        this
    ));

    ESP_LOGI(TAG, "Free heap before ws_start: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Connecting to WS: %s", ws_url.c_str());
    esp_websocket_client_start(client);
    ESP_LOGI(TAG, "Free heap after ws_start: %d bytes", esp_get_free_heap_size());

    if (status_cb) status_cb(1); // CONNECTING
}

void WebSocketClient::close() {
    if (client) {
        ESP_LOGI(TAG, "Free heap before close: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Closing WebSocket...");
        esp_websocket_client_close(client, 100);
        // Give internal task time to cleanup
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_websocket_client_destroy(client);
        client = nullptr;
        connected = false;
        ESP_LOGI(TAG, "Free heap after close: %d bytes", esp_get_free_heap_size());
    }

    if (status_cb) status_cb(0); // CLOSED
}

bool WebSocketClient::sendText(const std::string& msg) {
    if (!client || !connected) return false;

    int sent = esp_websocket_client_send_text(client, msg.c_str(), msg.size(), 100);
    return sent == msg.size();
}

bool WebSocketClient::sendBinary(const uint8_t* data, size_t len) {
    if (!client || !connected) return false;

    int sent = esp_websocket_client_send_bin(client, (const char*)data, len, 100);
    return sent == (int)len;
}

void WebSocketClient::onStatus(std::function<void(int)> cb) {
    status_cb = cb;
}

void WebSocketClient::onText(std::function<void(const std::string&)> cb) {
    text_cb = cb;
}

void WebSocketClient::onBinary(std::function<void(const uint8_t*, size_t)> cb) {
    binary_cb = cb;
}

// ======================================================================================
// EVENT HANDLER
// ======================================================================================
void WebSocketClient::eventHandlerStatic(void* handler_args, esp_event_base_t base,
                                         int32_t event_id, void* event_data) {
    auto* self = static_cast<WebSocketClient*>(handler_args);
    self->eventHandler(base, event_id, (esp_websocket_event_data_t*)event_data);
}

void WebSocketClient::eventHandler(esp_event_base_t base, int32_t event_id,
                                   esp_websocket_event_data_t* data)
{
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected!");
        connected = true;
        if (status_cb) status_cb(2);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!data) break;

        if (data->op_code == 0x1) {  // WS_OP_TEXT
            if (text_cb) {
                text_cb(std::string((const char*)data->data_ptr, data->data_len));
            }
        } else if (data->op_code == 0x2) { // WS_OP_BINARY
            if (binary_cb) {
                binary_cb((const uint8_t*)data->data_ptr, data->data_len);
            }
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected");
        connected = false;
        if (status_cb) status_cb(0);
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error event");
        connected = false;
        if (status_cb) status_cb(0);
        break;

    default:
        break;
    }
}
