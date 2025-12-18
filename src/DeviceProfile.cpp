#include "DeviceProfile.hpp"

#include "AppController.hpp"

// ===== Managers =====
#include "system/DisplayManager.hpp"
#include "system/AudioManager.hpp"
#include "system/NetworkManager.hpp"
#include "system/PowerManager.hpp"
#include "system/OTAUpdater.hpp"
// State control for audio speak/listen transitions
#include "system/StateManager.hpp"
#include "system/StateTypes.hpp"
// Ring buffer API to feed downlink audio into AudioManager
#include "freertos/ringbuf.h"

// ===== Drivers / IO =====
#include "DisplayDriver.hpp"
#include "I2SAudioInput_INMP441.hpp"
#include "I2SAudioOutput_MAX98357.hpp"
#include "TouchInput.hpp"
#include "Power.hpp"

// ===== Codec =====
#include "AdpcmCodec.hpp"

// ===== Assets =====
// Uncomment sau khi convert assets bằng scripts/convert_assets.py
// Ví dụ: python scripts/convert_assets.py icon wifi_ok.png src/assets/icons/
//        python scripts/convert_assets.py emotion happy.gif src/assets/emotions/ 20 true
//
// #include "assets/icons/wifi_ok.hpp"
// #include "assets/icons/wifi_fail.hpp"
// #include "assets/icons/battery.hpp"
// #include "assets/icons/battery_low.hpp"
// #include "assets/icons/battery_charge.hpp"
// #include "assets/icons/battery_full.hpp"
#include "assets/icons/critical_power.hpp"
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
        uint32_t spi_speed_hz = 40 * 1000 * 1000; // SPI clock speed (40 MHz with NO_DUMMY)
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
        .pin_mosi = device_cfg::display.pin_mosi,
        .pin_sclk = device_cfg::display.pin_sclk,

        // Try Y offset 80 for panels with 240x240 active area in 240x320 memory
        .x_offset = 0,
        .y_offset = 80, // Map 240x240 window into 240x320 GRAM (common ST7789)
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
    display->registerIcon(
        "battery_critical",
        DisplayManager::Icon{
            asset::icon::CRITICAL_POWER.w,
            asset::icon::CRITICAL_POWER.h,
            asset::icon::CRITICAL_POWER.rgb});

    // =========================================================
    // 2️⃣ AUDIO
    // =========================================================
    auto audio = std::make_unique<AudioManager>();

    // --- Mic: INMP441 ---
    I2SAudioInput_INMP441::Config mic_cfg{
        .i2s_port = I2S_NUM_0,
        .pin_bck = GPIO_NUM_14, // I2S_MIC_SERIAL_CLOCK
        .pin_ws = GPIO_NUM_15,  // I2S_MIC_WORD_SELECT
        .pin_din = GPIO_NUM_32, // I2S_MIC_SERIAL_DATA
        .sample_rate = 16000};

    auto mic = std::make_unique<I2SAudioInput_INMP441>(mic_cfg);

    // --- Speaker: MAX98357 ---
    I2SAudioOutput_MAX98357::Config spk_cfg{
        .i2s_port = I2S_NUM_1,
        .pin_bck = GPIO_NUM_26,  // I2S_SPEAKER_SERIAL_CLOCK
        .pin_ws = GPIO_NUM_25,   // I2S_SPEAKER_WORD_SELECT
        .pin_dout = GPIO_NUM_22, // I2S_SPEAKER_SERIAL_DATA
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
    // Thêm path nếu server yêu cầu, ví dụ: ws://192.168.1.100:8080/ws
    // Uvicorn/FastAPI thường khai báo endpoint WebSocket tại "/ws".
    net_cfg.ws_url = "ws://192.168.0.103:8080/ws";

    if (!network->init(net_cfg))
    {
        ESP_LOGE(TAG, "NetworkManager init failed");
        return false;
    }

    // --- Network → Audio wiring ---
    // Push incoming binary (ADPCM) from WS into speaker ringbuffer
    // and drive InteractionState to SPEAKING while audio is arriving.
    StreamBufferHandle_t spk_sb = audio->getSpeakerEncodedBuffer();
    AudioManager *audio_ptr = audio.get();       // Capture pointer for disconnect handler
    NetworkManager *network_ptr = network.get(); // For session flag access

    network->onServerBinary([spk_sb, network_ptr](const uint8_t *data, size_t len)
                            {
        if (!data || len == 0) return;
        // Feed encoded data to AudioManager's downlink buffer
        size_t written = xStreamBufferSend(spk_sb, data, len, pdMS_TO_TICKS(100));
        if (written != len) {
            static uint32_t drop_count = 0;
            if (++drop_count % 10 == 0) {
                ESP_LOGW("Network", "ADPCM buffer full! Dropped %zu bytes (wanted %zu)", len - written, len);
            }
        }

        // Set SPEAKING only ONCE per TTS session (prevent state spam)
        if (!network_ptr->isSpeakingSessionActive()) {
            network_ptr->startSpeakingSession();
            auto& sm = StateManager::instance();
            sm.setInteractionState(state::InteractionState::SPEAKING,
                                   state::InputSource::SERVER_COMMAND);
        } });

    // Handle WS disconnect - must cleanup to unblock speaker task
    network->onDisconnect([spk_sb, audio_ptr]()
                          {
        auto& sm = StateManager::instance();
        auto current_state = sm.getInteractionState();
        
        ESP_LOGW("DeviceProfile", "WS disconnected - cleanup audio state");
        
        // Flush buffer to wake speaker task from blocking read
        xStreamBufferReset(spk_sb);
        
        // Stop speaking to set speaking=false and unblock task
        if (current_state == state::InteractionState::SPEAKING) {
            sm.setInteractionState(state::InteractionState::IDLE,
                                   state::InputSource::SYSTEM);
        } });

    // Optionally react to simple text control messages from server
    network->onServerText([network_ptr](const std::string &msg)
                          {
        auto& sm = StateManager::instance();
        if (msg == "PROCESSING_START" || msg == "PROCESSING") {
            sm.setInteractionState(state::InteractionState::PROCESSING,
                                   state::InputSource::SERVER_COMMAND);
        } else if (msg == "LISTENING") {
            sm.setInteractionState(state::InteractionState::LISTENING,
                                   state::InputSource::SERVER_COMMAND);
        } else if (msg == "SPEAKING" || msg == "SPEAK_START") {
            sm.setInteractionState(state::InteractionState::SPEAKING,
                                   state::InputSource::SERVER_COMMAND);
        } else if (msg == "IDLE" || msg == "SPEAK_END" || msg == "DONE" || msg == "TTS_END") {
            // Reset session flag to allow next TTS session
            network_ptr->endSpeakingSession();
            sm.setInteractionState(state::InteractionState::IDLE,
                                   state::InputSource::SERVER_COMMAND);
        } });

    // =========================================================
    // STATE OBSERVER: Control WS immune mode during SPEAKING
    // =========================================================
    // During audio streaming, prevent WS from closing on WiFi fluctuations
    // (use network_ptr already declared above)
    auto &sm = StateManager::instance();
    sm.subscribeInteraction([network_ptr](state::InteractionState new_state, state::InputSource src)
                            {
        if (new_state == state::InteractionState::SPEAKING) {
            // Enable immune mode - WS must survive WiFi roaming/power save transitions
            network_ptr->setWSImmuneMode(true);
        } else {
            // Disable immune mode when not speaking
            network_ptr->setWSImmuneMode(false);
        } });

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
    power_cfg.low_battery_percent = 15.0f; // low battery warning at 15%
    power_cfg.critical_percent = 5.0f;     // critical battery (auto sleep) at 5%
    power_cfg.enable_smoothing = true;     // enable smoothing filter
    power_cfg.smoothing_alpha = 0.15f;     // smoothing factor alpha

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
    app_cfg.deep_sleep_wakeup_sec = 60; // wake every 60s to re-check battery

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
