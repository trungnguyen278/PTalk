#include "DisplayManager.hpp"
#include "DisplayDriver.hpp"
#include "Framebuffer.hpp"
#include "AnimationPlayer.hpp"

#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char* TAG = "DisplayManager";

DisplayManager::DisplayManager() = default;
DisplayManager::~DisplayManager() {
    // Ensure task is stopped before destruction
    stopLoop();
    // Unsubscribe
    if (binding_enabled) {
        auto &sm = StateManager::instance();
        if (sub_inter  != -1) sm.unsubscribeInteraction(sub_inter);
        if (sub_conn   != -1) sm.unsubscribeConnectivity(sub_conn);
        if (sub_sys    != -1) sm.unsubscribeSystem(sub_sys);
        if (sub_power  != -1) sm.unsubscribePower(sub_power);
    }
}

// ----------------------------------------------------------------------------
// Init
// ----------------------------------------------------------------------------
bool DisplayManager::init(std::unique_ptr<DisplayDriver> driver, int width, int height)
{
    if (!driver) {
        ESP_LOGE(TAG, "init: DisplayDriver is null");
        return false;
    }

    drv = std::move(driver);
    width_ = width;
    height_ = height;

    fb = std::make_unique<Framebuffer>(width, height);
    anim_player = std::make_unique<AnimationPlayer>(fb.get(), drv.get());

    ESP_LOGI(TAG, "DisplayManager init OK (%dx%d)", width, height);
    return true;
}

// ----------------------------------------------------------------------------
// Task loop management
// ----------------------------------------------------------------------------
bool DisplayManager::startLoop(uint32_t interval_ms,
                               UBaseType_t priority,
                               uint32_t stackSize,
                               BaseType_t core)
{
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "startLoop: already running");
        update_interval_ms_ = interval_ms;
        return true;
    }
    if (!drv || !fb || !anim_player) {
        ESP_LOGE(TAG, "startLoop: not initialized");
        return false;
    }

    update_interval_ms_ = interval_ms;

#if defined(ESP_PLATFORM)
    BaseType_t rc = xTaskCreatePinnedToCore(
        &DisplayManager::taskEntry,
        "DisplayLoop",
        stackSize,
        this,
        priority,
        &task_handle_,
        core
    );
#else
    BaseType_t rc = xTaskCreate(
        &DisplayManager::taskEntry,
        "DisplayLoop",
        stackSize,
        this,
        priority,
        &task_handle_
    );
#endif

    if (rc != pdPASS) {
        ESP_LOGE(TAG, "startLoop: xTaskCreate failed (%d)", (int)rc);
        task_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Display loop started (interval=%ums)", (unsigned)update_interval_ms_);
    return true;
}

void DisplayManager::stopLoop()
{
    if (task_handle_ == nullptr) return;
    TaskHandle_t th = task_handle_;
    task_handle_ = nullptr;
    vTaskDelete(th);
    ESP_LOGI(TAG, "Display loop stopped");
}

// ----------------------------------------------------------------------------
// State binding
// ----------------------------------------------------------------------------
void DisplayManager::enableStateBinding(bool enable)
{
    binding_enabled = enable;

    if (!enable) return;

    auto &sm = StateManager::instance();

    sub_inter = sm.subscribeInteraction([this](state::InteractionState s, state::InputSource src) {
        this->handleInteraction(s, src);
    });

    sub_conn = sm.subscribeConnectivity([this](state::ConnectivityState s) {
        this->handleConnectivity(s);
    });

    sub_sys = sm.subscribeSystem([this](state::SystemState s) {
        this->handleSystem(s);
    });

    sub_power = sm.subscribePower([this](state::PowerState s) {
        this->handlePower(s);
    });

    ESP_LOGI(TAG, "DisplayManager state binding enabled");
}

// ----------------------------------------------------------------------------
// High-level UI API
// ----------------------------------------------------------------------------

void DisplayManager::showIdle() {
    playEmotion("idle");
    toast_active = false;
}

void DisplayManager::showListening(state::InputSource src) {
    playEmotion("listening");

    switch (src) {
        case state::InputSource::BUTTON: showToast("Button → Listening"); break;
        case state::InputSource::WAKEWORD: showToast("Wakeword detected"); break;
        case state::InputSource::SERVER_COMMAND: showToast("Server request"); break;
        default: break;
    }
}

void DisplayManager::showThinking() {
    playEmotion("thinking");
    showToast("Processing...");
}

void DisplayManager::showSpeaking() {
    playEmotion("speaking");
}

void DisplayManager::showError(const char* msg) {
    playEmotion("error");
    showToast(msg ? msg : "Error");
}

void DisplayManager::showLowBattery() {
    playIcon("battery_low");
    showToast("Low battery");
}

void DisplayManager::showCharging() {
    playIcon("battery_charge");
    showToast("Charging...");
}

void DisplayManager::showFullBattery() {
    playIcon("battery_full");
    showToast("Full battery");
}

void DisplayManager::setBatteryPercent(uint8_t p) {
    battery_percent = p;
}


// ----------------------------------------------------------------------------
// Toast
// ----------------------------------------------------------------------------
void DisplayManager::showToast(const std::string& text, uint32_t duration_ms)
{
    toast_text = text;
    toast_timer = duration_ms;
    toast_active = true;
}

// ----------------------------------------------------------------------------
// Power saving mode
// ----------------------------------------------------------------------------
void DisplayManager::setPowerSaveMode(bool enable)
{
    if (enable) {
        anim_player->pause();
    } else {
        anim_player->resume();
    }
}

// ----------------------------------------------------------------------------
// Asset registration
// ----------------------------------------------------------------------------
void DisplayManager::registerEmotion(const std::string& name, const Animation& anim) {
    emotions[name] = anim;
}

void DisplayManager::registerIcon(const std::string& name, const Icon& icon) {
    icons[name] = icon;
}

// ----------------------------------------------------------------------------
// Update (should be called from UI task)
void DisplayManager::update(uint32_t dt_ms)
{
    if (!fb || !drv) return;

    // 1) update animation frame
    anim_player->update(dt_ms);

    // 2) draw icon if needed
    // (icons are drawn as overlay → always on top)
    // (you can extend later with battery percent etc.)

    // 3) draw toast text
    if (toast_active && toast_timer > 0) {
        drv->drawTextCenter(fb.get(), toast_text.c_str(), 0xFFFF, width_/2, height_-25);
        toast_timer = (toast_timer > dt_ms) ? toast_timer - dt_ms : 0;
        if (toast_timer == 0) toast_active = false;
    }
    // 4) draw battery percent if available
    if (battery_percent != 255) {
        std::string bat_str = std::to_string(battery_percent) + "%";
        drv->drawText(fb.get(), bat_str.c_str(), 0xFFFF, width_-40, 10);
    }

    // 5) push framebuffer to display
    drv->flush(fb.get());
}

void DisplayManager::taskEntry(void* arg)
{
    auto* self = static_cast<DisplayManager*>(arg);
    TickType_t prev = xTaskGetTickCount();
    for (;;) {
        if (self->task_handle_ == nullptr) {
            // Stopped; exit task
            vTaskDelete(nullptr);
        }
        TickType_t now = xTaskGetTickCount();
        uint32_t dt_ms = (now - prev) * portTICK_PERIOD_MS;
        prev = now;
        self->update(dt_ms);
        vTaskDelay(pdMS_TO_TICKS(self->update_interval_ms_));
    }
}

// ----------------------------------------------------------------------------
// MAPPING STATE → UI BEHAVIOR
// ----------------------------------------------------------------------------

void DisplayManager::handleInteraction(state::InteractionState s, state::InputSource src)
{
    switch (s) {
        case state::InteractionState::TRIGGERED:
        case state::InteractionState::LISTENING:
            showListening(src);
            break;
        case state::InteractionState::PROCESSING:
            showThinking();
            break;
        case state::InteractionState::SPEAKING:
            showSpeaking();
            break;
        case state::InteractionState::CANCELLING:
            showToast("Cancelled", 1500);
            break;
        case state::InteractionState::MUTED:
            showToast("Muted");
            break;
        case state::InteractionState::SLEEPING:
            setPowerSaveMode(true);
            break;
        case state::InteractionState::IDLE:
        default:
            showIdle();
            break;
    }
}

void DisplayManager::handleConnectivity(state::ConnectivityState s)
{
    switch (s) {
        case state::ConnectivityState::OFFLINE:
            playIcon("wifi_fail");
            showToast("Offline");
            break;

        case state::ConnectivityState::CONNECTING_WIFI:
            showToast("Connecting WiFi...");
            break;

        case state::ConnectivityState::WIFI_PORTAL:
            showToast("WiFi Portal Mode");
            break;

        case state::ConnectivityState::CONNECTING_WS:
            showToast("Connecting Server...");
            break;

        case state::ConnectivityState::ONLINE:
            playIcon("wifi_ok");
            showToast("Online");
            break;
    }
}

void DisplayManager::handleSystem(state::SystemState s)
{
    switch (s) {
        case state::SystemState::BOOTING:
            playEmotion("boot");
            showToast("Booting...");
            break;

        case state::SystemState::RUNNING:
            showIdle();
            break;

        case state::SystemState::ERROR:
            showError("System Error");
            break;

        case state::SystemState::MAINTENANCE:
            showToast("Maintenance Mode");
            playEmotion("maintenance");
            break;

        case state::SystemState::UPDATING_FIRMWARE:
            showToast("Updating...");
            playEmotion("updating");
            break;

        case state::SystemState::FACTORY_RESETTING:
            showToast("Factory Reset...");
            playEmotion("reset");
            break;
    }
}

void DisplayManager::handlePower(state::PowerState s)
{
    switch (s) {
        case state::PowerState::NORMAL:
            playIcon("battery");
            break;

        case state::PowerState::LOW_BATTERY:
            showLowBattery();
            break;

        case state::PowerState::CHARGING:
            showCharging();
            break;

        case state::PowerState::FULL_BATTERY:
            showFullBattery();
            break;

        case state::PowerState::POWER_SAVING:
            setPowerSaveMode(true);
            break;

        case state::PowerState::CRITICAL:
            showToast("Battery Critical!", 4000);
            playEmotion("lowbat");
            break;

        case state::PowerState::ERROR:
            playEmotion("error");
            showToast("Battery Error!", 4000);
            break;
    }
}

// ----------------------------------------------------------------------------
// Internal asset playback
void DisplayManager::playEmotion(const std::string& name, int x, int y)
{
    auto it = emotions.find(name);
    if (it == emotions.end()) {
        ESP_LOGW(TAG, "Emotion '%s' not found", name.c_str());
        return;
    }

    const Animation& anim = it->second;

    // Clear previous
    //fb->clear(0x0000);

    // Start animation centered or at (x,y)
    anim_player->setAnimation(anim, x, y);
}

void DisplayManager::playIcon(const std::string& name, int x, int y)
{
    auto it = icons.find(name);
    if (it == icons.end()) {
        ESP_LOGW(TAG, "Icon '%s' not found", name.c_str());
        return;
    }

    const Icon& ico = it->second;

    // Clear background but keep animation if needed
    // For now we clear full (sau chỉnh logic overlay):
    //fb->clear(0x0000);

    fb->drawBitmap(x, y, ico.w, ico.h, ico.rgb);
}
