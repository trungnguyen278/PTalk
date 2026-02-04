#include "MeoFeature.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include <algorithm>

static const char* TAG = "MeoFeature";

namespace meo
{

// =========================================================================
// MeoFeatureManager Implementation
// =========================================================================

void MeoFeatureManager::registerFeature(const std::string& feature_name, FeatureHandler handler)
{
    if (feature_name.empty() || !handler)
    {
        ESP_LOGW(TAG, "Invalid feature registration: name=%s", feature_name.c_str());
        return;
    }
    
    handlers_[feature_name] = handler;
    ESP_LOGI(TAG, "Registered feature: %s", feature_name.c_str());
}

void MeoFeatureManager::unregisterFeature(const std::string& feature_name)
{
    auto it = handlers_.find(feature_name);
    if (it != handlers_.end())
    {
        handlers_.erase(it);
        ESP_LOGI(TAG, "Unregistered feature: %s", feature_name.c_str());
    }
}

bool MeoFeatureManager::hasFeature(const std::string& feature_name) const
{
    return handlers_.find(feature_name) != handlers_.end();
}

std::vector<std::string> MeoFeatureManager::getRegisteredFeatures() const
{
    std::vector<std::string> names;
    names.reserve(handlers_.size());
    for (const auto& pair : handlers_)
    {
        names.push_back(pair.first);
    }
    return names;
}

FeatureResponse MeoFeatureManager::invokeFeature(const FeatureCall& call)
{
    ESP_LOGI(TAG, "Invoking feature: %s (device=%s)", 
             call.feature_name.c_str(), call.device_id.c_str());

    auto it = handlers_.find(call.feature_name);
    if (it == handlers_.end())
    {
        ESP_LOGW(TAG, "Feature not found: %s", call.feature_name.c_str());
        return FeatureResponse{
            call.feature_name,
            call.device_id,
            false,
            "Feature not registered: " + call.feature_name,
            call.invoke_id,
            {}
        };
    }

    try
    {
        return it->second(call);
    }
    catch (const std::exception& e)
    {
        ESP_LOGE(TAG, "Feature %s threw exception: %s", call.feature_name.c_str(), e.what());
        return FeatureResponse{
            call.feature_name,
            call.device_id,
            false,
            std::string("Exception: ") + e.what(),
            call.invoke_id,
            {}
        };
    }
}

FeatureCall MeoFeatureManager::parseInvokePayload(const std::string& json_payload,
                                                   const std::string& feature_from_topic)
{
    FeatureCall call;
    call.feature_name = feature_from_topic; // Default from topic (legacy mode)

    cJSON* root = cJSON_Parse(json_payload.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse invoke payload: %s", json_payload.c_str());
        return call;
    }

    // Try to get feature name from JSON (cloud-compatible mode)
    // Accepts both "feature" and "feature_name" keys
    cJSON* feature_obj = cJSON_GetObjectItem(root, "feature");
    if (!feature_obj)
    {
        feature_obj = cJSON_GetObjectItem(root, "feature_name");
    }
    if (feature_obj && feature_obj->valuestring)
    {
        call.feature_name = feature_obj->valuestring;
    }

    // Get invoke_id if present (for correlation)
    cJSON* invoke_id_obj = cJSON_GetObjectItem(root, "invoke_id");
    if (invoke_id_obj && invoke_id_obj->valuestring)
    {
        call.invoke_id = invoke_id_obj->valuestring;
    }

    // Get device_id if present
    cJSON* device_id_obj = cJSON_GetObjectItem(root, "device_id");
    if (device_id_obj && device_id_obj->valuestring)
    {
        call.device_id = device_id_obj->valuestring;
    }

    // Parse params object
    cJSON* params_obj = cJSON_GetObjectItem(root, "params");
    if (params_obj && cJSON_IsObject(params_obj))
    {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, params_obj)
        {
            if (item->string)
            {
                std::string value;
                if (cJSON_IsString(item))
                {
                    value = item->valuestring;
                }
                else if (cJSON_IsNumber(item))
                {
                    // Convert number to string
                    char buf[32];
                    if (item->valuedouble == (double)(int)item->valuedouble)
                    {
                        snprintf(buf, sizeof(buf), "%d", item->valueint);
                    }
                    else
                    {
                        snprintf(buf, sizeof(buf), "%.6g", item->valuedouble);
                    }
                    value = buf;
                }
                else if (cJSON_IsBool(item))
                {
                    value = cJSON_IsTrue(item) ? "true" : "false";
                }
                else
                {
                    // For complex types, serialize to JSON string
                    char* json_str = cJSON_PrintUnformatted(item);
                    if (json_str)
                    {
                        value = json_str;
                        cJSON_free(json_str);
                    }
                }
                call.params[item->string] = value;
            }
        }
    }
    else
    {
        // Fallback: treat all top-level keys (except feature/feature_name/invoke_id) as params
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, root)
        {
            if (item->string)
            {
                std::string key = item->string;
                if (key == "feature" || key == "feature_name" || key == "invoke_id" || key == "device_id")
                    continue;

                std::string value;
                if (cJSON_IsString(item))
                {
                    value = item->valuestring;
                }
                else if (cJSON_IsNumber(item))
                {
                    char buf[32];
                    if (item->valuedouble == (double)(int)item->valuedouble)
                    {
                        snprintf(buf, sizeof(buf), "%d", item->valueint);
                    }
                    else
                    {
                        snprintf(buf, sizeof(buf), "%.6g", item->valuedouble);
                    }
                    value = buf;
                }
                else if (cJSON_IsBool(item))
                {
                    value = cJSON_IsTrue(item) ? "true" : "false";
                }
                call.params[key] = value;
            }
        }
    }

    cJSON_Delete(root);

    ESP_LOGD(TAG, "Parsed invoke: feature=%s, params_count=%d", 
             call.feature_name.c_str(), (int)call.params.size());

    return call;
}

std::string MeoFeatureManager::serializeResponse(const FeatureResponse& response)
{
    cJSON* root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "feature_name", response.feature_name.c_str());
    cJSON_AddStringToObject(root, "device_id", response.device_id.c_str());
    cJSON_AddBoolToObject(root, "success", response.success);
    
    if (!response.message.empty())
    {
        cJSON_AddStringToObject(root, "message", response.message.c_str());
    }
    
    if (!response.invoke_id.empty())
    {
        cJSON_AddStringToObject(root, "invoke_id", response.invoke_id.c_str());
    }

    // Add response data if present
    if (!response.data.empty())
    {
        cJSON* data_obj = cJSON_CreateObject();
        for (const auto& pair : response.data)
        {
            cJSON_AddStringToObject(data_obj, pair.first.c_str(), pair.second.c_str());
        }
        cJSON_AddItemToObject(root, "data", data_obj);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);

    return result;
}

std::string MeoFeatureManager::serializeEvent(const DeviceEvent& event)
{
    cJSON* root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "event", event.event_name.c_str());
    cJSON_AddStringToObject(root, "device_id", event.device_id.c_str());

    // Add event data
    if (!event.data.empty())
    {
        for (const auto& pair : event.data)
        {
            // Try to parse as number
            char* end;
            double num = strtod(pair.second.c_str(), &end);
            if (*end == '\0' && pair.second.length() > 0)
            {
                // It's a valid number
                if (num == (double)(int)num)
                {
                    cJSON_AddNumberToObject(root, pair.first.c_str(), (int)num);
                }
                else
                {
                    cJSON_AddNumberToObject(root, pair.first.c_str(), num);
                }
            }
            else if (pair.second == "true" || pair.second == "false")
            {
                cJSON_AddBoolToObject(root, pair.first.c_str(), pair.second == "true");
            }
            else
            {
                cJSON_AddStringToObject(root, pair.first.c_str(), pair.second.c_str());
            }
        }
    }

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);

    return result;
}

} // namespace meo
