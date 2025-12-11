#pragma once
#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/ledc.h"

// ============================================================================
//  DisplaySpiConfig: cấu hình phần cứng, dùng lại cho dự án khác
// ============================================================================
struct DisplaySpiConfig {
    // Kích thước logic của panel (sau này có thể đổi theo rotation)
    uint16_t width  = 240;
    uint16_t height = 240;

    // Chân SPI
    gpio_num_t pin_sclk = GPIO_NUM_NC;
    gpio_num_t pin_mosi = GPIO_NUM_NC;
    gpio_num_t pin_miso = GPIO_NUM_NC;   // thường không dùng, để NC

    // Chân điều khiển LCD
    gpio_num_t pin_cs   = GPIO_NUM_NC;
    gpio_num_t pin_dc   = GPIO_NUM_NC;
    gpio_num_t pin_rst  = GPIO_NUM_NC;
    gpio_num_t pin_bl   = GPIO_NUM_NC;   // backlight (PWM) - optional

    // SPI host + speed
    spi_host_device_t spi_host   = SPI2_HOST;
    int                spi_speed_hz = 40 * 1000 * 1000; // 40MHz default

    // Backlight PWM config (nếu dùng)
    ledc_channel_t ledc_channel = LEDC_CHANNEL_0;
    ledc_timer_t   ledc_timer   = LEDC_TIMER_0;

    // Rotation: 0..3 (panel ST7789 sẽ map lại sau)
    uint8_t rotation = 0;

    // Nếu panel có RAM lớn hơn vùng hiển thị (ví dụ 240x320 nhưng chỉ dùng 240x240)
    // có thể cấu hình offset nếu cần
    uint16_t x_offset = 0;
    uint16_t y_offset = 0;
};