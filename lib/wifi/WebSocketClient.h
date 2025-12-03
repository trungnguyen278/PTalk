#pragma once

#include <string>
#include <functional>
#include "../components/esp_websocket_client/include/esp_websocket_client.h"
#include "esp_event.h"
#include "esp_log.h"

class WebSocketClient {
public:
    explicit WebSocketClient(const std::string& uri) : server_uri(uri) {}

    void init() {
        esp_websocket_client_config_t cfg = {};
        cfg.uri = server_uri.c_str();

        client = esp_websocket_client_init(&cfg);
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, event_handler, this);
    }

    void connect() {
        if (client) {
            esp_websocket_client_start(client);
        }
    }

    void disconnect() {
        if (client) {
            esp_websocket_client_stop(client);
        }
    }

    bool isConnected() const {
        return client && esp_websocket_client_is_connected(client);
    }

    void sendText(const std::string& msg) {
        if (isConnected()) {
            esp_websocket_client_send_text(client, msg.c_str(), msg.size(), portMAX_DELAY);
        }
    }

    void sendBinary(const uint8_t* data, size_t len) {
        if (isConnected()) {
            esp_websocket_client_send_bin(client, (const char*)data, len, portMAX_DELAY);
        }
    }

    // ==== Event callbacks ====
    void onConnected(std::function<void()> cb) { connected_cb = cb; }
    void onDisconnected(std::function<void()> cb) { disconnected_cb = cb; }
    void onTextMessage(std::function<void(const std::string&)> cb) { text_cb = cb; }
    void onBinaryMessage(std::function<void(const uint8_t*, size_t)> cb) { bin_cb = cb; }
    void onError(std::function<void()> cb) { error_cb = cb; }

private:
    static void event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        WebSocketClient* self = static_cast<WebSocketClient*>(handler_args);
        auto* data = (esp_websocket_event_data_t*)event_data;

        switch (event_id) {
            case WEBSOCKET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "WebSocket connected");
                if (self->connected_cb) self->connected_cb();
                break;

            case WEBSOCKET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "WebSocket disconnected");
                if (self->disconnected_cb) self->disconnected_cb();
                break;

            case WEBSOCKET_EVENT_DATA:
                if (data->op_code == WS_TRANSPORT_OPCODES_TEXT && self->text_cb) {
                    std::string msg(data->data_ptr, data->data_ptr + data->data_len);
                    self->text_cb(msg);
                }
                else if (data->op_code == WS_TRANSPORT_OPCODES_BINARY && self->bin_cb) {
                    self->bin_cb((const uint8_t*)data->data_ptr, data->data_len);
                }
                break;

            case WEBSOCKET_EVENT_ERROR:
                ESP_LOGE(TAG, "WebSocket error");
                if (self->error_cb) self->error_cb();
                break;
        }
    }

    std::string server_uri;
    esp_websocket_client_handle_t client = nullptr;

    std::function<void()> connected_cb;
    std::function<void()> disconnected_cb;
    std::function<void(const std::string&)> text_cb;
    std::function<void(const uint8_t*, size_t)> bin_cb;
    std::function<void()> error_cb;

    static constexpr const char* TAG = "WebSocketClient";
};
