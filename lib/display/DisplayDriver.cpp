#include "DisplayDriver.hpp"
#include "Framebuffer.hpp"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char* TAG = "DisplayDriver";

#define ST7789_CMD_SWRESET   0x01
#define ST7789_CMD_SLPOUT    0x11
#define ST7789_CMD_COLMOD    0x3A
#define ST7789_CMD_MADCTL    0x36
#define ST7789_CMD_CASET     0x2A
#define ST7789_CMD_RASET     0x2B
#define ST7789_CMD_RAMWR     0x2C
#define ST7789_CMD_DISPON    0x29

// MADCTL bits
#define ST7789_MADCTL_MY  0x80
#define ST7789_MADCTL_MX  0x40
#define ST7789_MADCTL_MV  0x20
#define ST7789_MADCTL_ML  0x10
#define ST7789_MADCTL_BGR 0x08
#define ST7789_MADCTL_MH  0x04


// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------

DisplayDriver::DisplayDriver() = default;

DisplayDriver::~DisplayDriver() {
    if (spi_dev) {
        spi_bus_remove_device(spi_dev);
        spi_dev = nullptr;
    }
}


// ----------------------------------------------------------------------------
// Low-level SPI helpers
// ----------------------------------------------------------------------------

void DisplayDriver::sendCommand(uint8_t cmd)
{
    gpio_set_level((gpio_num_t)cfg_.pin_dc, 0);   // DC = 0 (command)

    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_transmit(spi_dev, &t);
}

void DisplayDriver::sendData(const uint8_t* data, size_t len)
{
    if (!len) return;

    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1);   // DC = 1 (data)

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_transmit(spi_dev, &t);
}

void DisplayDriver::setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t col_data[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    uint8_t row_data[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
    };

    sendCommand(ST7789_CMD_CASET);
    sendData(col_data, 4);

    sendCommand(ST7789_CMD_RASET);
    sendData(row_data, 4);

    sendCommand(ST7789_CMD_RAMWR);
}


// ----------------------------------------------------------------------------
// Backlight control
// ----------------------------------------------------------------------------

void DisplayDriver::setBacklight(bool on)
{
    if (cfg_.pin_bl >= 0) {
        gpio_set_direction((gpio_num_t)cfg_.pin_bl, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cfg_.pin_bl, on ? 1 : 0);
    }
}


// ----------------------------------------------------------------------------
// Init sequence
// ----------------------------------------------------------------------------

bool DisplayDriver::init(const Config& cfg)
{
    cfg_ = cfg;
    width_ = cfg.width;
    height_ = cfg.height;

    ESP_LOGI(TAG, "Init ST7789 %dx%d", width_, height_);

    // 1. Init SPI bus
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = cfg.pin_mosi;
    buscfg.miso_io_num = -1;  // not used
    buscfg.sclk_io_num = cfg.pin_sclk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = width_ * height_ * 2;

    ESP_ERROR_CHECK(spi_bus_initialize(cfg.spi_host, &buscfg, cfg.dma_chan));

    // 2. Add device
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = cfg.spi_speed_hz;
    devcfg.mode = 0;
    devcfg.spics_io_num = cfg.pin_cs;
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_add_device(cfg.spi_host, &devcfg, &spi_dev));

    // 3. Init GPIO (DC, RST, BL)
    if (cfg.pin_dc >= 0) {
        gpio_set_direction((gpio_num_t)cfg.pin_dc, GPIO_MODE_OUTPUT);
    }
    if (cfg.pin_rst >= 0) {
        gpio_set_direction((gpio_num_t)cfg.pin_rst, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cfg.pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level((gpio_num_t)cfg.pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (cfg.pin_bl >= 0) {
        gpio_set_direction((gpio_num_t)cfg.pin_bl, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cfg.pin_bl, 1);
    }

    // 4. Send ST7789 init commands
    sendCommand(ST7789_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    sendCommand(ST7789_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t colmod = 0x55; // RGB565
    sendCommand(ST7789_CMD_COLMOD);
    sendData(&colmod, 1);

    uint8_t madctl =
        ST7789_MADCTL_MX |
        ST7789_MADCTL_MY |
        ST7789_MADCTL_BGR;

    sendCommand(ST7789_CMD_MADCTL);
    sendData(&madctl, 1);

    sendCommand(ST7789_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    initialized = true;

    ESP_LOGI(TAG, "ST7789 init OK");
    return true;
}

// ----------------------------------------------------------------------------
// Flush full framebuffer to screen
// ----------------------------------------------------------------------------
void DisplayDriver::flush(Framebuffer* fb)
{
    if (!initialized || !fb) return;

    setAddressWindow(0, 0, width_ - 1, height_ - 1);

    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1);

    spi_transaction_t t = {};
    t.length = width_ * height_ * 16;
    t.tx_buffer = fb->data();   // thêm getter: return pixels_;
    ESP_ERROR_CHECK(spi_device_transmit(spi_dev, &t));
}



// ----------------------------------------------------------------------------
// Drawing primitives
// ----------------------------------------------------------------------------

void DisplayDriver::fillScreen(uint16_t color)
{
    if (!initialized) return;

    setAddressWindow(0, 0, width_ - 1, height_ - 1);

    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1); // data

    spi_transaction_t t = {};
    t.length = 16 * 1024 * 8;
    t.tx_buffer = nullptr;

    uint16_t line_buf[240];
    for (int i = 0; i < 240; i++) line_buf[i] = color;

    for (uint32_t i = 0; i < height_; i++) {
        t.tx_buffer = line_buf;
        ESP_ERROR_CHECK(spi_device_transmit(spi_dev, &t));
    }
}


void DisplayDriver::drawPixel(int x, int y, uint16_t color)
{
    if (!initialized) return;
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

    setAddressWindow(x, y, x, y);
    sendData((uint8_t*)&color, 2);
}


void DisplayDriver::drawBitmap(int x, int y, int w, int h, const uint16_t* pixels)
{
    if (!initialized || !pixels) return;
    if (w <= 0 || h <= 0) return;

    setAddressWindow(x, y, x + w - 1, y + h - 1);
    sendData((uint8_t*)pixels, w * h * 2);
}


// ----------------------------------------------------------------------------
// Text rendering (very simple bitmap font)
// ----------------------------------------------------------------------------

void DisplayDriver::drawText(Framebuffer* fb, const char* text, uint16_t color, int x, int y)
{
    if (!fb || !text) return;

    fb->drawText8x8(x, y, text, color);
}

void DisplayDriver::drawTextCenter(Framebuffer* fb, const char* text, uint16_t color, int cx, int cy)
{
    if (!fb || !text) return;

    int len = strlen(text);
    int text_w = len * 8;
    int x = cx - text_w / 2;
    int y = cy - 4;

    fb->drawText8x8(x, y, text, color);
}
