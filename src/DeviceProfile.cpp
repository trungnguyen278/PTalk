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

bool DeviceProfile::setup(AppController &app)
{
    ESP_LOGI(TAG, "DeviceProfile setup begin");

    // =========================================================
    // 1️⃣ DISPLAY
    // =========================================================
    auto display = std::make_unique<DisplayManager>();

    // --- Display driver config (ST7789 240x240) ---
    DisplayDriver::Config lcd_cfg{
        .spi_host = SPI2_HOST,
        .pin_cs = GPIO_NUM_5,
        .pin_dc = GPIO_NUM_16,
        .pin_rst = GPIO_NUM_17,
        .pin_bl = GPIO_NUM_4,
        .spi_speed_hz = 40 * 1000 * 1000};

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

    if (!network->init())
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
    PowerManager::Config power_cfg{};
    // ADC1_CHANNEL_5 (GPIO33) as default battery sense; adjust to your wiring
    auto power_driver = std::make_unique<Power>(ADC1_CHANNEL_5, GPIO_NUM_NC, GPIO_NUM_NC);
    auto power = std::make_unique<PowerManager>(std::move(power_driver), power_cfg);

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

    ESP_LOGI(TAG, "DeviceProfile setup OK");
    return true;
}
