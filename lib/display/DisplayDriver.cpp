#include "DisplayDriver.hpp"
#include "Font8x8.hpp"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "DisplayDriver";

#define ST7789_CMD_SWRESET 0x01
#define ST7789_CMD_SLPOUT 0x11
#define ST7789_CMD_COLMOD 0x3A
#define ST7789_CMD_MADCTL 0x36
#define ST7789_CMD_CASET 0x2A
#define ST7789_CMD_RASET 0x2B
#define ST7789_CMD_RAMWR 0x2C
#define ST7789_CMD_DISPON 0x29

// MADCTL bits
#define ST7789_MADCTL_MY 0x80
#define ST7789_MADCTL_MX 0x40
#define ST7789_MADCTL_MV 0x20
#define ST7789_MADCTL_ML 0x10
#define ST7789_MADCTL_BGR 0x08
#define ST7789_MADCTL_MH 0x04

// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------

DisplayDriver::DisplayDriver() = default;

DisplayDriver::~DisplayDriver()
{
    if (spi_dev)
    {
        spi_bus_remove_device(spi_dev);
        spi_dev = nullptr;
    }
    // Free SPI bus if it was initialized
    if (initialized)
    {
        spi_bus_free(cfg_.spi_host);
        ESP_LOGD(TAG, "SPI bus freed");
    }
}

// ----------------------------------------------------------------------------
// Low-level SPI helpers
// ----------------------------------------------------------------------------

void DisplayDriver::sendCommand(uint8_t cmd)
{
    gpio_set_level((gpio_num_t)cfg_.pin_dc, 0); // DC = 0 (command)

    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_transmit(spi_dev, &t);
}

void DisplayDriver::sendData(const uint8_t *data, size_t len)
{
    if (!len)
        return;

    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1); // DC = 1 (data)

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_transmit(spi_dev, &t);
}

void DisplayDriver::setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Apply panel memory offsets if needed (should be 0 for 240x320 display)
    uint16_t xs = x0 + cfg_.x_offset;
    uint16_t xe = x1 + cfg_.x_offset;
    uint16_t ys = y0 + cfg_.y_offset;
    uint16_t ye = y1 + cfg_.y_offset;

    uint8_t col_data[4] = {
        (uint8_t)(xs >> 8), (uint8_t)(xs & 0xFF),
        (uint8_t)(xe >> 8), (uint8_t)(xe & 0xFF)};
    uint8_t row_data[4] = {
        (uint8_t)(ys >> 8), (uint8_t)(ys & 0xFF),
        (uint8_t)(ye >> 8), (uint8_t)(ye & 0xFF)};

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
    if (cfg_.pin_bl < 0)
        return;

    // If PWM is set up, drive duty; otherwise fall back to GPIO level
    if (bl_pwm_ready_)
    {
        const uint32_t duty_max = (1u << LEDC_TIMER_13_BIT) - 1;
        uint32_t duty = on ? (bl_level_percent_ * duty_max) / 100 : 0;
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    }
    else
    {
        gpio_set_direction((gpio_num_t)cfg_.pin_bl, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cfg_.pin_bl, on ? 1 : 0);
    }
}

void DisplayDriver::setBacklightLevel(uint8_t percent)
{
    bl_level_percent_ = (percent > 100) ? 100 : percent;

    if (cfg_.pin_bl < 0)
        return;

    if (!bl_pwm_ready_)
    {
        initBacklightPwm();
    }

    if (bl_pwm_ready_)
    {
        const uint32_t duty_max = (1u << LEDC_TIMER_13_BIT) - 1;
        uint32_t duty = (bl_level_percent_ * duty_max) / 100;
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    }
    else
    {
        // Fallback to simple on/off if PWM setup fails
        gpio_set_direction((gpio_num_t)cfg_.pin_bl, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cfg_.pin_bl, bl_level_percent_ > 0 ? 1 : 0);
    }
}

// ----------------------------------------------------------------------------
// Init sequence
// ----------------------------------------------------------------------------

bool DisplayDriver::init(const Config &cfg)
{
    cfg_ = cfg;
    width_ = cfg.width;
    height_ = cfg.height;

    ESP_LOGI(TAG, "Init ST7789 %dx%d", width_, height_);

    // 1. Init SPI bus
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = cfg.pin_mosi;
    buscfg.miso_io_num = -1; // not used
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
    devcfg.flags = SPI_DEVICE_NO_DUMMY; // Allow higher speeds without dummy bits

    ESP_ERROR_CHECK(spi_bus_add_device(cfg.spi_host, &devcfg, &spi_dev));

    // 3. Init GPIO (DC, RST, BL)
    if (cfg.pin_dc >= 0)
    {
        gpio_set_direction((gpio_num_t)cfg.pin_dc, GPIO_MODE_OUTPUT);
    }
    if (cfg.pin_rst >= 0)
    {
        gpio_set_direction((gpio_num_t)cfg.pin_rst, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cfg.pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level((gpio_num_t)cfg.pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (cfg.pin_bl >= 0)
    {
        gpio_set_direction((gpio_num_t)cfg.pin_bl, GPIO_MODE_OUTPUT);
        // Ensure any deep sleep hold from previous run is disabled
        gpio_hold_dis((gpio_num_t)cfg.pin_bl);
        gpio_set_level((gpio_num_t)cfg.pin_bl, 0);
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

    // Clear screen to black to avoid pixel noise
    fillScreen(0x0000);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Screen cleared");
    // Turn on backlight
    setBacklight(true);
    // Prepare PWM (lazy start if desired)
    initBacklightPwm();

    return true;
}

// ----------------------------------------------------------------------------
// Window + streaming write
// ----------------------------------------------------------------------------
void DisplayDriver::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (!initialized)
        return;
    setAddressWindow(x0, y0, x1, y1);
    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1);
}

void DisplayDriver::writePixels(const uint16_t *buffer, size_t len_bytes)
{
    if (!initialized || !buffer || len_bytes == 0)
        return;
    spi_transaction_t t = {};
    t.length = len_bytes * 8; // bits
    t.tx_buffer = buffer;
    ESP_ERROR_CHECK(spi_device_transmit(spi_dev, &t));
}

// ----------------------------------------------------------------------------
// Backlight deep-sleep hold control
// ----------------------------------------------------------------------------
void DisplayDriver::holdBacklightDuringDeepSleep(bool enable)
{
    if (cfg_.pin_bl < 0)
        return;
    if (enable)
    {
        // Keep current BL level during deep sleep
        gpio_hold_en((gpio_num_t)cfg_.pin_bl);
        gpio_deep_sleep_hold_en();
    }
    else
    {
        gpio_hold_dis((gpio_num_t)cfg_.pin_bl);
        gpio_deep_sleep_hold_dis();
    }
}

// ----------------------------------------------------------------------------
// Drawing primitives
// ----------------------------------------------------------------------------

void DisplayDriver::fillScreen(uint16_t color)
{
    if (!initialized)
        return;

    setAddressWindow(0, 0, width_ - 1, height_ - 1);
    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1); // data

    // Allocate line buffer (much smaller than full screen)
    uint16_t *line_buf = (uint16_t *)malloc(width_ * sizeof(uint16_t));
    if (!line_buf)
    {
        ESP_LOGE(TAG, "fillScreen: malloc failed for line buffer (%d bytes)", width_ * 2);
        return;
    }

    // Fill line buffer with color
    for (int i = 0; i < width_; i++)
    {
        line_buf[i] = color;
    }

    // Send line by line
    spi_transaction_t t = {};
    t.length = width_ * 16; // bits per line
    t.tx_buffer = line_buf;

    for (int row = 0; row < height_; row++)
    {
        esp_err_t err = spi_device_transmit(spi_dev, &t);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "fillScreen SPI transmit failed at row %d: %d", row, err);
            break;
        }
    }

    free(line_buf);
}

void DisplayDriver::fillRect(int x, int y, int w, int h, uint16_t color)
{
    if (!initialized)
        return;
    if (w <= 0 || h <= 0)
        return;
    if (x >= width_ || y >= height_)
        return;

    // Clip to screen bounds
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > width_)
        w = width_ - x;
    if (y + h > height_)
        h = height_ - y;
    if (w <= 0 || h <= 0)
        return;

    setAddressWindow(x, y, x + w - 1, y + h - 1);
    gpio_set_level((gpio_num_t)cfg_.pin_dc, 1);

    // Allocate line buffer
    uint16_t *line_buf = (uint16_t *)malloc(w * sizeof(uint16_t));
    if (!line_buf)
    {
        ESP_LOGE(TAG, "fillRect: malloc failed");
        return;
    }

    // Fill line buffer
    for (int i = 0; i < w; i++)
    {
        line_buf[i] = color;
    }

    // Send line by line
    spi_transaction_t t = {};
    t.length = w * 16; // bits
    t.tx_buffer = line_buf;

    for (int row = 0; row < h; row++)
    {
        spi_device_transmit(spi_dev, &t);
    }

    free(line_buf);
}

void DisplayDriver::drawPixel(int x, int y, uint16_t color)
{
    if (!initialized)
        return;
    if (x < 0 || x >= width_ || y < 0 || y >= height_)
        return;

    setAddressWindow(x, y, x, y);
    sendData((uint8_t *)&color, 2);
}

void DisplayDriver::drawBitmap(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!initialized || !pixels)
        return;
    if (w <= 0 || h <= 0)
        return;

    setAddressWindow(x, y, x + w - 1, y + h - 1);
    sendData((uint8_t *)pixels, w * h * 2);
}

// ----------------------------------------------------------------------------
// Text rendering (direct to display)
// ----------------------------------------------------------------------------

void DisplayDriver::drawText(const char *text, uint16_t color, int x, int y, int scale)
{
    if (!initialized || !text)
        return;

    int cx = x;
    while (*text)
    {
        char c = *text++;
        if (c < 32 || c > 126)
            continue;

        const uint8_t *glyph = FONT8x8[c - 32];

        // Draw character pixel by pixel
        for (int row = 0; row < 8; row++)
        {
            uint8_t line = glyph[row];
            for (int col = 0; col < 8; col++)
            {
                if (line & (0x80 >> col))
                {
                    // Draw scaled pixel
                    for (int sy = 0; sy < scale; sy++)
                    {
                        for (int sx = 0; sx < scale; sx++)
                        {
                            drawPixel(cx + col * scale + sx, y + row * scale + sy, color);
                        }
                    }
                }
            }
        }

        cx += 8 * scale;
    }
}

void DisplayDriver::drawTextCenter(const char *text, uint16_t color, int cx, int cy, int scale)
{
    if (!text)
        return;

    int len = strlen(text);
    if (scale < 1)
        scale = 1;
    int text_w = len * 8 * scale;
    int x = cx - text_w / 2;
    int y = cy - 4 * scale;

    drawText(text, color, x, y, scale);
}

// Display rotation (0, 1, 2, 3 = 0°, 90°, 180°, 270°)
void DisplayDriver::setRotation(uint8_t rotation)
{
    rotation %= 4;

    uint8_t madctl = 0;
    uint16_t new_x_offset = 0;
    uint16_t new_y_offset = 0;

    bool was_landscape = (rotation_ == 1 || rotation_ == 3);
    bool is_landscape = (rotation == 1 || rotation == 3);

    switch (rotation)
    {
    case 0:
        madctl = 0;
        break;
    case 1: // 90°
        madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV;
        break;
    case 2:
        madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY;
        break;
    case 3: // 270°
        madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV;
        break;
    }

    madctl |= ST7789_MADCTL_BGR;

    sendCommand(ST7789_CMD_MADCTL);
    sendData(&madctl, 1);

    // ✅ swap DIMENSION TRƯỚC khi update rotation_
    if (was_landscape != is_landscape)
    {
        uint16_t tmp = width_;
        width_ = height_;
        height_ = tmp;
        ESP_LOGI(TAG, "Rotation swap -> %ux%u", width_, height_);
    }

    rotation_ = rotation;

    cfg_.x_offset = new_x_offset;
    cfg_.y_offset = new_y_offset;

    ESP_LOGI(TAG,
             "Rotation=%u madctl=0x%02X size=%ux%u",
             rotation_, madctl, width_, height_);
}

void DisplayDriver::initBacklightPwm()
{
    if (cfg_.pin_bl < 0)
    {
        bl_pwm_ready_ = false;
        return;
    }

    // Configure a simple LEDC channel for backlight dimming
    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode = LEDC_HIGH_SPEED_MODE;
    tcfg.timer_num = LEDC_TIMER_0;
    tcfg.duty_resolution = LEDC_TIMER_13_BIT;
    tcfg.freq_hz = 5000; // 5 kHz
    tcfg.clk_cfg = LEDC_AUTO_CLK;

    if (ledc_timer_config(&tcfg) != ESP_OK)
    {
        ESP_LOGW(TAG, "Backlight: timer config failed, fallback to GPIO");
        bl_pwm_ready_ = false;
        return;
    }

    ledc_channel_config_t ccfg = {};
    ccfg.speed_mode = LEDC_HIGH_SPEED_MODE;
    ccfg.channel = LEDC_CHANNEL_0;
    ccfg.timer_sel = LEDC_TIMER_0;
    ccfg.intr_type = LEDC_INTR_DISABLE;
    ccfg.gpio_num = cfg_.pin_bl;
    ccfg.duty = (1u << LEDC_TIMER_13_BIT) - 1; // start full
    ccfg.hpoint = 0;

    if (ledc_channel_config(&ccfg) != ESP_OK)
    {
        ESP_LOGW(TAG, "Backlight: channel config failed, fallback to GPIO");
        bl_pwm_ready_ = false;
        return;
    }

    bl_pwm_ready_ = true;
}