#include "DisplayManager.hpp"
#include "DisplayDriver.hpp"
// Framebuffer.hpp removed - using direct rendering
#include "AnimationPlayer.hpp"

#include <utility>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "DisplayManager";

DisplayManager::DisplayManager() = default;
DisplayManager::~DisplayManager()
{
    // Ensure task is stopped before destruction
    stopLoop();
    // Unsubscribe
    if (binding_enabled)
    {
        auto &sm = StateManager::instance();
        if (sub_inter != -1)
            sm.unsubscribeInteraction(sub_inter);
        if (sub_conn != -1)
            sm.unsubscribeConnectivity(sub_conn);
        if (sub_sys != -1)
            sm.unsubscribeSystem(sub_sys);
        if (sub_power != -1)
            sm.unsubscribePower(sub_power);
        if (sub_emotion != -1)
            sm.unsubscribeEmotion(sub_emotion);
    }
}

// ----------------------------------------------------------------------------
// Init
// ----------------------------------------------------------------------------
bool DisplayManager::init(std::unique_ptr<DisplayDriver> driver, int width, int height)
{
    if (!driver)
    {
        ESP_LOGE(TAG, "init: DisplayDriver is null");
        return false;
    }

    drv = std::move(driver);
    width_ = width;
    height_ = height;

    drv->setRotation(1); // Landscape
    // Sync dimensions after rotation (driver swaps width↔height for landscape)
    width_ = drv->width();
    height_ = drv->height();

    // AnimationPlayer renders directly to display - no framebuffer needed!
    anim_player = std::make_unique<AnimationPlayer>(drv.get());

    ESP_LOGI(TAG, "DisplayManager init OK (%dx%d) - Framebuffer-less architecture", width, height);
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
    if (task_handle_ != nullptr)
    {
        ESP_LOGW(TAG, "startLoop: already running");
        update_interval_ms_ = interval_ms;
        return true;
    }
    if (!drv || !anim_player)
    {
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
        core);
#else
    BaseType_t rc = xTaskCreate(
        &DisplayManager::taskEntry,
        "DisplayLoop",
        stackSize,
        this,
        priority,
        &task_handle_);
#endif

    if (rc != pdPASS)
    {
        ESP_LOGE(TAG, "startLoop: xTaskCreate failed (%d)", (int)rc);
        task_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Display loop started (interval=%ums)", (unsigned)update_interval_ms_);
    return true;
}

void DisplayManager::stopLoop()
{
    if (task_handle_ == nullptr)
        return;

    // ✅ Signal graceful shutdown instead of force-deleting
    task_running_.store(false);

    // Wait for task to exit (max 1 second)
    uint32_t wait_ms = 0;
    const uint32_t TIMEOUT_MS = 1000;
    while (task_handle_ != nullptr && wait_ms < TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_ms += 10;
    }

    if (task_handle_ != nullptr)
    {
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

    if (!enable)
        return;

    auto &sm = StateManager::instance();

    sub_inter = sm.subscribeInteraction([this](state::InteractionState s, state::InputSource src)
                                        { this->handleInteraction(s, src); });

    sub_conn = sm.subscribeConnectivity([this](state::ConnectivityState s)
                                        { this->handleConnectivity(s); });

    sub_sys = sm.subscribeSystem([this](state::SystemState s)
                                 { this->handleSystem(s); });

    sub_power = sm.subscribePower([this](state::PowerState s)
                                  { this->handlePower(s); });

    sub_emotion = sm.subscribeEmotion([this](state::EmotionState s)
                                      { this->handleEmotion(s); });

    ESP_LOGI(TAG, "DisplayManager state binding enabled");
}

// (public show* helpers removed; UI is driven via handlers)

void DisplayManager::setBatteryPercent(uint8_t p)
{
    battery_percent = p;
}

// (toast feature removed)

// ----------------------------------------------------------------------------
// Power saving mode
// ----------------------------------------------------------------------------
void DisplayManager::setPowerSaveMode(bool enable)
{
    if (enable)
    {
        anim_player->pause();
    }
    else
    {
        anim_player->resume();
    }
}

void DisplayManager::setBacklight(bool on)
{
    if (drv)
    {
        drv->setBacklight(on);
    }
}

void DisplayManager::setBrightness(uint8_t percent)
{
    if (drv)
    {
        drv->setBacklightLevel(percent);
    }
}

// ----------------------------------------------------------------------------
// Asset registration
// ----------------------------------------------------------------------------
void DisplayManager::registerEmotion(const std::string &name, const Animation1Bit &anim)
{
    emotions[name] = anim;
}

void DisplayManager::registerIcon(const std::string &name, const Icon &icon)
{
    icons[name] = icon;
}

// ----------------------------------------------------------------------------
// Update (should be called from UI task)
void DisplayManager::update(uint32_t dt_ms)
{
    if (!drv)
        return;

    // Sync dimensions from driver (in case rotation changed)
    width_ = drv->width();
    height_ = drv->height();

    // ✅ DEBUG: Log first update to verify task is running
    static bool first_update = true;
    if (first_update)
    {
        ESP_LOGI(TAG, "First update() called - display loop is working, dimensions=%dx%d", width_, height_);
        first_update = false;
    }

    // 0) If text mode active, render text only (no animation)
    if (text_active_)
    {
        // Only clear once when entering text mode
        if (!text_mode_cleared_)
        {
            drv->fillScreen(0x0000);
            text_mode_cleared_ = true;
        }
        if (!text_msg_.empty())
        {
            drv->drawTextCenter(text_msg_.c_str(), text_color_, width_ / 2, height_ / 2, text_scale_); // Center of screen
        }
        return;
    }
    else
    {
        // Reset flag when not in text mode
        text_mode_cleared_ = false;
    }

    // 1) update animation frame
    anim_player->update(dt_ms);

    // 2) Render animation frame directly to display (no framebuffer!)
    anim_player->render();

    // 3) Overlay battery percentage (top-right corner)
    // Note: Drawn over animation, cleared by next animation frame
    if (battery_percent < 255)
    {
        // Only redraw if battery percent changed
        if (battery_percent != prev_battery_percent)
        {
            // Clear old text area (assume max width of "100%" = 4 chars * 8 pixels = 32px)
            int text_x = width_ - 160;               // 40px from right edge
            drv->fillRect(text_x, 5, 40, 8, 0x0000); // Clear 40x8 black rectangle

            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", battery_percent);
            drv->drawText(buf, 0xFFFF, text_x, 5, 1); // White text, top-right

            prev_battery_percent = battery_percent;
        }
    }
}

void DisplayManager::taskEntry(void *arg)
{
    auto *self = static_cast<DisplayManager *>(arg);
    self->task_running_.store(true); // ✅ Signal task is running
    TickType_t prev = xTaskGetTickCount();

    while (self->task_running_.load())
    { // ✅ Check graceful shutdown flag
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
    switch (s)
    {
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
    switch (s)
    {
    case state::ConnectivityState::OFFLINE:
        // show text "Offline"
        playText("Offline", -1, -1, 0xFFFF, 1.5); // centered, white text
        break;

    case state::ConnectivityState::CONNECTING_WIFI:
        // Show text "Connecting WiFi..."
        playText("Connecting WiFi...", -1, -1, 0xFFFF, 1.8); // centered, white text
        break;

    case state::ConnectivityState::WIFI_PORTAL:
        // Show text "WiFi Portal Mode"
        playText("WiFi Portal Mode", -1, -1, 0xFFFF, 1.8); // centered, white text
        // playEmotion("sad");
        break;

    case state::ConnectivityState::CONNECTING_WS:
        playEmotion("stun");
        break;

    case state::ConnectivityState::ONLINE:
        // playIcon("wifi_ok");
        break;
    }
}

void DisplayManager::handleSystem(state::SystemState s)
{
    switch (s)
    {
    case state::SystemState::BOOTING:
        // ESP_LOGI(TAG, "Displaying Booting message");
        playText("PTIT", 40, 0, 0xF800, 2); // red
        break;

    case state::SystemState::RUNNING:
        // ESP_LOGI(TAG, "Displaying Running message");
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
    switch (s)
    {
    case state::PowerState::NORMAL:
        playIcon("battery");
        break;

    // case state::PowerState::LOW_BATTERY:
    //     playIcon("battery_low");
    //     break;

    case state::PowerState::CHARGING:
        playIcon("battery_charge", IconPlacement::Custom, width_ - 185, 0);
        break;

    case state::PowerState::FULL_BATTERY:
        playIcon("battery_full", IconPlacement::Custom, width_ - 185, 0);
        break;

    case state::PowerState::POWER_SAVING:
        setPowerSaveMode(true);
        break;

    case state::PowerState::CRITICAL:
        ESP_LOGI(TAG, "CRITICAL: show critical battery icon");
        // Show registered critical battery icon fullscreen
        playIcon("battery_critical", IconPlacement::Custom, 51, 22);
        // ✅ Removed blocking delay - icon stays visible via display loop
        break;

    case state::PowerState::ERROR:
        playEmotion("error");
        break;
    }
}

void DisplayManager::handleEmotion(state::EmotionState s)
{
    // Map emotion state to an available emotion asset
    switch (s)
    {
    case state::EmotionState::HAPPY:
        playEmotion("happy");
        break;
    case state::EmotionState::SAD:
        playEmotion("sad");
        break;
    case state::EmotionState::THINKING:
        playEmotion("thinking");
        break;
    case state::EmotionState::CONFUSED:
        playEmotion("stun");
        break;
    case state::EmotionState::NEUTRAL:
    case state::EmotionState::CALM:
    case state::EmotionState::EXCITED:
    case state::EmotionState::ANGRY:
    default:
        playEmotion("idle");
        break;
    }
}

// ----------------------------------------------------------------------------
// Internal asset playback
void DisplayManager::playEmotion(const std::string &name, int x, int y)
{
    auto it = emotions.find(name);
    if (it == emotions.end())
    {
        ESP_LOGW(TAG, "Emotion '%s' not found", name.c_str());
        return;
    }

    const Animation1Bit &anim = it->second;

    ESP_LOGI(TAG, "playEmotion '%s' starting animation", name.c_str());

    // Disable text mode when playing animation
    text_active_ = false;

    // Default y=22 to reserve space for battery display (top 22px)
    if (y == 0)
    {
        y = 22;
    }

    // Start animation centered or at (x,y)
    anim_player->setAnimation(anim, x, y);
}

void DisplayManager::playText(const std::string &text, int x, int y, uint16_t color, int scale)
{
    ESP_LOGI(TAG, "playText '%s' at (%d,%d) color=0x%04X scale=%d", text.c_str(), x, y, color, scale);
    if (scale < 1)
        scale = 1; // keep visible text size
    text_msg_ = text;
    text_x_ = x;
    text_y_ = y;
    text_color_ = color;
    text_scale_ = scale;
    text_active_ = true;
    // Stop animation to avoid overwriting text
    anim_player->stop();
}

void DisplayManager::clearText()
{
    text_active_ = false;
    text_mode_cleared_ = false; // Reset clear flag
    text_msg_.clear();
}

void DisplayManager::playIcon(const std::string &name,
                              IconPlacement placement,
                              int x,
                              int y)
{

    // Các icon thường (cao 22), hiển thị cùng hàng với % pin (top-right)
    auto it = icons.find(name);
    if (it == icons.end())
    {
        ESP_LOGW(TAG, "Icon '%s' not found", name.c_str());
        return;
    }
    const Icon &ico = it->second;
    int draw_x = 0;
    int draw_y = 0;

    switch (placement)
    {
    case IconPlacement::Custom:
        draw_x = x;
        draw_y = y;
        break;

    case IconPlacement::Center:
        draw_x = (width_ - ico.w) / 2;
        draw_y = (height_ - ico.h) / 2;
        break;

    case IconPlacement::TopRight:
        draw_x = width_ - ico.w - 40;
        draw_y = 0;
        break;

    case IconPlacement::Fullscreen:
        draw_x = 0;
        draw_y = 0;
        break;
    }
    if (drv)
    {
        drv->drawRLE2bitIcon(draw_x, draw_y, ico.w, ico.h, ico.rle_data);
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

    if (!drv)
        return;

    drv->fillScreen(0x0000); // OTA screen: full clear needed
    // TODO: Draw "Updating Firmware" title and progress bar outline
    // TODO: Display initial message
}

void DisplayManager::setOTAProgress(uint8_t current_percent)
{
    if (current_percent > 100)
        current_percent = 100;

    ota_progress_percent = current_percent;

    if (!drv)
        return;

    // Update progress bar on display (0-100%)
    // TODO: Draw progress bar visual
    // Example: draw bar from left to right
    // int bar_width = (width_ * current_percent) / 100;
    // drv->drawBitmap(...);  // Draw progress bar scanline

    ESP_LOGD(TAG, "OTA progress: %u%%", current_percent);
}

void DisplayManager::setOTAStatus(const std::string &status)
{
    ota_status_text = status;
    ESP_LOGI(TAG, "OTA status: %s", status.c_str());

    if (!drv)
        return;

    // TODO: Display status text on screen
    // Example: show at bottom of screen
}

void DisplayManager::showOTACompleted()
{
    ESP_LOGI(TAG, "Showing OTA completed screen");
    ota_updating = false;
    ota_completed = true;
    ota_error = false;
    ota_progress_percent = 100;
    ota_status_text = "Update completed!";

    if (!drv)
        return;

    drv->fillScreen(0x0000);
    // TODO: Draw checkmark or success animation
    // TODO: Display "Update Successful" message
}

void DisplayManager::showOTAError(const std::string &error_msg)
{
    ESP_LOGE(TAG, "Showing OTA error: %s", error_msg.c_str());
    ota_updating = false;
    ota_completed = false;
    ota_error = true;
    ota_error_msg = error_msg;
    ota_status_text = "Update failed!";

    if (!drv)
        return;

    drv->fillScreen(0x0000);
    // TODO: Draw error icon (X mark)
    // TODO: Display error message
}

void DisplayManager::showRebooting()
{
    ESP_LOGI(TAG, "Showing rebooting screen");
    ota_updating = false;
    ota_completed = true;
    ota_error = false;
    ota_status_text = "Rebooting...";

    if (!drv)
        return;

    drv->fillScreen(0x0000);
    // TODO: Draw rebooting animation or countdown
    // TODO: Display "Device restarting..." message
}
