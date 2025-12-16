#include "TouchInput.hpp"
#include "esp_log.h"

static const char* TAG = "TouchInput";

bool TouchInput::init(const Config& cfg)
{
    cfg_ = cfg;

    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << cfg_.pin;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en   = cfg_.active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io.pull_down_en = cfg_.active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    io.intr_type = GPIO_INTR_DISABLE;

    ESP_ERROR_CHECK(gpio_config(&io));

    last_state = readRaw();
    return true;
}

TouchInput::~TouchInput() {
    stop();
}

void TouchInput::start()
{
    if (running) return;
    running = true;

    xTaskCreatePinnedToCore(
        taskEntry,
        "TouchInput",
        2048,
        this,
        3,
        &task_,
        0
    );

    ESP_LOGI(TAG, "TouchInput started");
}

void TouchInput::stop()
{
    running = false;
}

void TouchInput::onEvent(std::function<void(Event)> cb)
{
    cb_ = cb;
}

bool TouchInput::readRaw() const
{
    bool level = gpio_get_level(cfg_.pin);
    return cfg_.active_low ? !level : level;
}

void TouchInput::taskEntry(void* arg)
{
    static_cast<TouchInput*>(arg)->loop();
}

void TouchInput::loop()
{
    while (running) {
        bool state = readRaw();

        if (state != last_state) {
            vTaskDelay(pdMS_TO_TICKS(cfg_.debounce_ms));
            state = readRaw();

            if (state != last_state) {
                last_state = state;

                if (state) {
                    press_tick = xTaskGetTickCount();
                    if (cb_) cb_(Event::PRESS);
                } else {
                    TickType_t held =
                        xTaskGetTickCount() - press_tick;

                    if (held * portTICK_PERIOD_MS >= cfg_.long_press_ms) {
                        if (cb_) cb_(Event::LONG_PRESS);
                    } else {
                        if (cb_) cb_(Event::RELEASE);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "TouchInput stopped");
    vTaskDelete(nullptr);
}
