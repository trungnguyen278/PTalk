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
#include "system/BluetoothService.hpp"
// Ring buffer API to feed downlink audio into AudioManager
// #include "freertos/ringbuf.h"

// ===== Drivers / IO =====
#include "DisplayDriver.hpp"
#include "I2SAudioInput_INMP441.hpp"
#include "I2SAudioOutput_MAX98357.hpp"
#include "TouchInput.hpp"
#include "Power.hpp"

// ===== Codec =====
#include "AdpcmCodec.hpp"

#include "nvs_flash.h"
#include "nvs.h"

// ===== Assets =====
// Uncomment sau khi convert assets bằng scripts/convert_assets.py
// Ví dụ: python scripts/convert_assets.py icon wifi_ok.png src/assets/icons/
//        python scripts/convert_assets.py emotion happy.gif src/assets/emotions/ 20 true
//
// #include "assets/icons/wifi_ok.hpp"
// #include "assets/icons/wifi_fail.hpp"
// #include "assets/icons/battery.hpp"
// #include "assets/icons/battery_low.hpp"
#include "../assets/icons/battery_charge.hpp"
#include "../assets/icons/battery_full.hpp"
#include "../assets/icons/critical_power.hpp"
//
#include "../assets/emotions/neutral.hpp"
#include "../assets/emotions/idle.hpp"
#include "../assets/emotions/listening.hpp"
#include "../assets/emotions/happy.hpp"
#include "../assets/emotions/sad.hpp"
#include "../assets/emotions/thinking.hpp"
#include "../assets/emotions/stun.hpp"
// #include "assets/emotions/speaking.hpp"
// #include "assets/emotions/error.hpp"
// #include "assets/emotions/boot.hpp"
// #include "assets/emotions/lowbat.hpp"

#include "esp_log.h"
#include <esp_attr.h>

static const char *TAG = "DeviceProfile";

// Helper function to register emotions (extracted to reduce code size in setup())
static void registerEmotions(DisplayManager *display)
{
    // Register neutral emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::NEUTRAL.width;
        anim1bit.height = asset::emotion::NEUTRAL.height;
        anim1bit.frame_count = asset::emotion::NEUTRAL.frame_count;
        anim1bit.fps = asset::emotion::NEUTRAL.fps;
        anim1bit.loop = asset::emotion::NEUTRAL.loop;
        anim1bit.max_packed_size = asset::emotion::NEUTRAL.max_packed_size;
        // Frame 0 is diff from black → no base_frame
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::NEUTRAL.frames();
        display->registerEmotion("neutral", anim1bit);
    }

    // Register idle emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::IDLE.width;
        anim1bit.height = asset::emotion::IDLE.height;
        anim1bit.frame_count = asset::emotion::IDLE.frame_count;
        anim1bit.fps = asset::emotion::IDLE.fps;
        anim1bit.loop = asset::emotion::IDLE.loop;
        anim1bit.max_packed_size = asset::emotion::IDLE.max_packed_size;
        // Frame 0 is diff from black → no base_frame
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::IDLE.frames();
        display->registerEmotion("idle", anim1bit);
    }
    // Register listening emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::LISTENING.width;
        anim1bit.height = asset::emotion::LISTENING.height;
        anim1bit.frame_count = asset::emotion::LISTENING.frame_count;
        anim1bit.fps = asset::emotion::LISTENING.fps;
        anim1bit.loop = asset::emotion::LISTENING.loop;
        anim1bit.max_packed_size = asset::emotion::LISTENING.max_packed_size;
        // Frame 0 is diff from black → no base_frame
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::LISTENING.frames();
        display->registerEmotion("listening", anim1bit);
    }
    // Register happy emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::HAPPY.width;
        anim1bit.height = asset::emotion::HAPPY.height;
        anim1bit.frame_count = asset::emotion::HAPPY.frame_count;
        anim1bit.fps = asset::emotion::HAPPY.fps;
        anim1bit.loop = asset::emotion::HAPPY.loop;
        anim1bit.max_packed_size = asset::emotion::HAPPY.max_packed_size;
        // Frame 0 is diff from black → no base_frame
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::HAPPY.frames();
        display->registerEmotion("happy", anim1bit);
    }

    // Register sad emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::SAD.width;
        anim1bit.height = asset::emotion::SAD.height;
        anim1bit.frame_count = asset::emotion::SAD.frame_count;
        anim1bit.fps = asset::emotion::SAD.fps;
        anim1bit.loop = asset::emotion::SAD.loop;
        anim1bit.max_packed_size = asset::emotion::SAD.max_packed_size;
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::SAD.frames();
        display->registerEmotion("sad", anim1bit);
    }

    // Register thinking emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::THINKING.width;
        anim1bit.height = asset::emotion::THINKING.height;
        anim1bit.frame_count = asset::emotion::THINKING.frame_count;
        anim1bit.fps = asset::emotion::THINKING.fps;
        anim1bit.loop = asset::emotion::THINKING.loop;
        anim1bit.max_packed_size = asset::emotion::THINKING.max_packed_size;
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::THINKING.frames();
        display->registerEmotion("thinking", anim1bit);
    }

    // Register stun emotion
    {
        Animation1Bit anim1bit;
        anim1bit.width = asset::emotion::STUN.width;
        anim1bit.height = asset::emotion::STUN.height;
        anim1bit.frame_count = asset::emotion::STUN.frame_count;
        anim1bit.fps = asset::emotion::STUN.fps;
        anim1bit.loop = asset::emotion::STUN.loop;
        anim1bit.max_packed_size = asset::emotion::STUN.max_packed_size;
        anim1bit.base_frame = nullptr;
        anim1bit.frames = asset::emotion::STUN.frames();
        display->registerEmotion("stun", anim1bit);
    }
}

// Centralized hardware/config values for easy tweaking
namespace device_cfg
{
    struct PowerPins
    {
        adc1_channel_t adc_channel = ADC1_CHANNEL_5; // Battery sense ADC channel (default GPIO33)
        gpio_num_t pin_chg = GPIO_NUM_34;            // Optional charge detect pin (active level depends on HW)
        gpio_num_t pin_full = GPIO_NUM_35;           // Optional full-battery pin (active level depends on HW)
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

// =================================================================================
// User-configurable settings (loaded from NVS namespace "usercfg")
// =================================================================================
namespace user_cfg
{
    struct UserSettings
    {
        std::string device_name = "PTalk";
        uint8_t volume = 60;      // 0-100 %
        uint8_t brightness = 100; // 0-100 %
        std::string wifi_ssid;
        std::string wifi_pass;
        std::string ws_url; // Stored WS URL (may be full URL or host:port)
        std::string mqtt_url; // Stored MQTT URL (may be full URL or host:port)
        
        // MEO SDK credentials
        std::string user_id;  // MEO user namespace
        std::string tx_key;   // MQTT password
    };

    static std::string get_string(nvs_handle_t h, const char *key)
    {
        size_t required = 0;
        if (nvs_get_str(h, key, nullptr, &required) != ESP_OK || required == 0)
            return {};
        std::string tmp;
        tmp.resize(required);
        if (nvs_get_str(h, key, tmp.data(), &required) != ESP_OK)
            return {};
        if (!tmp.empty() && tmp.back() == '\0')
            tmp.pop_back();
        return tmp;
    }

    static uint8_t get_u8(nvs_handle_t h, const char *key, uint8_t def_val)
    {
        uint8_t v = def_val;
        nvs_get_u8(h, key, &v);
        return v;
    }

    static UserSettings load()
    {
        UserSettings cfg;
        nvs_handle_t h;
        esp_err_t err = nvs_open("storage", NVS_READONLY, &h);
        if (err != ESP_OK)
        {
            ESP_LOGI(TAG, "storage not found, using defaults");
            return cfg;
        }

        cfg.device_name = get_string(h, "device_name");
        if (cfg.device_name.empty())
            cfg.device_name = "PTalk";

        // Đọc WiFi credentials với key giống bên ghi
        cfg.wifi_ssid = get_string(h, "ssid");
        cfg.wifi_pass = get_string(h, "pass");

        // Optional WS URL override
        cfg.ws_url = get_string(h, "ws_url");
        // Optional MQTT URL override
        cfg.mqtt_url = get_string(h, "mqtt_url");
        
        // MEO SDK credentials
        cfg.user_id = get_string(h, "user_id");
        cfg.tx_key = get_string(h, "tx_key");
        
        cfg.volume = get_u8(h, "volume", cfg.volume);
        cfg.brightness = get_u8(h, "brightness", cfg.brightness);

        nvs_close(h);
        return cfg;
    }

    static void save_all_settings(const BluetoothService::ConfigData &data)
    {
        ESP_LOGI("user_cfg", "save_all_settings called: ssid='%s', mqtt_url='%s', user_id='%s'",
                 data.ssid.c_str(), data.mqtt_url.c_str(), data.user_id.c_str());
        
        nvs_handle_t h;
        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK)
        {
            if (!data.device_name.empty())
                nvs_set_str(h, "device_name", data.device_name.c_str());
            if (!data.ssid.empty())
                nvs_set_str(h, "ssid", data.ssid.c_str());
            if (!data.pass.empty())
                nvs_set_str(h, "pass", data.pass.c_str());
            if (!data.ws_url.empty())
                nvs_set_str(h, "ws_url", data.ws_url.c_str());
            if (!data.mqtt_url.empty()) {
                nvs_set_str(h, "mqtt_url", data.mqtt_url.c_str());
                ESP_LOGI("user_cfg", "Saved mqtt_url='%s' to NVS", data.mqtt_url.c_str());
            } else {
                ESP_LOGW("user_cfg", "mqtt_url is empty, not saving");
            }
            
            // MEO SDK credentials
            if (!data.user_id.empty()) {
                nvs_set_str(h, "user_id", data.user_id.c_str());
                ESP_LOGI("user_cfg", "Saved user_id='%s' to NVS", data.user_id.c_str());
            }
            if (!data.tx_key.empty()) {
                nvs_set_str(h, "tx_key", data.tx_key.c_str());  // Actually save the key!
                ESP_LOGI("user_cfg", "Saved tx_key to NVS (len=%d)", (int)data.tx_key.length());
            }
            
            nvs_set_u8(h, "volume", data.volume);
            nvs_set_u8(h, "brightness", data.brightness);

            nvs_commit(h);
            nvs_close(h);
            ESP_LOGI("user_cfg", "All settings saved to NVS via BLE");
        }
        else
        {
            ESP_LOGE("user_cfg", "Failed to open NVS for saving!");
        }
    }
}

bool DeviceProfile::setup(AppController &app)
{
    ESP_LOGI(TAG, "DeviceProfile setup begin");

    // Ensure NVS is initialized before loading user settings
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    // Load user-overridable settings (from NVS) and merge with factory defaults
    user_cfg::UserSettings user = user_cfg::load();

    // =========================================================
    // 1️⃣ DISPLAY
    // =========================================================
    auto display_mgr = std::make_unique<DisplayManager>();

    // --- Display driver config (ST7789 240x320) ---
    DisplayDriver::Config lcd_cfg{
        .spi_host = device_cfg::display.spi_host,
        .pin_cs = device_cfg::display.pin_cs,
        .pin_dc = device_cfg::display.pin_dc,
        .pin_rst = device_cfg::display.pin_rst,
        .pin_bl = device_cfg::display.pin_bl,
        .pin_mosi = device_cfg::display.pin_mosi,
        .pin_sclk = device_cfg::display.pin_sclk,

        .width = 240,
        .height = 320,

        // 240x320 full screen (no offset needed)
        .x_offset = 0,
        .y_offset = 0,
        .spi_speed_hz = device_cfg::display.spi_speed_hz};

    auto lcd_driver = std::make_unique<DisplayDriver>();

    if (!lcd_driver->init(lcd_cfg))
    {
        ESP_LOGE(TAG, "DisplayDriver init failed");
        return false;
    }

    if (!display_mgr->init(std::move(lcd_driver), 240, 320))
    {
        ESP_LOGE(TAG, "DisplayManager init failed");
        return false;
    }

    // Auto-bind UI to state changes so animations/icons update reactively
    display_mgr->enableStateBinding(true);

    // Apply user brightness preference (0-100%)
    display_mgr->setBrightness(user.brightness);

    // --- Register UI assets ---
    // Emotions (animations)
    registerEmotions(display_mgr.get());

    // Icons (static images)
    // display->registerIcon("wifi_ok",         asset::icon::WIFI_OK);
    // display->registerIcon("wifi_fail",       asset::icon::WIFI_FAIL);
    // display->registerIcon("battery",         asset::icon::BATTERY);
    // display->registerIcon("battery_low",     asset::icon::BATTERY_LOW);
    display_mgr->registerIcon(
        "battery_charge",
        DisplayManager::Icon{
            asset::icon::BATTERY_CHARGE.w,
            asset::icon::BATTERY_CHARGE.h,
            asset::icon::BATTERY_CHARGE.rle_data});
    display_mgr->registerIcon(
        "battery_full",
        DisplayManager::Icon{
            asset::icon::BATTERY_FULL.h,
            asset::icon::BATTERY_FULL.w,
            asset::icon::BATTERY_FULL.rle_data});
    display_mgr->registerIcon(
        "battery_critical",
        DisplayManager::Icon{
            asset::icon::CRITICAL_POWER.w,
            asset::icon::CRITICAL_POWER.h,
            asset::icon::CRITICAL_POWER.rle_data});

    // =========================================================
    // 2️⃣ AUDIO
    // =========================================================
    auto audio_mgr = std::make_unique<AudioManager>();

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

    // Apply user volume preference (0-100%)
    speaker->setVolume(user.volume);

    // --- Codec ---
    auto codec = std::make_unique<AdpcmCodec>();

    // Wire dependencies into AudioManager before init/start
    audio_mgr->setInput(std::move(mic));
    audio_mgr->setOutput(std::move(speaker));
    audio_mgr->setCodec(std::move(codec));

    if (!audio_mgr->init())
    {
        ESP_LOGE(TAG, "AudioManager init failed");
        return false;
    }

    // audio_mgr->start();

    // =========================================================
    // 3️⃣ NETWORK
    // =========================================================
    auto network_mgr = std::make_unique<NetworkManager>();

    // Configure captive portal and WebSocket server endpoint here
    NetworkManager::Config net_cfg{};
    net_cfg.ap_ssid = "PTalk-Portal"; // SSID hiển thị khi mở portal
    net_cfg.ap_max_clients = 4;       // Số thiết bị tối đa kết nối vào portal

    // Xác định WS URL: ưu tiên lấy từ NVS; nếu trống dùng mặc định "171.226.10.121:8000"
    auto normalize_ws_url = [](std::string val) -> std::string {
        // Trim spaces
        auto trim = [](std::string &s){
            while (!s.empty() && (s.front()==' '||s.front()=='\t'||s.front()=='\n'||s.front()=='\r')) s.erase(s.begin());
            while (!s.empty() && (s.back()==' '||s.back()=='\t'||s.back()=='\n'||s.back()=='\r')) s.pop_back();
        };
        trim(val);
        if (val.empty()) return std::string();
        // Already a ws(s):// URL
        if (val.rfind("ws://", 0) == 0 || val.rfind("wss://", 0) == 0) {
            // Ensure it has a path; if missing, append /ws
            auto pos_slash = val.find('/', val.find("://") + 3);
            if (pos_slash == std::string::npos) val += "/ws";
            return val;
        }
        // Convert http(s):// to ws(s)://
        if (val.rfind("http://", 0) == 0) {
            val.replace(0, 7, "ws://");
            auto pos_slash = val.find('/', val.find("://") + 3);
            if (pos_slash == std::string::npos) val += "/ws";
            return val;
        }
        if (val.rfind("https://", 0) == 0) {
            val.replace(0, 8, "wss://");
            auto pos_slash = val.find('/', val.find("://") + 3);
            if (pos_slash == std::string::npos) val += "/ws";
            return val;
        }
        // Assume host:port → prepend ws:// and append /ws
        return std::string("ws://") + val + "/ws";
    };

    auto normalize_mqtt_url = [](std::string val) -> std::string {
        // Trim spaces
        auto trim = [](std::string &s){
            while (!s.empty() && (s.front()==' '||s.front()=='\t'||s.front()=='\n'||s.front()=='\r')) s.erase(s.begin());
            while (!s.empty() && (s.back()==' '||s.back()=='\t'||s.back()=='\n'||s.back()=='\r')) s.pop_back();
        };
        trim(val);
        if (val.empty()) return std::string();
        // Already a mqtt(s):// URL
        if (val.rfind("mqtt://", 0) == 0 || val.rfind("mqtts://", 0) == 0) {
            return val;
        }
        // Convert tcp(s):// to mqtt(s)://
        if (val.rfind("tcp://", 0) == 0) {
            val.replace(0, 6, "mqtt://");
            return val;
        }
        if (val.rfind("tcps://", 0) == 0) {
            val.replace(0, 7, "mqtts://");
            return val;
        }
        // Assume host:port → prepend mqtt://
        return std::string("mqtt://") + val;
    };

    const std::string default_hostport = "171.226.10.121:8000";
    std::string chosen_ws = user.ws_url.empty() ? default_hostport : user.ws_url;
    net_cfg.ws_url = normalize_ws_url(chosen_ws);
    
    const std::string default_mqtt = "171.226.10.121:1883";
    std::string chosen_mqtt = user.mqtt_url.empty() ? default_mqtt : user.mqtt_url;
    net_cfg.mqtt_url = normalize_mqtt_url(chosen_mqtt);
    
    // MEO SDK credentials (loaded from NVS, set via BLE)
    net_cfg.user_id = user.user_id;
    net_cfg.tx_key = user.tx_key;

    if (!network_mgr->init(net_cfg))
    {
        ESP_LOGE(TAG, "NetworkManager init failed");
        return false;
    }

    // If user provided Wi-Fi credentials in user settings, try them first
    if (!user.wifi_ssid.empty())
    {
        network_mgr->setCredentials(user.wifi_ssid, user.wifi_pass);
    }

    // --- Network → Audio wiring ---
    // Push incoming binary (ADPCM) from WS into speaker ringbuffer
    // and drive InteractionState to SPEAKING while audio is arriving.
    StreamBufferHandle_t spk_sb = audio_mgr->getSpeakerEncodedBuffer();
    network_mgr->setMicBuffer(audio_mgr->getMicEncodedBuffer()); // Uplink mic buffer
    // Expose managers to NetworkManager for real-time config (volume/brightness)
    network_mgr->setManagers(audio_mgr.get(), display_mgr.get());
    AudioManager *audio_ptr = audio_mgr.get();                   // Capture pointer for disconnect handler
    NetworkManager *network_ptr = network_mgr.get();             // For session flag access

    network_mgr->onServerBinary([spk_sb, network_ptr](const uint8_t *data, size_t len)
                                {
        if (!data || len == 0) return;
        auto interaction = StateManager::instance().getInteractionState();
        // Only accept downlink audio when a speaking session is active
        if (interaction == state::InteractionState::LISTENING || !network_ptr->isSpeakingSessionActive()) {
            static uint32_t ignored = 0;
            if (++ignored % 10 == 0) {
                ESP_LOGW("Network", "Ignoring audio binary (state=%d, speaking_session=%d)",
                         static_cast<int>(interaction), network_ptr->isSpeakingSessionActive());
            }
            return;
        }
        // Feed encoded data to AudioManager's downlink buffer
        size_t written = xStreamBufferSend(spk_sb, data, len, pdMS_TO_TICKS(100));
        if (written != len) {
            static uint32_t drop_count = 0;
            if (++drop_count % 10 == 0) {
                ESP_LOGW("Network", "ADPCM buffer full! Dropped %zu bytes (wanted %zu)", len - written, len);
            }
        }
        });

    // Handle WS disconnect - must cleanup to unblock speaker task
    network_mgr->onDisconnect([spk_sb, audio_ptr]()
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
    network_mgr->onServerText([network_ptr](const std::string &msg)
                              {
        auto& sm = StateManager::instance();
        if (msg == "PROCESSING_START" || msg == "PROCESSING") {
            sm.setInteractionState(state::InteractionState::PROCESSING,
                                   state::InputSource::SERVER_COMMAND);
        } else if (msg == "AUDIO_START"||msg == "SPEAKING" || msg == "SPEAK_START") {
            // Server explicitly starts audio; enter speaking state once per session
            if (!network_ptr->isSpeakingSessionActive()) {
                network_ptr->startSpeakingSession();
                sm.setInteractionState(state::InteractionState::SPEAKING,
                                       state::InputSource::SERVER_COMMAND);
            }
        // } else if (msg == "LISTENING") {
        //     sm.setInteractionState(state::InteractionState::LISTENING,
        //                            state::InputSource::SERVER_COMMAND);
        // } else if (msg == "SPEAKING" || msg == "SPEAK_START") {
        //     sm.setInteractionState(state::InteractionState::SPEAKING,
        //                            state::InputSource::SERVER_COMMAND);
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

    static state::InteractionState prev_interaction_state = state::InteractionState::IDLE;

    sm.subscribeInteraction([network_ptr](state::InteractionState new_state, state::InputSource src)
                            {
    // 🟢 Vừa nhấn nút (Vào LISTENING)
    if (new_state == state::InteractionState::LISTENING && prev_interaction_state != state::InteractionState::LISTENING) {
        // End any ongoing speaking session to allow next TTS session
        network_ptr->endSpeakingSession();
        network_ptr->sendText("START");
    }
    // 🔴 Vừa thả nút (Thoát LISTENING)
    else if (new_state != state::InteractionState::LISTENING && prev_interaction_state == state::InteractionState::LISTENING) {
        network_ptr->sendText("END");
    }

    // Immune Mode duy trì kết nối (Giữ nguyên)
    if (new_state == state::InteractionState::SPEAKING) {
        network_ptr->setWSImmuneMode(true);
    } else {
        network_ptr->setWSImmuneMode(false);
    }

    prev_interaction_state = new_state; });

    // =========================================================
    // BluetoothService for BLE-based configuration
    auto ble_service = std::make_shared<BluetoothService>();
    ble_service->onConfigComplete([&app](const BluetoothService::ConfigData &data)
                                  {
    user_cfg::save_all_settings(data);
    
    // Tạo AppEvent mới để AppController biết đã config xong
    app.postEvent(event::AppEvent::CONFIG_DONE_RESTART); });

    network_mgr->setBluetoothService(ble_service);
    // =========================================================
    // 4️⃣ TOUCH INPUT
    // =========================================================
    auto touch_input = std::make_unique<TouchInput>();

    TouchInput::Config touch_cfg{
        .pin = GPIO_NUM_16,
        .active_low = true,
        .long_press_ms = 1200};

    if (!touch_input->init(touch_cfg))
    {
        ESP_LOGE(TAG, "TouchInput init failed");
        return false;
    }

    touch_input->onEvent([&app](TouchInput::Event e)
                         {
        if (e == TouchInput::Event::PRESS) {
            app.postEvent(event::AppEvent::USER_BUTTON);
        }
        if (e == TouchInput::Event::RELEASE) {
            // Currently no action on release
            app.postEvent(event::AppEvent::RELEASE_BUTTON);
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
    // power_cfg.low_battery_percent = 15.0f; // low battery warning at 15%
    power_cfg.critical_percent = 5.0f; // critical battery (auto sleep) at 5%
    power_cfg.enable_smoothing = true; // enable smoothing filter
    power_cfg.smoothing_alpha = 0.15f; // smoothing factor alpha

    // Battery sensing hardware (divider + optional charge/full pins)
    auto power_driver = std::make_unique<Power>(
        device_cfg::power.adc_channel,
        device_cfg::power.pin_chg,  // set to GPIO pin if hardware provides charge indicator
        device_cfg::power.pin_full, // set to GPIO pin if hardware provides full indicator
        device_cfg::power.r1_ohm,
        device_cfg::power.r2_ohm);

    auto power_mgr = std::make_unique<PowerManager>(std::move(power_driver), power_cfg);

    // Link PowerManager → DisplayManager for battery % updates
    power_mgr->setDisplayManager(display_mgr.get());

    // Expose PowerManager so WS status/handshake can report real battery %
    network_mgr->setPowerManager(power_mgr.get());

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
        std::move(display_mgr),
        std::move(audio_mgr),
        std::move(network_mgr),
        std::move(power_mgr),
        std::move(touch_input),
        std::move(ota));

    // Apply app-level configuration (deep sleep interval, etc.)
    app.setConfig(app_cfg);

    ESP_LOGI(TAG, "DeviceProfile setup OK");
    return true;
}
