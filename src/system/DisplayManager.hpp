#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "StateTypes.hpp"
#include "StateManager.hpp"
#include "AnimationPlayer.hpp"

// FreeRTOS (ESP32 task loop support)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// Forward declarations
class DisplayDriver;       // ST7789 low-level driver
class Framebuffer;         // Offscreen buffer
class AnimationPlayer;     // Multi-frame animation engine

// ----------------------------------------------------------------------------
// Asset descriptors
// ----------------------------------------------------------------------------



//
// DisplayManager = UI Logic layer
// ----------------------------------------------------------------------------
// - Subscribe to StateManager (optional)
// - AppController can also call UI API directly
// - Handles emotion animation, icons, toast messages, power-save mode
// - Uses DisplayDriver for actual drawing
//
class DisplayManager {
public:
   

    // Icon asset descriptor
    struct Icon {
        int w = 0;
        int h = 0;
        const uint16_t* rgb = nullptr;
    };

public:
    DisplayManager();
    ~DisplayManager();

    // Initialize with low-level display driver
    // Takes ownership of the low-level driver to ensure lifetime matches the UI
    bool init(std::unique_ptr<DisplayDriver> driver, int width = 240, int height = 240);

    // Real-time update (should be called every 20–50ms)
    void update(uint32_t dt_ms);

    // Optional internal task loop APIs (alternative to external caller)
    bool startLoop(uint32_t interval_ms = 33,
                   UBaseType_t priority = 5,
                   uint32_t stackSize = 4096,
                   BaseType_t core = tskNO_AFFINITY);
    void stopLoop();
    bool isLoopRunning() const { return task_handle_ != nullptr; }
    void setUpdateIntervalMs(uint32_t interval_ms) { update_interval_ms_ = interval_ms; }

    // Enable/Disable automatic UI updates from StateManager
    void enableStateBinding(bool enable);

    // --- High-level UI API (used by AppController) ---------------------------
    void showIdle();
    void showListening(state::InputSource src);
    void showThinking();
    void showSpeaking();
    void showError(const char* msg);

    void showLowBattery();
    void showCharging();
    void showFullBattery();
    void setBatteryPercent(uint8_t p);
    
    // Show short message on screen
    void showToast(const std::string& text, uint32_t duration_ms = 3000);

    // Power saving mode (stop animations)
    void setPowerSaveMode(bool enable);

    // --- Asset Registration ---------------------------------------------------
    void registerEmotion(const std::string& name, const Animation& anim);
    void registerIcon(const std::string& name, const Icon& icon);

private:
    // Internal handlers mapping state -> UI behavior
    void handleInteraction(state::InteractionState s, state::InputSource src);
    void handleConnectivity(state::ConnectivityState s);
    void handleSystem(state::SystemState s);
    void handlePower(state::PowerState s);

    // Internal asset playback
    void playEmotion(const std::string& name, int x = 0, int y = 0);
    void playIcon(const std::string& name, int x = 0, int y = 0);
    static void taskEntry(void* arg);
    

private:
    std::unique_ptr<DisplayDriver> drv; // owned low-level driver
    std::unique_ptr<Framebuffer> fb;
    std::unique_ptr<AnimationPlayer> anim_player;

    // asset tables
    std::unordered_map<std::string, Animation> emotions;
    std::unordered_map<std::string, Icon> icons;

    // battery
    uint8_t battery_percent = 255;

    // toast system
    std::string toast_text;
    uint32_t toast_timer = 0;
    bool toast_active = false;

    // subscriptions
    int sub_inter = -1;
    int sub_conn = -1;
    int sub_sys  = -1;
    int sub_power = -1;

    bool binding_enabled = false;

    int width_ = 240;
    int height_ = 240;

    // Task loop state
    TaskHandle_t task_handle_ = nullptr;
    uint32_t update_interval_ms_ = 33; // ~30 FPS
};
