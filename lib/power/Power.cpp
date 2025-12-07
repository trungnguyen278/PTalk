#include "Power.hpp"
#include <cmath>

Power::Power(adc1_channel_t adc_channel, gpio_num_t pin_chg, gpio_num_t pin_full, float R1, float R2)
    : channel_(adc_channel), pin_chg_(pin_chg), pin_full_(pin_full), R1_(R1), R2_(R2)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel_, ADC_ATTEN_DB_11);

    esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100,
        &adc_chars_
    );

    // Configure CHRG pin
    if(pin_chg_ != GPIO_NUM_NC) {
        gpio_config_t io = {};       
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pin_bit_mask = (1ULL << pin_chg_);
        gpio_config(&io);
    }

    // Configure FULL pin
    if(pin_full_ != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pin_bit_mask = (1ULL << pin_full_);
        gpio_config(&io);
    }
}

// Read VBAT real voltage, return -1 if disconnected/floating
float Power::readVoltage() {
    uint32_t raw = adc1_get_raw(channel_);
    uint32_t mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars_);

    // If mv < 40mV -> impossible for real battery => floating/disconnected
    if (mv < 40) {
        return -1.0f;
    }

    float vadc = mv / 1000.0f;
    float vbat = vadc * ((R1_ + R2_) / R2_);
    return vbat;
}

uint8_t Power::voltageToPercent(float v) {
    struct { float v; uint8_t p; } tbl[] = {
        {3.00, 0}, {3.30, 10}, {3.50, 25}, {3.70, 50},
        {3.90, 75}, {4.10, 90}, {4.20, 100}
    };

    uint8_t raw_percent = 0;

    if (v <= tbl[0].v) raw_percent = 0;
    else if (v >= tbl[6].v) raw_percent = 100;
    else {
        for (int i = 0; i < 6; i++) {
            if (v >= tbl[i].v && v < tbl[i + 1].v) {
                float r = (v - tbl[i].v) / (tbl[i + 1].v - tbl[i].v);
                raw_percent = tbl[i].p + r * (tbl[i + 1].p - tbl[i].p);
                break;
            }
        }
    }

    // Hysteresis: only update when difference >= 5%
    static int last_percent = -1;

    if (last_percent < 0) {
        last_percent = raw_percent; // first assignment
    }
    else if (std::abs((int)raw_percent - last_percent) >= 5) {
        last_percent = raw_percent;
    }

    // Round DOWN to nearest 5%
    int rounded = (last_percent / 5) * 5;
    if (rounded > 100) rounded = 100;

    return static_cast<uint8_t>(rounded);
}

uint8_t Power::getBatteryPercent() {
    float v = readVoltage();
    if (v < 0.0f) {
        return BATTERY_INVALID;
    }
    return voltageToPercent(v);
}

// TP4056: CHRG LOW = charging
uint8_t Power::isCharging() {
    if(pin_chg_ == GPIO_NUM_NC)
        return STATUS_UNKNOWN;

    int level = gpio_get_level(pin_chg_);
    if(level == 0)
        return 1;

    return STATUS_UNKNOWN;
}

// TP4056: FULL LOW = battery full
uint8_t Power::isFull() {
    if(pin_full_ == GPIO_NUM_NC)
        return STATUS_UNKNOWN;

    int level = gpio_get_level(pin_full_);
    if(level == 0)
        return 1;

    return STATUS_UNKNOWN;
}
