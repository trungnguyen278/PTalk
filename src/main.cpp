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

#include "../lib/display/Display.hpp"
#include "../lib/display/DisplayTypes.hpp"
#include "system/StateManager.hpp"
#include "../lib/display/DisplayAnimator.hpp"

#include "vuive.hpp"

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

    // 1. CẤU HÌNH (Lấy từ main.txt cũ của bạn)
    DisplayConfig cfg = {
        240,          // width
        240,          // height
        21,           // pin_mosi
        23,           // pin_sclk
        5,            // pin_cs
        18,           // pin_dc
        19,           // pin_rst
        27,           // pin_bl
        SPI2_HOST,    // host
        40000000,     // spi_speed_hz
        LEDC_CHANNEL_0, // ledc_channel
        LEDC_TIMER_0,   // ledc_timer
        0x0000,       // default_bg
        2             // rotation
    };


    Display display(cfg);
    display.init();
    static DisplayAnimator animator(&display);
    animator.setAnimation(&vuive);

    // Tạo task animator
    xTaskCreatePinnedToCore(
        DisplayAnimator::taskEntry,
        "anim_task",
        4096,
        &animator,
        4,
        NULL,
        1   // chạy trên core 1
    );

    // Play animation loop với fps=15
    animator.play(true, 60);
    
    // while (1) {
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
    while(1){
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}