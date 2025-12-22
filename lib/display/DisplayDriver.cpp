#include "DisplayDriver.hpp"
#include "Framebuffer.hpp"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"


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
    // Free SPI bus if it was initialized
    if (initialized) {
        spi_bus_free(cfg_.spi_host);
        ESP_LOGD(TAG, "SPI bus freed");
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
    // Apply panel memory offsets if needed
    uint16_t xs = x0 + cfg_.x_offset;
    uint16_t xe = x1 + cfg_.x_offset;
    uint16_t ys = y0 + cfg_.y_offset;
    uint16_t ye = y1 + cfg_.y_offset;
    
    // Khi rotation = 2 (180°), tăng Y thêm (320 - 240) = 80px
    if (rotation_ == 2) {
        ys += (320 - 240);
        ye += (320 - 240);
    }
    
    // Khi rotation = 3 (270°), tăng X thêm (320 - 240) = 80px
    if (rotation_ == 3) {
        xs += (320 - 240);
        xe += (320 - 240);
    }

    uint8_t col_data[4] = {
        (uint8_t)(xs >> 8), (uint8_t)(xs & 0xFF),
        (uint8_t)(xe >> 8), (uint8_t)(xe & 0xFF)
    };
    uint8_t row_data[4] = {
        (uint8_t)(ys >> 8), (uint8_t)(ys & 0xFF),
        (uint8_t)(ye >> 8), (uint8_t)(ye & 0xFF)
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
    devcfg.flags = SPI_DEVICE_NO_DUMMY;  // Allow higher speeds without dummy bits

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
        // Ensure any deep sleep hold from previous run is disabled
        gpio_hold_dis((gpio_num_t)cfg.pin_bl);
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

    // Optional: invert colors for bring-up diagnostics
    // 0x21 = INVON (invert), 0x20 = INVOFF (normal)
    sendCommand(0x21);

    sendCommand(ST7789_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    initialized = true;

    ESP_LOGI(TAG, "ST7789 init OK");
    return true;
}

// ----------------------------------------------------------------------------
// Window + streaming write
// ----------------------------------------------------------------------------
void DisplayDriver::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (!initialized) return;
    setAddressWindow(x0, y0, x1, y1);
    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1);
}

void DisplayDriver::writePixels(const uint16_t* buffer, size_t len_bytes)
{
    if (!initialized || !buffer || len_bytes == 0) return;
    spi_transaction_t t = {};
    t.length = len_bytes * 8;  // bits
    t.tx_buffer = buffer;
    ESP_ERROR_CHECK(spi_device_transmit(spi_dev, &t));
}

// ----------------------------------------------------------------------------
// Backlight deep-sleep hold control
// ----------------------------------------------------------------------------
void DisplayDriver::holdBacklightDuringDeepSleep(bool enable)
{
    if (cfg_.pin_bl < 0) return;
    if (enable) {
        // Keep current BL level during deep sleep
        gpio_hold_en((gpio_num_t)cfg_.pin_bl);
        gpio_deep_sleep_hold_en();
    } else {
        gpio_hold_dis((gpio_num_t)cfg_.pin_bl);
        gpio_deep_sleep_hold_dis();
    }
}

// ----------------------------------------------------------------------------
// Flush full framebuffer to screen
// ----------------------------------------------------------------------------
void DisplayDriver::flush(Framebuffer* fb)
{
    if (!initialized || !fb) return;

    ESP_LOGD(TAG, "flush() to ST7789 %ux%u (offs %u,%u)", width_, height_, cfg_.x_offset, cfg_.y_offset);

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

    // Allocate temp buffer
    size_t buf_size = width_ * height_ * sizeof(uint16_t);
    uint16_t* fill_buf = (uint16_t*)malloc(buf_size);
    if (!fill_buf) {
        ESP_LOGE(TAG, "fillScreen: malloc failed");
        return;
    }

    // Fill entire buffer at once (not line by line)
    for (uint32_t i = 0; i < width_ * height_; i++) {
        fill_buf[i] = color;
    }

    // Send all pixels in single SPI transaction
    spi_transaction_t t = {};
    t.length = width_ * height_ * 16;  // in bits
    t.tx_buffer = fill_buf;
    t.user = nullptr;
    
    esp_err_t err = spi_device_transmit(spi_dev, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fillScreen SPI transmit failed: %d", err);
    }
    
    // Small delay to ensure display latches data
    vTaskDelay(pdMS_TO_TICKS(5));
    
    free(fill_buf);
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

void DisplayDriver::drawText(Framebuffer* fb, const char* text, uint16_t color, int x, int y, int scale)
{
    if (!fb || !text) return;

    fb->drawText8x8(x, y, text, color, scale);
}

void DisplayDriver::drawTextCenter(Framebuffer* fb, const char* text, uint16_t color, int cx, int cy, int scale)
{
    if (!fb || !text) return;

    //ESP_LOGI(TAG, "drawTextCenter called: text='%s' color=0x%04X pos=(%d,%d)", text, color, cx, cy);

    int len = strlen(text);
    if (scale < 1) scale = 1;
    int text_w = len * 8 * scale;
    int x = cx - text_w / 2;
    int y = cy - 4 * scale;

    //ESP_LOGI(TAG, "drawTextCenter: drawing at (%d,%d)", x, y);
    fb->drawText8x8(x, y, text, color, scale);
}
// Display rotation (0, 1, 2, 3 = 0°, 90°, 180°, 270°)
void DisplayDriver::setRotation(uint8_t rotation)
{
    // ST7789 MADCTL cho 4 rotation (0° và 180° đã đúng, fix 90° và 270°)
    // 0° và 180°: Portrait (MV=0)
    // 90° và 270°: Landscape (MV=1)
    
    uint8_t madctl = 0;
    uint16_t new_x_offset = 0;
    uint16_t new_y_offset = 0;
    
    rotation = rotation % 4;  // Ensure 0-3


    switch (rotation) {
        case 0:  // 0° - Portrait (GIỮ NGUYÊN)
            madctl = 0;
            new_x_offset = 0;
            new_y_offset = 0;
            break;
        case 1:  // 90° - Landscape CW (FIX: đảo offset từ Y sang X)
            madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV;
            new_x_offset = 0;  // 80 pixel offset chuyển sang X
            new_y_offset = 0;
            break;
        case 2:  // 180° - Portrait flipped (GIỮ NGUYÊN)
            madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY;
            new_x_offset = 0;
            new_y_offset = 0;
            break;
        case 3:  // 270° - Landscape CCW (FIX: đảo offset từ Y sang X)
            madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV;
            new_x_offset = 0;  // 80 pixel offset chuyển sang X
            new_y_offset = 0;
            break;
    }
    
    // Keep BGR flag if set
    madctl |= ST7789_MADCTL_BGR;
    
    sendCommand(ST7789_CMD_MADCTL);
    sendData(&madctl, 1);
    
    // Store rotation for later use in setAddressWindow
    rotation_ = rotation;
    
    // Update cfg with new offsets
    cfg_.x_offset = new_x_offset;
    cfg_.y_offset = new_y_offset;
    
    ESP_LOGI(TAG, "Display rotation set to %u (0x%02X), offset=(%u,%u)", rotation, madctl, new_x_offset, new_y_offset);
}