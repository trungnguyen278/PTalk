#pragma once
#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/ledc.h"

struct DisplayConfig {
    int width;      // Chiều rộng vật lý (ví dụ 240)
    int height;     // Chiều cao vật lý (ví dụ 320 hoặc 240)

    // Pins
    int pin_mosi;
    int pin_sclk;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_bl;

    // SPI Configuration
    spi_host_device_t host; 
    int spi_speed_hz;       

    // PWM Configuration
    ledc_channel_t ledc_channel; 
    ledc_timer_t   ledc_timer;   

    uint16_t default_bg = 0x0000;

    // Rotation: 0, 1, 2, 3
    // 0: Portrait
    // 1: Landscape (90 độ)
    // 2: Inverted Portrait (180 độ)
    // 3: Inverted Landscape (270 độ)
    uint8_t rotation = 0; 
};