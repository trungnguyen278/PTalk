#pragma once
#include <cstdint>
#include <functional>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/**
 * TouchInput
 * ------------------------------------------------
 * - Đọc nút nhấn / touch
 * - Debounce
 * - Detect: press / release / long-press
 * - KHÔNG set state
 * - KHÔNG gọi AppController trực tiếp
 */
class TouchInput {
public:
    enum class Event : uint8_t {
        PRESS,
        RELEASE,
        LONG_PRESS
    };

    struct Config {
        gpio_num_t pin;
        bool active_low = true;
        uint32_t long_press_ms = 1500;
        uint32_t debounce_ms   = 30;
    };

public:
    TouchInput() = default;
    ~TouchInput();

    bool init(const Config& cfg);
    void start();
    void stop();

    // AppController đăng ký callback
    void onEvent(std::function<void(Event)> cb);

private:
    static void taskEntry(void* arg);
    void loop();

    bool readRaw() const;

private:
    Config cfg_{};
    std::function<void(Event)> cb_;

    TaskHandle_t task_ = nullptr;
    std::atomic<bool> running {false};

    // debounce & timing
    bool last_state = false;
    TickType_t press_tick = 0;
};
