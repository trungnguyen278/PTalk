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

#include "../lib/display/Display.h"
#include "../lib/display/DisplayTypes.h"
#include "../lib/wifi/WebSocketClient.h"

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
    DisplayConfig cfg;
    cfg.width        = 240; // Giả sử 240x240 hoặc 240x320
    cfg.height       = 240; // Nếu màn chữ nhật thì sửa thành 320
    
    // Config Pin (Từ code cũ: TFT_MOSI 21, TFT_SCLK 23...)
    cfg.host         = SPI2_HOST; // Sử dụng HSPI
    cfg.pin_mosi     = 21;
    cfg.pin_sclk     = 23;
    cfg.pin_cs       = 5;
    cfg.pin_dc       = 18;
    cfg.pin_rst      = 19;
    
    // Code cũ: pinMode(27, OUTPUT); -> Backlight là 27
    cfg.pin_bl       = 27; 

    // Config PWM cho Backlight
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.spi_speed_hz = 40 * 1000 * 1000; // 40MHz
    cfg.default_bg   = BLACK;
    cfg.rotation = 2; // Xoay 180 độ 

    // 2. KHỞI TẠO
    Display tft(cfg);
    tft.init();

    // 3. VẼ TEST
    ESP_LOGI(TAG, "Dang ve len man hinh...");
    
    // Clear màn hình màu đen
    tft.fill(BLACK);

    // Vẽ chữ PTIT to (Size 4) giống vị trí code cũ (40, 90)
    // Code cũ: tft.setTextSize(13) -> Font bitmap của mình nhỏ hơn nên dùng size 4-5
    tft.drawText("PTIT", 60, 90, 5, WHITE);


    while (1) {


        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}