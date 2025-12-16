#include "I2SAudioInput_INMP441.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "INMP441";

I2SAudioInput_INMP441::I2SAudioInput_INMP441(const Config& cfg)
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
    if (running) return true;

    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_cfg.sample_rate = cfg_.sample_rate;
    i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    i2s_cfg.channel_format = cfg_.use_left_channel
        ? I2S_CHANNEL_FMT_ONLY_LEFT
        : I2S_CHANNEL_FMT_ONLY_RIGHT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.dma_buf_count = 4;
    i2s_cfg.dma_buf_len = 256;
    i2s_cfg.use_apll = false;
    i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num = cfg_.pin_bck;
    pin_cfg.ws_io_num  = cfg_.pin_ws;
    pin_cfg.data_in_num = cfg_.pin_din;
    pin_cfg.data_out_num = I2S_PIN_NO_CHANGE;

    ESP_ERROR_CHECK(i2s_driver_install(cfg_.i2s_port, &i2s_cfg, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(cfg_.i2s_port, &pin_cfg));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(cfg_.i2s_port));

    running = true;
    ESP_LOGI(TAG, "INMP441 capture started");
    return true;
}

void I2SAudioInput_INMP441::stopCapture()
{
    if (!running) return;

    i2s_stop(cfg_.i2s_port);
    running = false;
    ESP_LOGI(TAG, "INMP441 capture stopped");
}

void I2SAudioInput_INMP441::pauseCapture()
{
    if (!running) return;
    i2s_stop(cfg_.i2s_port);
    ESP_LOGI(TAG, "INMP441 capture paused");
}

// ============================================================================
// Data
// ============================================================================

size_t I2SAudioInput_INMP441::readPcm(int16_t* pcm, size_t max_samples)
{
    if (!pcm || max_samples == 0) return 0;

    if (!running || muted) {
        memset(pcm, 0, max_samples * sizeof(int16_t));
        return max_samples;
    }

    // INMP441 xuất 24-bit left-justified trong 32-bit
    static int32_t i2s_raw[128];

    size_t bytes_read = 0;
    ESP_ERROR_CHECK(
        i2s_read(
            cfg_.i2s_port,
            i2s_raw,
            sizeof(i2s_raw),
            &bytes_read,
            portMAX_DELAY
        )
    );

    size_t in_samples = bytes_read / sizeof(int32_t);
    if (in_samples > max_samples)
        in_samples = max_samples;

    for (size_t i = 0; i < in_samples; i++) {
        // 24-bit → 16-bit
        pcm[i] = static_cast<int16_t>(i2s_raw[i] >> 14);
    }

    return in_samples; // ✅ TRẢ VỀ SAMPLE
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
    if (enable && running) {
        i2s_stop(cfg_.i2s_port);
    } else if (!enable && running) {
        i2s_start(cfg_.i2s_port);
    }
}
