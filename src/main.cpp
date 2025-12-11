// #include <stdio.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "esp_log.h"
// #include "../lib/wifi/WifiManager.h"

// static const char* TAG = "APP_MAIN";

// extern "C" void app_main()
// {
//     ESP_LOGI(TAG, "===== PTalk – WiFiManager Test =====");

//     // Đăng ký callback báo trạng thái WiFi
//     WifiManager::instance().onStatus([](int status) {
//         switch (status) {
//         case 0:
//             ESP_LOGW("WiFiCB", "DISCONNECTED");
//             break;

//         case 1:
//             ESP_LOGI("WiFiCB", "CONNECTING...");
//             break;

//         case 2:
//             ESP_LOGI("WiFiCB", "GOT IP!");
//             break;
//         }
//     });

//     // Khởi tạo WiFi Manager
//     WifiManager::instance().init();
//     //start portal
    
//     while(!WifiManager::instance().isConnected()){
//         WifiManager::instance().startCaptivePortal();
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
    
//     // Vòng lặp test cơ bản
//     while (true)
//     {
//         if (WifiManager::instance().isConnected()) {
//             ESP_LOGI(TAG, "Connected ✔  IP: %s",
//                      WifiManager::instance().getIp().c_str());
//         } else {
//             ESP_LOGW(TAG, "Not connected…");
//         }

//         vTaskDelay(pdMS_TO_TICKS(3000));
//     }
// }


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
// #include "esp_websocket_client.h"


#include "system/StateManager.hpp"

#include "../lib/power/Power.hpp"



static const char *TAG = "MAIN_TEST";

// Định nghĩa màu RGB565 cơ bản
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Khoi dong he thong test Display...");


    
    Power power(ADC1_CHANNEL_5, 10000, 20000);


    // while (1) {
    //     printf("Battery = %d%%\n", power.getBatteryPercent());
    //     // Test fill màu đỏ
    //     display.fill(0xF800);
    //     vTaskDelay(pdMS_TO_TICKS(1000));

    //     // Test fill màu xanh lá
    //     display.fill(0x07E0);
    //     vTaskDelay(pdMS_TO_TICKS(1000));

    //     // Test fill màu xanh dương
    //     display.fill(0x001F);
    //     vTaskDelay(pdMS_TO_TICKS(1000));

    //     // Test text
    //     display.clear();
    //     display.drawText("Hello PTIT", 20, 100, 2, 0xFFFF);
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }


    const TickType_t delayMs = pdMS_TO_TICKS(5000);  // 5s


    while (true)
    {   

        vTaskDelay(delayMs);
    }

}