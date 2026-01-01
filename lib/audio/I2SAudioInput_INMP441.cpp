#include "I2SAudioInput_INMP441.hpp"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "INMP441";

I2SAudioInput_INMP441::I2SAudioInput_INMP441(const Config &cfg)
    : cfg_(cfg) {}

I2SAudioInput_INMP441::~I2SAudioInput_INMP441()
{
    stopCapture();
    i2s_driver_uninstall(cfg_.i2s_port);
}

// ============================================================================
// Lifecycle
// ============================================================================

bool I2SAudioInput_INMP441::startCapture()
{
    if (running)
        return true;

    i2s_driver_uninstall(cfg_.i2s_port); // Dọn dẹp sạch port

    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_cfg.sample_rate = cfg_.sample_rate;
    i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;

    // BẮT BUỘC: Đọc cả 2 kênh để tạo 64 xung clock cho ICS43434
    i2s_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;

    i2s_cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
    i2s_cfg.dma_buf_count = 4;
    i2s_cfg.dma_buf_len = 512;
    i2s_cfg.use_apll = false;
    i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    esp_err_t err = i2s_driver_install(cfg_.i2s_port, &i2s_cfg, 0, nullptr);
    if (err != ESP_OK)
        return false;

    i2s_pin_config_t pin_cfg = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = cfg_.pin_bck,
        .ws_io_num = cfg_.pin_ws,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = cfg_.pin_din};
    i2s_set_pin(cfg_.i2s_port, &pin_cfg);

    running = true;
    return true;
}
void I2SAudioInput_INMP441::stopCapture()
{
    if (!running)
        return;

    i2s_stop(cfg_.i2s_port);
    running = false;
    ESP_LOGI(TAG, "INMP441 capture stopped");
}

void I2SAudioInput_INMP441::pauseCapture()
{
    if (!running)
        return;
    i2s_stop(cfg_.i2s_port);
    ESP_LOGI(TAG, "INMP441 capture paused");
}

// ============================================================================
// Data
// ============================================================================

size_t I2SAudioInput_INMP441::readPcm(int16_t* pcm, size_t max_samples)
{
    if (!pcm || max_samples == 0 || !running) return 0;

    // Khai báo buffer tạm trên Stack (256 samples * 2 kênh * 4 bytes = 2KB)
    // ESP32 Task stack 4KB hoặc 8KB dư sức chứa 2KB này.
    int32_t raw_buf[max_samples * 2]; 
    size_t bytes_read = 0;

    esp_err_t res = i2s_read(cfg_.i2s_port, raw_buf, sizeof(raw_buf), &bytes_read, pdMS_TO_TICKS(20));
    if (res != ESP_OK || bytes_read == 0) return 0;

    size_t actual_samples = bytes_read / sizeof(int32_t);
    size_t pcm_idx = 0;

    // Lọc lấy kênh Trái (Index 0, 2, 4...) và dịch bit 14
    for (size_t i = 0; i < actual_samples; i += 2) {
        if (pcm_idx < max_samples) {
            pcm[pcm_idx++] = (int16_t)(raw_buf[i] >> 16);
        }
    }

    return pcm_idx; 
}

// ============================================================================
// Control
// ============================================================================

void I2SAudioInput_INMP441::setMuted(bool mute)
{
    muted = mute;
}

void I2SAudioInput_INMP441::setLowPower(bool enable)
{
    if (enable && running)
    {
        i2s_stop(cfg_.i2s_port);
    }
    else if (!enable && running)
    {
        i2s_start(cfg_.i2s_port);
    }
}
