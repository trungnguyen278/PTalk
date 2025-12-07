#pragma once

#include <cstdint>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"

#define BATTERY_INVALID 255
#define STATUS_UNKNOWN  255

class Power {
private:
    adc1_channel_t channel_;
    esp_adc_cal_characteristics_t adc_chars_;

    gpio_num_t pin_chg_;
    gpio_num_t pin_full_;

    float R1_, R2_;

    float readVoltage();
    uint8_t voltageToPercent(float v);

public:
    Power(adc1_channel_t adc_channel,
          gpio_num_t pin_chg = GPIO_NUM_NC,
          gpio_num_t pin_full = GPIO_NUM_NC,
          float R1 = 10000,
          float R2 = 20000);

    Power(adc1_channel_t adc_channel, float R1, float R2)
    : Power(adc_channel, GPIO_NUM_NC, GPIO_NUM_NC, R1, R2) {}

    
    ~Power() = default;

    // Return 0–100% valid OR BATTERY_INVALID (255)
    uint8_t getBatteryPercent();

    // Return: 1 = YES, 0 = NO, 255 = UNKNOWN
    uint8_t isCharging();
    uint8_t isFull();
};
