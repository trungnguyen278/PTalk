#include "DeviceProfile.hpp"

#include "AppController.hpp"

// ===== Managers =====
#include "system/DisplayManager.hpp"
#include "system/AudioManager.hpp"
#include "system/NetworkManager.hpp"
#include "system/PowerManager.hpp"
#include "system/OTAUpdater.hpp"

// ===== Drivers / IO =====
#include "DisplayDriver.hpp"
#include "I2SAudioInput_INMP441.hpp"
#include "I2SAudioOutput_MAX98357.hpp"
#include "TouchInput.hpp"
#include "Power.hpp"

// ===== Codec =====
#include "AdpcmCodec.hpp"

// ===== Assets =====
// Uncomment sau khi convert assets bằng convert_assets.py
// Ví dụ: python convert_assets.py icon wifi_ok.png src/assets/icons/
//        python convert_assets.py emotion happy.gif src/assets/emotions/ 20 true
//
// #include "assets/icons/wifi_ok.hpp"
// #include "assets/icons/wifi_fail.hpp"
// #include "assets/icons/battery.hpp"
// #include "assets/icons/battery_low.hpp"
// #include "assets/icons/battery_charge.hpp"
// #include "assets/icons/battery_full.hpp"
//
// #include "assets/emotions/idle.hpp"
// #include "assets/emotions/listening.hpp"
// #include "assets/emotions/thinking.hpp"
// #include "assets/emotions/speaking.hpp"
// #include "assets/emotions/error.hpp"
// #include "assets/emotions/boot.hpp"
// #include "assets/emotions/lowbat.hpp"

#include "esp_log.h"

static const char *TAG = "DeviceProfile";

// Centralized hardware/config values for easy tweaking
namespace device_cfg
{
    struct PowerPins
    {
        adc1_channel_t adc_channel = ADC1_CHANNEL_5; // Battery sense ADC channel (default GPIO33)
        gpio_num_t pin_chg = GPIO_NUM_NC;            // Optional charge detect pin (active level depends on HW)
        gpio_num_t pin_full = GPIO_NUM_NC;           // Optional full-battery pin (active level depends on HW)
        float r1_ohm = 10000.0f;                     // Resistor divider R1 (top, to battery)
        float r2_ohm = 20000.0f;                     // Resistor divider R2 (bottom, to GND)
    };

    constexpr PowerPins power{};

    struct DisplayPins
    {
        spi_host_device_t spi_host = SPI2_HOST;   // SPI host (SPI2_HOST or SPI3_HOST)
        gpio_num_t pin_mosi = GPIO_NUM_21;        // MOSI (SDA in some boards)
        gpio_num_t pin_sclk = GPIO_NUM_23;        // SCLK (Clock)
        gpio_num_t pin_cs = GPIO_NUM_5;           // Chip Select
        gpio_num_t pin_dc = GPIO_NUM_18;          // Data/Command
        gpio_num_t pin_rst = GPIO_NUM_19;         // Reset
        gpio_num_t pin_bl = GPIO_NUM_27;          // Backlight
        uint32_t spi_speed_hz = 40 * 1000 * 1000; // SPI clock speed (40 MHz)
    };

    constexpr DisplayPins display{};
}

bool DeviceProfile::setup(AppController &app)
{
    ESP_LOGI(TAG, "DeviceProfile setup begin");

    // =========================================================
    // 1️⃣ DISPLAY
    // =========================================================
    auto display = std::make_unique<DisplayManager>();

    // --- Display driver config (ST7789 240x240) ---
    DisplayDriver::Config lcd_cfg{
        .spi_host = device_cfg::display.spi_host,
        .pin_cs = device_cfg::display.pin_cs,
        .pin_dc = device_cfg::display.pin_dc,
        .pin_rst = device_cfg::display.pin_rst,
        .pin_bl = device_cfg::display.pin_bl,
        .spi_speed_hz = device_cfg::display.spi_speed_hz};

    auto lcd_driver = std::make_unique<DisplayDriver>();

    if (!lcd_driver->init(lcd_cfg))
    {
        ESP_LOGE(TAG, "DisplayDriver init failed");
        return false;
    }

    if (!display->init(std::move(lcd_driver), 240, 240))
    {
        ESP_LOGE(TAG, "DisplayManager init failed");
        return false;
    }

    // Auto-bind UI to state changes so animations/icons update reactively
    display->enableStateBinding(true);

    // --- Register UI assets ---
    // Uncomment sau khi có assets đã convert

    // Emotions (animations)
    // display->registerEmotion("idle",        asset::emotion::IDLE);
    // display->registerEmotion("listening",   asset::emotion::LISTENING);
    // display->registerEmotion("thinking",    asset::emotion::THINKING);
    // display->registerEmotion("speaking",    asset::emotion::SPEAKING);
    // display->registerEmotion("error",       asset::emotion::ERROR);
    // display->registerEmotion("boot",        asset::emotion::BOOT);
    // display->registerEmotion("lowbat",      asset::emotion::LOWBAT);
    // display->registerEmotion("maintenance", asset::emotion::IDLE);      // Tạm dùng IDLE
    // display->registerEmotion("updating",    asset::emotion::THINKING);  // Tạm dùng THINKING
    // display->registerEmotion("reset",       asset::emotion::ERROR);     // Tạm dùng ERROR

    // Icons (static images)
    // display->registerIcon("wifi_ok",         asset::icon::WIFI_OK);
    // display->registerIcon("wifi_fail",       asset::icon::WIFI_FAIL);
    // display->registerIcon("battery",         asset::icon::BATTERY);
    // display->registerIcon("battery_low",     asset::icon::BATTERY_LOW);
    // display->registerIcon("battery_charge",  asset::icon::BATTERY_CHARGE);
    // display->registerIcon("battery_full",    asset::icon::BATTERY_FULL);

    // =========================================================
    // 2️⃣ AUDIO
    // =========================================================
    auto audio = std::make_unique<AudioManager>();

    // --- Mic: INMP441 ---
    I2SAudioInput_INMP441::Config mic_cfg{
        .i2s_port = I2S_NUM_0,
        .pin_bck = GPIO_NUM_26,
        .pin_ws = GPIO_NUM_25,
        .pin_din = GPIO_NUM_33,
        .sample_rate = 16000};

    auto mic = std::make_unique<I2SAudioInput_INMP441>(mic_cfg);

    // --- Speaker: MAX98357 ---
    I2SAudioOutput_MAX98357::Config spk_cfg{
        .i2s_port = I2S_NUM_1,
        .pin_bck = GPIO_NUM_14,
        .pin_ws = GPIO_NUM_27,
        .pin_dout = GPIO_NUM_32,
        .sample_rate = 16000};

    auto speaker = std::make_unique<I2SAudioOutput_MAX98357>(spk_cfg);

    // --- Codec ---
    auto codec = std::make_unique<AdpcmCodec>();

    // Wire dependencies into AudioManager before init/start
    audio->setInput(std::move(mic));
    audio->setOutput(std::move(speaker));
    audio->setCodec(std::move(codec));

    if (!audio->init())
    {
        ESP_LOGE(TAG, "AudioManager init failed");
        return false;
    }

    audio->start();

    // =========================================================
    // 3️⃣ NETWORK
    // =========================================================
    auto network = std::make_unique<NetworkManager>();

    // Configure captive portal and WebSocket server endpoint here
    NetworkManager::Config net_cfg{};
    net_cfg.ap_ssid = "PTalk-Portal"; // SSID hiển thị khi mở portal
    net_cfg.ap_max_clients = 4;       // Số thiết bị tối đa kết nối vào portal

    // Đặt địa chỉ IP và port của WebSocket server (ví dụ: 192.168.1.100:8080)
    // Có thể thêm path nếu server yêu cầu, ví dụ: ws://192.168.1.100:8080/ws
    net_cfg.ws_url = "ws://192.168.1.100:8080";

    if (!network->init(net_cfg))
    {
        ESP_LOGE(TAG, "NetworkManager init failed");
        return false;
    }

    // --- Network → Audio callback ---
    // TODO: Uncomment khi AudioManager sẵn sàng nhận data từ server
    // network->onServerBinary(
    //     [&audio](const uint8_t* data, size_t len) {
    //         audio->onAudioPacketFromServer(data, len);
    //     }
    // );

    // =========================================================
    // 4️⃣ TOUCH INPUT
    // =========================================================
    auto touch = std::make_unique<TouchInput>();

    TouchInput::Config touch_cfg{
        .pin = GPIO_NUM_0,
        .active_low = true,
        .long_press_ms = 1200};

    if (!touch->init(touch_cfg))
    {
        ESP_LOGE(TAG, "TouchInput init failed");
        return false;
    }

    touch->onEvent([&app](TouchInput::Event e)
                   {
        if (e == TouchInput::Event::PRESS) {
            app.postEvent(event::AppEvent::USER_BUTTON);
        }
        if (e == TouchInput::Event::LONG_PRESS) {
            app.postEvent(event::AppEvent::SLEEP_REQUEST);
        } });

    // =========================================================
    // 5️⃣ POWER
    // =========================================================
    // Centralize power/deep-sleep thresholds here for easy tuning
    PowerManager::Config power_cfg{};
    power_cfg.evaluate_interval_ms = 2000; // sample every 2s
    power_cfg.low_battery_percent = 20.0f;
    power_cfg.critical_percent = 8.0f;
    power_cfg.enable_smoothing = true;
    power_cfg.smoothing_alpha = 0.15f;

    // Battery sensing hardware (divider + optional charge/full pins)
    auto power_driver = std::make_unique<Power>(
        device_cfg::power.adc_channel,
        device_cfg::power.pin_chg,  // set to GPIO pin if hardware provides charge indicator
        device_cfg::power.pin_full, // set to GPIO pin if hardware provides full indicator
        device_cfg::power.r1_ohm,
        device_cfg::power.r2_ohm);

    auto power = std::make_unique<PowerManager>(std::move(power_driver), power_cfg);

    // App-level power behavior (deep sleep re-check interval)
    AppController::Config app_cfg{};
    app_cfg.deep_sleep_wakeup_sec = 30; // wake every 30s to re-check battery

    // =========================================================
    // 6️⃣ CREATE OTA UPDATER
    // =========================================================
    auto ota = std::make_unique<OTAUpdater>();

    // =========================================================
    // 7️⃣ ATTACH MODULES → APP CONTROLLER
    // =========================================================
    app.attachModules(
        std::move(display),
        std::move(audio),
        std::move(network),
        std::move(power),
        std::move(touch),
        std::move(ota));

    // Apply app-level configuration (deep sleep interval, etc.)
    app.setConfig(app_cfg);

    ESP_LOGI(TAG, "DeviceProfile setup OK");
    return true;
}
