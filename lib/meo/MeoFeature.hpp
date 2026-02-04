#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>

/**
 * @brief MEO Feature Layer
 * 
 * Implements the MEO SDK feature invoke/event model:
 * - Feature invoke: Server → Device (execute a feature with params)
 * - Feature event: Device → Server (publish feature response/events)
 * 
 * Topic patterns:
 * - Cloud invoke: meo/{userId}/{deviceId}/feature
 * - Legacy invoke: meo/{deviceId}/feature/{featureName}/invoke
 * - Event publish: meo/{userId}/{deviceId}/event/{eventName}
 * - Feature response: meo/{userId}/{deviceId}/event/feature_response
 */

namespace meo
{
    // =========================================================================
    // DATA STRUCTURES
    // =========================================================================

    /**
     * @brief Parameters passed to a feature handler
     * Maps parameter names to string values
     */
    using FeatureParams = std::map<std::string, std::string>;

    /**
     * @brief Feature invocation request
     */
    struct FeatureCall
    {
        std::string device_id;      // Device receiving the invoke
        std::string feature_name;   // Name of the feature to invoke
        FeatureParams params;       // Parameters for the feature
        std::string invoke_id;      // Optional: correlation ID for response
    };

    /**
     * @brief Feature response sent back to server
     */
    struct FeatureResponse
    {
        std::string feature_name;   // Name of the feature
        std::string device_id;      // Device ID
        bool success;               // Whether the invoke succeeded
        std::string message;        // Optional message/result
        std::string invoke_id;      // Correlation ID (if provided in invoke)
        FeatureParams data;         // Optional response data
    };

    /**
     * @brief Event published by device
     */
    struct DeviceEvent
    {
        std::string event_name;     // Name of the event
        std::string device_id;      // Device ID
        FeatureParams data;         // Event payload data
    };

    // =========================================================================
    // FEATURE HANDLER
    // =========================================================================

    /**
     * @brief Callback type for feature handlers
     * @param call The feature invocation details
     * @return FeatureResponse to send back to server
     */
    using FeatureHandler = std::function<FeatureResponse(const FeatureCall& call)>;

    // =========================================================================
    // BUILT-IN FEATURE NAMES (PTalk specific)
    // =========================================================================
    
    namespace features
    {
        // Device configuration features
        constexpr const char* SET_VOLUME = "set_volume";
        constexpr const char* SET_BRIGHTNESS = "set_brightness";
        constexpr const char* SET_DEVICE_NAME = "set_device_name";
        constexpr const char* SET_WIFI = "set_wifi";
        
        // Device control features
        constexpr const char* REBOOT = "reboot";
        constexpr const char* REQUEST_STATUS = "request_status";
        constexpr const char* REQUEST_OTA = "request_ota";
        constexpr const char* REQUEST_BLE_CONFIG = "request_ble_config";
        
        // Audio features (PTalk specific)
        constexpr const char* PLAY_TTS = "play_tts";
        constexpr const char* STOP_AUDIO = "stop_audio";
        constexpr const char* SET_EMOTION = "set_emotion";
        
        // Status/query features
        constexpr const char* GET_STATUS = "get_status";
        constexpr const char* GET_WIFI_LIST = "get_wifi_list";
    }

    // =========================================================================
    // BUILT-IN EVENT NAMES
    // =========================================================================
    
    namespace events
    {
        constexpr const char* FEATURE_RESPONSE = "feature_response";
        constexpr const char* STATUS = "status";
        constexpr const char* BATTERY = "battery";
        constexpr const char* CONNECTIVITY = "connectivity";
        constexpr const char* AUDIO_STATE = "audio_state";
        constexpr const char* EMOTION = "emotion";
        constexpr const char* ERROR = "error";
        constexpr const char* OTA_PROGRESS = "ota_progress";
        constexpr const char* OTA_COMPLETE = "ota_complete";
    }

    // =========================================================================
    // MEO FEATURE MANAGER
    // =========================================================================

    /**
     * @brief Manages feature registration and invocation
     * 
     * Usage:
     *   MeoFeatureManager mgr;
     *   mgr.registerFeature("set_volume", [](const FeatureCall& call) {
     *       int volume = std::stoi(call.params.at("volume"));
     *       // Apply volume...
     *       return FeatureResponse{call.feature_name, call.device_id, true, "Volume set"};
     *   });
     *   
     *   // When MQTT message arrives:
     *   auto response = mgr.invokeFeature(call);
     */
    class MeoFeatureManager
    {
    public:
        MeoFeatureManager() = default;
        ~MeoFeatureManager() = default;

        /**
         * @brief Register a handler for a feature
         * @param feature_name Name of the feature
         * @param handler Callback to handle the feature invoke
         */
        void registerFeature(const std::string& feature_name, FeatureHandler handler);

        /**
         * @brief Unregister a feature handler
         * @param feature_name Name of the feature to remove
         */
        void unregisterFeature(const std::string& feature_name);

        /**
         * @brief Check if a feature is registered
         * @param feature_name Name of the feature
         * @return true if registered
         */
        bool hasFeature(const std::string& feature_name) const;

        /**
         * @brief Get list of registered feature names
         * @return Vector of feature names
         */
        std::vector<std::string> getRegisteredFeatures() const;

        /**
         * @brief Invoke a feature by name
         * @param call The feature call details
         * @return Response from the feature handler
         */
        FeatureResponse invokeFeature(const FeatureCall& call);

        /**
         * @brief Parse feature invoke from JSON payload
         * 
         * Supports both cloud-compatible and legacy formats:
         * - Cloud: {"feature": "name", "params": {...}}
         * - Legacy: {"params": {...}} (feature name from topic)
         * 
         * @param json_payload The JSON string
         * @param feature_from_topic Feature name extracted from topic (legacy mode)
         * @return Parsed FeatureCall
         */
        static FeatureCall parseInvokePayload(const std::string& json_payload, 
                                               const std::string& feature_from_topic = "");

        /**
         * @brief Serialize FeatureResponse to JSON
         * @param response The response to serialize
         * @return JSON string
         */
        static std::string serializeResponse(const FeatureResponse& response);

        /**
         * @brief Serialize DeviceEvent to JSON
         * @param event The event to serialize
         * @return JSON string
         */
        static std::string serializeEvent(const DeviceEvent& event);

    private:
        std::map<std::string, FeatureHandler> handlers_;
    };

} // namespace meo
