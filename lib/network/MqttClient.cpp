#include "MqttClient.hpp"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"

static const char* TAG = "MqttClient";

MqttClient::MqttClient() = default;

MqttClient::~MqttClient()
{
    stop();
}

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

void MqttClient::init()
{
    // Nothing heavy here
}

void MqttClient::setUri(const std::string& uri)
{
    uri_ = uri;
}

void MqttClient::setClientId(const std::string& id)
{
    client_id_ = id;
}

void MqttClient::setUsername(const std::string& user)
{
    username_ = user;
}

void MqttClient::setPassword(const std::string& pass)
{
    password_ = pass;
}

void MqttClient::setCredentials(const std::string& user, const std::string& pass)
{
    username_ = user;
    password_ = pass;
}

void MqttClient::start()
{
    if (uri_.empty())
    {
        ESP_LOGE(TAG, "MQTT URI not set");
        return;
    }

    if (client_)
    {
        ESP_LOGW(TAG, "MQTT already started");
        return;
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.uri = uri_.c_str();
    cfg.client_id = client_id_.empty() ? nullptr : client_id_.c_str();
    
    // -------------------------------
    // MQTT Authentication (MEO tx_key)
    // -------------------------------
    cfg.username = username_.empty() ? nullptr : username_.c_str();
    cfg.password = password_.empty() ? nullptr : password_.c_str();

    // -------------------------------
    // Memory-conscious configuration
    // -------------------------------
    cfg.buffer_size = 4096;      // Reduced from 8KB
    cfg.out_buffer_size = 512;
    cfg.keepalive = 60;
    cfg.disable_auto_reconnect = false;
    cfg.reconnect_timeout_ms = 2000;

    // -------------------------------
    // LWT (optional but useful)
    // -------------------------------
    cfg.lwt_topic = nullptr; // set later if needed
    cfg.lwt_msg = nullptr;
    cfg.lwt_qos = 0;
    cfg.lwt_retain = 0;

    ESP_LOGI(TAG, "Free heap before mqtt_start: %u", esp_get_free_heap_size());

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_)
    {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        client_,
        MQTT_EVENT_ANY,
        &MqttClient::eventHandlerStatic,
        this);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register MQTT events");
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return;
    }

    esp_mqtt_client_start(client_);

    ESP_LOGI(TAG, "MQTT started: %s", uri_.c_str());
    ESP_LOGI(TAG, "Free heap after mqtt_start: %u", esp_get_free_heap_size());
}

void MqttClient::stop()
{
    if (!client_)
        return;

    ESP_LOGI(TAG, "Stopping MQTT client");

    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);

    client_ = nullptr;
    connected_ = false;

    if (disconnected_cb_)
        disconnected_cb_();
}

bool MqttClient::publish(const std::string& topic,
                         const std::string& json,
                         int qos,
                         bool retain)
{
    if (!client_ || !connected_)
        return false;

    int msg_id = esp_mqtt_client_publish(
        client_,
        topic.c_str(),
        json.c_str(),
        json.size(),
        qos,
        retain);

    return msg_id >= 0;
}

bool MqttClient::subscribe(const std::string& topic, int qos)
{
    if (!client_ || !connected_)
        return false;

    int msg_id = esp_mqtt_client_subscribe(
        client_,
        topic.c_str(),
        qos);

    return msg_id >= 0;
}

void MqttClient::onConnected(std::function<void()> cb)
{
    connected_cb_ = cb;
}

void MqttClient::onDisconnected(std::function<void()> cb)
{
    disconnected_cb_ = cb;
}

void MqttClient::onMessage(
    std::function<void(const std::string&, const std::string&)> cb)
{
    message_cb_ = cb;
}

bool MqttClient::isConnected() const
{
    return connected_;
}

// ------------------------------------------------------------------
// Event handling
// ------------------------------------------------------------------

void MqttClient::eventHandlerStatic(void* handler_args,
                                   esp_event_base_t,
                                   int32_t,
                                   void* event_data)
{
    auto* self = static_cast<MqttClient*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    self->eventHandler(event);
}

void MqttClient::eventHandler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        connected_ = true;
        if (connected_cb_)
            connected_cb_();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        connected_ = false;
        if (disconnected_cb_)
            disconnected_cb_();
        break;

    case MQTT_EVENT_DATA:
        if (message_cb_)
        {
            std::string topic;
            std::string payload;

            if (event->topic && event->topic_len > 0)
                topic.assign(event->topic, event->topic_len);

            if (event->data && event->data_len > 0)
                payload.assign(event->data, event->data_len);

            message_cb_(topic, payload);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}
