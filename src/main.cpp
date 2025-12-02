#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "../lib/wifi/WifiManager.h"

static const char* TAG = "APP_MAIN";

extern "C" void app_main()
{
    ESP_LOGI(TAG, "===== PTalk – WiFiManager Test =====");

    // Đăng ký callback báo trạng thái WiFi
    WifiManager::instance().onStatus([](int status) {
        switch (status) {
        case 0:
            ESP_LOGW("WiFiCB", "DISCONNECTED");
            break;

        case 1:
            ESP_LOGI("WiFiCB", "CONNECTING...");
            break;

        case 2:
            ESP_LOGI("WiFiCB", "GOT IP!");
            break;
        }
    });

    // Khởi tạo WiFi Manager
    WifiManager::instance().init();
    //start portal
    
    while(!WifiManager::instance().isConnected()){
        WifiManager::instance().startCaptivePortal();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Vòng lặp test cơ bản
    while (true)
    {
        if (WifiManager::instance().isConnected()) {
            ESP_LOGI(TAG, "Connected ✔  IP: %s",
                     WifiManager::instance().getIp().c_str());
        } else {
            ESP_LOGW(TAG, "Not connected…");
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
