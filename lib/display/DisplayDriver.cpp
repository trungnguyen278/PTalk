#include "DisplayDriver.hpp"
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

static const char* TAG = "DisplayDriver";

// ST7789 Commands
#define CMD_NOP       0x00
#define CMD_SWRESET   0x01
#define CMD_SLPOUT    0x11
#define CMD_DISPON    0x29
#define CMD_CASET     0x2A
#define CMD_RASET     0x2B
#define CMD_RAMWR     0x2C
#define CMD_MADCTL    0x36
#define CMD_COLMOD    0x3A

// MADCTL flags
#define MADCTL_MX 0x40
#define MADCTL_MY 0x80
#define MADCTL_MV 0x20
#define MADCTL_BGR 0x08


// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
DisplayDriver::DisplayDriver(const DisplaySpiConfig& config)
    : cfg(config), _width(config.width), _height(config.height)
{}

DisplayDriver::~DisplayDriver() {
    if (spi_dev) {
        spi_bus_remove_device(spi_dev);
    }
    // Bus deinit optional — let application decide
}


// ============================================================================
// INIT ROUTINE (public)
// ============================================================================
esp_err_t DisplayDriver::init() {
    ESP_LOGI(TAG, "Initializing DisplayDriver...");
    ESP_ERROR_CHECK(initSpiBus());
    ESP_ERROR_CHECK(initSpiDevice());

    hwReset();
    ESP_ERROR_CHECK(initPanelST7789());
    initBacklight();

    fill(0x0000);
    return ESP_OK;
}


// ============================================================================
// SPI INIT
// ============================================================================
esp_err_t DisplayDriver::initSpiBus() {
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = cfg.pin_sclk;
    buscfg.mosi_io_num = cfg.pin_mosi;
    buscfg.miso_io_num = cfg.pin_miso; 
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = cfg.width * cfg.height * 2 + 32;

    esp_err_t ret = spi_bus_initialize(cfg.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_ERR_INVALID_STATE) return ESP_OK; // Already running bus
    return ret;
}

esp_err_t DisplayDriver::initSpiDevice() {
    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = cfg.spi_speed_hz;
    devcfg.spics_io_num = cfg.pin_cs;
    devcfg.queue_size = 10;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;

    return spi_bus_add_device(cfg.spi_host, &devcfg, &spi_dev);
}


// ============================================================================
// LOW LEVEL SEND COMMAND/DATA
// ============================================================================
void DisplayDriver::sendCommand(uint8_t cmd) {
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    gpio_set_level(cfg.pin_dc, 0); // DC = Command
    spi_device_transmit(spi_dev, &t);
}

void DisplayDriver::sendData(const void* data, size_t len) {
    if (!len) return;
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    gpio_set_level(cfg.pin_dc, 1); // DC = Data
    spi_device_transmit(spi_dev, &t);
}


// ============================================================================
// PANEL RESET & INIT (ST7789)
// ============================================================================
void DisplayDriver::hwReset() {
    if (cfg.pin_rst == GPIO_NUM_NC) return;
    gpio_set_direction(cfg.pin_rst, GPIO_MODE_OUTPUT);
    gpio_set_level(cfg.pin_rst, 1);
    esp_rom_delay_us(5000);
    gpio_set_level(cfg.pin_rst, 0);
    esp_rom_delay_us(5000);
    gpio_set_level(cfg.pin_rst, 1);
    esp_rom_delay_us(5000);
}

esp_err_t DisplayDriver::initPanelST7789() {
    sendCommand(CMD_SWRESET);
    esp_rom_delay_us(150000);

    sendCommand(CMD_SLPOUT);
    esp_rom_delay_us(150000);

    // Color mode = RGB565
    sendCommand(CMD_COLMOD);
    sendData8(0x55);
    esp_rom_delay_us(10000);

    setRotation(cfg.rotation);

    sendCommand(CMD_DISPON);
    esp_rom_delay_us(50000);

    return ESP_OK;
}


// ============================================================================
// ROTATION (MADCTL)
// ============================================================================
void DisplayDriver::updateMadctl() {
    uint8_t madctl = MADCTL_BGR;

    switch (_rotation % 4) {
        case 0:
            _width = cfg.width;
            _height = cfg.height;
            madctl |= 0;
            break;
        case 1:
            _width = cfg.height;
            _height = cfg.width;
            madctl |= MADCTL_MV | MADCTL_MY;
            break;
        case 2:
            _width = cfg.width;
            _height = cfg.height;
            madctl |= MADCTL_MX | MADCTL_MY;
            break;
        case 3:
            _width = cfg.height;
            _height = cfg.width;
            madctl |= MADCTL_MV | MADCTL_MX;
            break;
    }

    sendCommand(CMD_MADCTL);
    sendData8(madctl);
}

void DisplayDriver::setRotation(uint8_t rotation) {
    _rotation = rotation % 4;
    updateMadctl();
}


// ============================================================================
// BACKLIGHT
// ============================================================================
void DisplayDriver::initBacklight() {
    if (cfg.pin_bl == GPIO_NUM_NC) return;

    ledc_timer_config_t tc = {};
    tc.speed_mode = LEDC_LOW_SPEED_MODE;
    tc.timer_num = cfg.ledc_timer;
    tc.duty_resolution = LEDC_TIMER_8_BIT;
    tc.freq_hz = 5000;
    ledc_timer_config(&tc);

    ledc_channel_config_t cc = {};
    cc.speed_mode = LEDC_LOW_SPEED_MODE;
    cc.channel = cfg.ledc_channel;
    cc.timer_sel = cfg.ledc_timer;
    cc.gpio_num = cfg.pin_bl;
    cc.duty = 0;
    ledc_channel_config(&cc);

    setBrightness(100);
}

void DisplayDriver::setBrightness(uint8_t percent) {
    if (cfg.pin_bl == GPIO_NUM_NC) return;
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, cfg.ledc_channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, cfg.ledc_channel);
}


// ============================================================================
// SET WINDOW & PUSH PIXELS (STREAMING)
// ============================================================================
void DisplayDriver::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += cfg.x_offset;
    x1 += cfg.x_offset;
    y0 += cfg.y_offset;
    y1 += cfg.y_offset;

    sendCommand(CMD_CASET);
    uint8_t data1[4] = { uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1) };
    sendData(data1, 4);

    sendCommand(CMD_RASET);
    uint8_t data2[4] = { uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1) };
    sendData(data2, 4);

    sendCommand(CMD_RAMWR);
}

void DisplayDriver::pushPixels(const uint16_t* data, size_t count) {
    sendData(data, count * sizeof(uint16_t));
}

void DisplayDriver::pushColor(uint16_t color, size_t count) {
    const size_t bufsize = 64;
    uint16_t buf[bufsize];
    for (int i = 0; i < bufsize; i++) buf[i] = color;

    while (count > 0) {
        size_t chunk = (count > bufsize) ? bufsize : count;
        pushPixels(buf, chunk);
        count -= chunk;
    }
}


// ============================================================================
// BASIC DRAW APIS
// ============================================================================
void DisplayDriver::fill(uint16_t color) {
    setWindow(0, 0, _width - 1, _height - 1);
    pushColor(color, _width * _height);
}

void DisplayDriver::drawBitmap(uint16_t x,
                               uint16_t y,
                               uint16_t w,
                               uint16_t h,
                               const uint16_t* pixels)
{
    setWindow(x, y, x + w - 1, y + h - 1);
    pushPixels(pixels, w * h);
}

void DisplayDriver::drawScanline(uint16_t y,
                                 const uint16_t* pixels,
                                 uint16_t width)
{
    setWindow(0, y, width - 1, y);
    pushPixels(pixels, width);
}
