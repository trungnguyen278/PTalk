#pragma once

#include <string>
#include <functional>
#include <cstdint>

#include "esp_err.h"
#include "mqtt_client.h"

/**
 * @brief Lightweight MQTT client for JSON control only
 *
 * - One TCP connection
 * - QoS 0
 * - Small buffers
 * - Auto reconnect
 */
class MqttClient
{
public:
    MqttClient();
    ~MqttClient();

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------
    void init();
    void setUri(const std::string& uri);      // mqtt://host:port
    void setClientId(const std::string& id);  // unique device id
    void setUsername(const std::string& user); // MQTT username (device_id)
    void setPassword(const std::string& pass); // MQTT password (tx_key)
    void setCredentials(const std::string& user, const std::string& pass);
    void start();
    void stop();

    // ------------------------------------------------------------------
    // Pub / Sub
    // ------------------------------------------------------------------
    bool publish(const std::string& topic,
                 const std::string& json,
                 int qos = 0,
                 bool retain = false);

    bool subscribe(const std::string& topic, int qos = 0);

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------
    void onConnected(std::function<void()> cb);
    void onDisconnected(std::function<void()> cb);
    void onMessage(std::function<void(const std::string& topic,
                                       const std::string& payload)> cb);

    bool isConnected() const;

private:
    // ------------------------------------------------------------------
    // Internal
    // ------------------------------------------------------------------
    static void eventHandlerStatic(void* handler_args,
                                   esp_event_base_t base,
                                   int32_t event_id,
                                   void* event_data);

    void eventHandler(esp_mqtt_event_handle_t event);

private:
    esp_mqtt_client_handle_t client_ = nullptr;

    std::string uri_;
    std::string client_id_;
    std::string username_;    // MQTT username (typically device_id)
    std::string password_;    // MQTT password (tx_key from provisioning)

    bool connected_ = false;

    std::function<void()> connected_cb_;
    std::function<void()> disconnected_cb_;
    std::function<void(const std::string&,
                       const std::string&)> message_cb_;
};
