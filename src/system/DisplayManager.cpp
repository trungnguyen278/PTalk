#include "DisplayManager.hpp"
#include "DisplayDriver.hpp"
#include "Framebuffer.hpp"
#include "AnimationPlayer.hpp"

#include <utility>
#include <atomic>

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
    
    // ✅ Signal graceful shutdown instead of force-deleting
    task_running_.store(false);
    
    // Wait for task to exit (max 1 second)
    uint32_t wait_ms = 0;
    const uint32_t TIMEOUT_MS = 1000;
    while (task_handle_ != nullptr && wait_ms < TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_ms += 10;
    }
    
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Display task did not exit; force deleting");
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    
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

// (public show* helpers removed; UI is driven via handlers)

void DisplayManager::setBatteryPercent(uint8_t p) {
    battery_percent = p;
}


// (toast feature removed)

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

void DisplayManager::setBacklight(bool on)
{
    if (drv) {
        drv->setBacklight(on);
    }
}

// ----------------------------------------------------------------------------
// Asset registration
// ----------------------------------------------------------------------------
void DisplayManager::registerEmotion(const std::string& name, const Animation1Bit& anim) {
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

    // ✅ DEBUG: Log first update to verify task is running
    static bool first_update = true;
    if (first_update) {
        ESP_LOGI(TAG, "First update() called - display loop is working");
        first_update = false;
    }

    // 1) update animation frame
    anim_player->update(dt_ms);
    
    // ✅ 2) Render animation frame to framebuffer
    anim_player->render();

    // 3) draw icon if needed
    // (icons are drawn as overlay → always on top)
    // (you can extend later with battery percent etc.)

    // (toast drawing removed)
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
    self->task_running_.store(true);  // ✅ Signal task is running
    TickType_t prev = xTaskGetTickCount();
    
    while (self->task_running_.load()) {  // ✅ Check graceful shutdown flag
        TickType_t now = xTaskGetTickCount();
        uint32_t dt_ms = (now - prev) * portTICK_PERIOD_MS;
        prev = now;
        ESP_LOGD(TAG, "DisplayManager update dt_ms=%u", dt_ms);
        self->update(dt_ms);
        vTaskDelay(pdMS_TO_TICKS(self->update_interval_ms_));
    }
    
    // ✅ Graceful exit: cleanup and notify stopper
    self->task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

// ----------------------------------------------------------------------------
// MAPPING STATE → UI BEHAVIOR
// ----------------------------------------------------------------------------

void DisplayManager::handleInteraction(state::InteractionState s, state::InputSource src)
{
    switch (s) {
        case state::InteractionState::TRIGGERED:
        case state::InteractionState::LISTENING:
            playEmotion("listening");
            break;
        case state::InteractionState::PROCESSING:
            playEmotion("thinking");
            break;
        case state::InteractionState::SPEAKING:
            playEmotion("speaking");
            break;
        case state::InteractionState::CANCELLING:
            break;
        case state::InteractionState::MUTED:
            break;
        case state::InteractionState::SLEEPING:
            setPowerSaveMode(true);
            break;
        case state::InteractionState::IDLE:
        default:
            playEmotion("idle");
            break;
    }
}

void DisplayManager::handleConnectivity(state::ConnectivityState s)
{
    switch (s) {
        case state::ConnectivityState::OFFLINE:
            playIcon("wifi_fail");
            break;

        case state::ConnectivityState::CONNECTING_WIFI:
            break;

        case state::ConnectivityState::WIFI_PORTAL:
            break;

        case state::ConnectivityState::CONNECTING_WS:
            break;

        case state::ConnectivityState::ONLINE:
            playIcon("wifi_ok");
            break;
    }
}

void DisplayManager::handleSystem(state::SystemState s)
{
    switch (s) {
        case state::SystemState::BOOTING:
            playEmotion("boot");
            break;

        case state::SystemState::RUNNING:
            playEmotion("idle");
            break;

        case state::SystemState::ERROR:
            playEmotion("error");
            break;

        case state::SystemState::MAINTENANCE:
            playEmotion("maintenance");
            break;

        case state::SystemState::UPDATING_FIRMWARE:
            playEmotion("updating");
            break;

        case state::SystemState::FACTORY_RESETTING:
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
            playIcon("battery_low");
            break;

        case state::PowerState::CHARGING:
            playIcon("battery_charge");
            break;

        case state::PowerState::FULL_BATTERY:
            playIcon("battery_full");
            break;

        case state::PowerState::POWER_SAVING:
            setPowerSaveMode(true);
            break;

        case state::PowerState::CRITICAL:
            ESP_LOGI(TAG, "CRITICAL: show critical battery icon");
            // Show registered critical battery icon fullscreen
            playIcon("battery_critical", IconPlacement::Fullscreen);
            // ✅ Removed blocking delay - icon stays visible via display loop
            break;

        case state::PowerState::ERROR:
            playEmotion("error");
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

    const Animation1Bit& anim = it->second;

    ESP_LOGI(TAG, "playEmotion '%s' starting animation", name.c_str());

    // Clear previous
    //fb->clear(0x0000);

    // Start animation centered or at (x,y)
    anim_player->setAnimation(anim, x, y);
}

void DisplayManager::playIcon(const std::string& name,
                              IconPlacement placement,
                              int x,
                              int y)
{
    auto it = icons.find(name);
    if (it == icons.end()) {
        ESP_LOGW(TAG, "Icon '%s' not found", name.c_str());
        return;
    }

    const Icon& ico = it->second;

    int draw_x = x;
    int draw_y = y;

    ESP_LOGI(TAG, "playIcon '%s' placement=%d size=%dx%d", name.c_str(), (int)placement, ico.w, ico.h);
    
    // ✅ DEBUG: Check icon data validity
    if (ico.rgb == nullptr) {
        ESP_LOGE(TAG, "Icon '%s' has NULL rgb data!", name.c_str());
        return;
    }
    ESP_LOGI(TAG, "Icon data ptr: %p, first pixel: 0x%04X", ico.rgb, ico.rgb[0]);

    switch (placement) {
        case IconPlacement::Center:
            draw_x = (width_ - ico.w) / 2;
            draw_y = (height_ - ico.h) / 2;
            break;
        case IconPlacement::TopRight:
            draw_x = width_ - ico.w - 8;  // small margin from edges
            draw_y = 8;
            break;
        case IconPlacement::Fullscreen:
            draw_x = 0;
            draw_y = 0;
            break;
        case IconPlacement::Custom:
        default:
            // keep provided x,y
            break;
    }

    // Clamp to screen bounds to avoid out-of-range writes
    if (draw_x < 0) draw_x = 0;
    if (draw_y < 0) draw_y = 0;

    ESP_LOGI(TAG, "drawBitmap at (%d,%d)", draw_x, draw_y);
    fb->drawBitmap(draw_x, draw_y, ico.w, ico.h, ico.rgb);

    // Push immediately so critical/one-shot icons show even if the loop stops soon
    if (drv) {
        ESP_LOGI(TAG, "Flushing framebuffer immediately");
        drv->flush(fb.get());
    } else {
        ESP_LOGW(TAG, "drv is null, cannot flush");
    }
}

// ============================================================================
// OTA Update UI Functions
// ============================================================================

void DisplayManager::showOTAUpdating()
{
    ESP_LOGI(TAG, "Showing OTA updating screen");
    ota_updating = true;
    ota_completed = false;
    ota_error = false;
    ota_progress_percent = 0;
    ota_status_text = "Starting update...";
    
    if (!fb || !drv) return;
    
    fb->clear(0x0000);  // Clear screen (black background)
    // TODO: Draw "Updating Firmware" title and progress bar outline
    // TODO: Display initial message
    drv->flush(fb.get());
}

void DisplayManager::setOTAProgress(uint8_t current_percent)
{
    if (current_percent > 100) current_percent = 100;
    
    ota_progress_percent = current_percent;
    
    if (!fb || !drv) return;
    
    // Update progress bar on display (0-100%)
    // TODO: Draw progress bar visual
    // Example: draw bar from left to right
    // int bar_width = (width_ * current_percent) / 100;
    // fb->drawRect(10, 120, bar_width, 20, 0x07E0);  // green bar
    
    ESP_LOGD(TAG, "OTA progress: %u%%", current_percent);
    drv->flush(fb.get());
}

void DisplayManager::setOTAStatus(const std::string& status)
{
    ota_status_text = status;
    ESP_LOGI(TAG, "OTA status: %s", status.c_str());
    
    if (!fb || !drv) return;
    
    // TODO: Display status text on screen
    // Example: show at bottom of screen
    drv->flush(fb.get());
}

void DisplayManager::showOTACompleted()
{
    ESP_LOGI(TAG, "Showing OTA completed screen");
    ota_updating = false;
    ota_completed = true;
    ota_error = false;
    ota_progress_percent = 100;
    ota_status_text = "Update completed!";
    
    if (!fb || !drv) return;
    
    fb->clear(0x0000);
    // TODO: Draw checkmark or success animation
    // TODO: Display "Update Successful" message
    drv->flush(fb.get());
}

void DisplayManager::showOTAError(const std::string& error_msg)
{
    ESP_LOGE(TAG, "Showing OTA error: %s", error_msg.c_str());
    ota_updating = false;
    ota_completed = false;
    ota_error = true;
    ota_error_msg = error_msg;
    ota_status_text = "Update failed!";
    
    if (!fb || !drv) return;
    
    fb->clear(0x0000);
    // TODO: Draw error icon (X mark)
    // TODO: Display error message
    drv->flush(fb.get());
}

void DisplayManager::showRebooting()
{
    ESP_LOGI(TAG, "Showing rebooting screen");
    ota_updating = false;
    ota_completed = true;
    ota_error = false;
    ota_status_text = "Rebooting...";
    
    if (!fb || !drv) return;
    
    fb->clear(0x0000);
    // TODO: Draw rebooting animation or countdown
    // TODO: Display "Device restarting..." message
    drv->flush(fb.get());
}
