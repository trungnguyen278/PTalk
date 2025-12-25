#include "PowerManager.hpp"
#include "DisplayManager.hpp"
#include "AppController.hpp"
#include "../../lib/power/Power.hpp"        // driver của bạn
#include "esp_log.h"

static const char* TAG = "PowerManager";

PowerManager::PowerManager(std::unique_ptr<Power> power, const Config& cfg)
    : power(std::move(power)), config(cfg) {}

PowerManager::~PowerManager() {
    stop();
}

bool PowerManager::init() {
    if (!power) {
        ESP_LOGE(TAG, "PowerManager - Power driver not provided!");
        return false;
    }

    timer = xTimerCreate(
        "PowerManagerTimer",
        pdMS_TO_TICKS(config.evaluate_interval_ms),
        pdTRUE,
        this,
        &PowerManager::timerCallbackStatic
    );

    if (!timer) {
        ESP_LOGE(TAG, "Failed to create PowerManager timer");
        return false;
    }

    return true;
}

void PowerManager::start() {
    if (started) return;
    started = true;

    if (timer) {
        xTimerStart(timer, 0);
    }

    ESP_LOGI(TAG, "PowerManager started");
}

void PowerManager::stop() {
    if (!started) return;
    started = false;

    if (timer) {
        xTimerStop(timer, 0);
    }

    ESP_LOGI(TAG, "PowerManager stopped");
}

void PowerManager::timerCallbackStatic(TimerHandle_t xTimer) {
    PowerManager* self = static_cast<PowerManager*>(pvTimerGetTimerID(xTimer));
    if (self) self->timerCallback();
}

void PowerManager::sampleNow() {
    // Run one synchronous evaluation to update state immediately
    timerCallback();
}

void PowerManager::timerCallback() {
    if (!power) return;

    float voltage = power->getVoltage();
    uint8_t percent = power->getBatteryPercent();
    uint8_t charging = power->isCharging();
    uint8_t full = power->isFull();

    if (voltage < 0.0f || percent == BATTERY_INVALID) {
        battery_present = false;
        publishIfChanged(state::PowerState::ERROR);
        return;
    }

    battery_present = true;
    last_voltage = voltage;

    if (config.enable_smoothing) {
        if (first_sample) {
            last_percent = percent;
            first_sample = false;
        } else {
            float smoothed = last_percent + config.smoothing_alpha * (percent - last_percent);
            last_percent = static_cast<uint8_t>(smoothed);
        }
    } else {
        last_percent = percent;
    }

    ui_charging = (charging == 1);
    ui_full = (full == 1);

    // ✅ Percent tracking: changes published via state machine, not events
    // DisplayManager subscribes PowerState directly and reads battery from power manager
    if (last_percent_sent != last_percent) {
        last_percent_sent = last_percent;
        // Update DisplayManager with battery % (called once per change, not spam)
        if (display_mgr) {
            display_mgr->setBatteryPercent(last_percent);
        }
    }

    auto newState = evaluateState(last_voltage, last_percent, charging, full);
    publishIfChanged(newState);
}



state::PowerState PowerManager::evaluateState(float volt,
                                             uint8_t percent,
                                             int chargingStatus,
                                             int fullStatus)
{
    // FULL takes priority
    if (fullStatus == 1) {
        return state::PowerState::FULL_BATTERY;
    }

    // CHARGING takes next priority
    if (chargingStatus == 1) {
        return state::PowerState::CHARGING;
    }

    // CRITICAL battery
    if (percent <= config.critical_percent) {
        return state::PowerState::CRITICAL;
    }

    // LOW battery
    // if (percent <= config.low_battery_percent) {
    //     return state::PowerState::LOW_BATTERY;
    // }

    return state::PowerState::NORMAL;
}

void PowerManager::publishIfChanged(state::PowerState new_state) {
    if (new_state == current_state) return;

    current_state = new_state;

    ESP_LOGI(TAG, "PowerState: %d (Volt:%.2fV, %%:%u, CHG:%d, FULL:%d)",
             static_cast<int>(new_state),
             last_voltage,
             last_percent,
             ui_charging,
             ui_full);

    StateManager::instance().setPowerState(new_state);
}
