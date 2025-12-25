#pragma once
#include <cstdint>
#include <memory>
#include "StateTypes.hpp"
#include "StateManager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class Power;
class DisplayManager;

class PowerManager {
public:
    struct Config {
        uint32_t evaluate_interval_ms = 2000;
        // float low_battery_percent = 20.0f;
        float critical_percent = 8.0f;
        bool enable_smoothing = true;
        float smoothing_alpha = 0.15f;
    };

public:
    PowerManager(std::unique_ptr<Power> power, const Config& cfg);
    ~PowerManager();

    bool init();
    void start();
    void stop();

    // === UI Query API ===
    uint8_t getPercent() const { return last_percent; }
    float getVoltage() const { return last_voltage; }
    state::PowerState getState() const { return current_state; }

    bool isCharging() const { return ui_charging; }
    bool isFull() const { return ui_full; }
    bool isBatteryPresent() const { return battery_present; }

    // Force an immediate power evaluation (runs one sample now)
    void sampleNow();

    // Link DisplayManager for battery % updates
    void setDisplayManager(DisplayManager* display) { display_mgr = display; }

private:
    static void timerCallbackStatic(TimerHandle_t timer);
    void timerCallback();

    state::PowerState evaluateState(float volt,
                                    uint8_t percent,
                                    int chargingStatus,
                                    int fullStatus);

    void publishIfChanged(state::PowerState st);

    uint8_t applySmoothing(uint8_t percent);

private:
    std::unique_ptr<Power> power;
    Config config;
    TimerHandle_t timer = nullptr;
    bool started = false;

    // cache
    float last_voltage = 0.0f;
    uint8_t last_percent_sent = 255;
    uint8_t last_percent = 0;
    state::PowerState current_state = state::PowerState::NORMAL;

    // keep flags for UI-friendly getters
    bool ui_charging = false;
    bool ui_full = false;
    bool battery_present = true;

    bool first_sample = true;

    // Display manager link for battery % updates
    DisplayManager* display_mgr = nullptr;
};
