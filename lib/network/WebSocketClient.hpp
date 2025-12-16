#pragma once

#include <string>
#include <functional>
#include <vector>
#include "esp_websocket_client.h"

/**
 * WebSocketClient
 * ---------------------------------------------------------
 * - Đóng gói esp_websocket_client
 * - Tự động callback status / text / binary
 * - Không xử lý logic ứng dụng (NetworkManager làm việc đó)
 */
class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // Init sơ bộ, chưa connect
    void init();

    // Kết nối tới ws_url (thiết lập trong setUrl)
    void connect();
    void close();

    // Get connection status
    bool isConnected() const { return connected; }

    void setUrl(const std::string& url);

    // Send
    bool sendText(const std::string& msg);
    bool sendBinary(const uint8_t* data, size_t len);

    // Callbacks
    void onStatus(std::function<void(int)> cb);   // 0=closed,1=connecting,2=open
    void onText(std::function<void(const std::string&)> cb);
    void onBinary(std::function<void(const uint8_t*, size_t)> cb);

private:
    static void eventHandlerStatic(void* handler_args, esp_event_base_t base,
                                   int32_t event_id, void* event_data);

    void eventHandler(esp_event_base_t base, int32_t event_id,
                      esp_websocket_event_data_t* data);

private:
    esp_websocket_client_handle_t client = nullptr;

    std::string ws_url;

    bool connected = false;

    // callbacks
    std::function<void(int)> status_cb;               // status
    std::function<void(const std::string&)> text_cb;  // text message
    std::function<void(const uint8_t*, size_t)> binary_cb; // binary message
};
