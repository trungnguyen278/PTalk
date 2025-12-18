#include "I2SAudioOutput_MAX98357.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "MAX98357";

I2SAudioOutput_MAX98357::I2SAudioOutput_MAX98357(const Config& cfg)
    : cfg_(cfg)
{
    // Install I2S driver ONCE during construction - never reinstall
    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_cfg.sample_rate = cfg_.sample_rate;
    i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.dma_buf_count = 6;
    i2s_cfg.dma_buf_len = 256;
    i2s_cfg.use_apll = false;
    i2s_cfg.tx_desc_auto_clear = true;
    i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num = cfg_.pin_bck;
    pin_cfg.ws_io_num  = cfg_.pin_ws;
    pin_cfg.data_out_num = cfg_.pin_dout;
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(cfg_.i2s_port, &i2s_cfg, 0, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S driver install failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2s_set_pin(cfg_.i2s_port, &pin_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S set pin failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(cfg_.i2s_port);
        return;
    }

    // Explicit clock config for precise 16kHz timing
    err = i2s_set_clk(
        cfg_.i2s_port,
        cfg_.sample_rate,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_MONO
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S set clock failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(cfg_.i2s_port);
        return;
    }

    i2s_installed = true;
    ESP_LOGI(TAG, "I2S driver installed: %dHz, 16bit, mono, APLL=off", cfg_.sample_rate);
}

I2SAudioOutput_MAX98357::~I2SAudioOutput_MAX98357()
{
    stopPlayback();
    if (i2s_installed) {
        i2s_driver_uninstall(cfg_.i2s_port);
        i2s_installed = false;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

bool I2SAudioOutput_MAX98357::startPlayback()
{
    if (running) return true;
    if (!i2s_installed) {
        ESP_LOGE(TAG, "I2S not installed, cannot start playback");
        return false;
    }

    // Clear DMA buffer and start clock
    esp_err_t err = i2s_zero_dma_buffer(cfg_.i2s_port);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to zero DMA buffer: %s", esp_err_to_name(err));
    }

    err = i2s_start(cfg_.i2s_port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start I2S: %s", esp_err_to_name(err));
        return false;
    }

    running = true;
    ESP_LOGI(TAG, "MAX98357 playback started");
    return true;
}

void I2SAudioOutput_MAX98357::stopPlayback()
{
    if (!running) return;

    i2s_stop(cfg_.i2s_port);
    running = false;
    ESP_LOGI(TAG, "MAX98357 playback stopped");
}

// ============================================================================
// Data
// ============================================================================

size_t I2SAudioOutput_MAX98357::writePcm(const int16_t* pcm, size_t pcm_samples)
{
    if (!running || volume == 0 || !pcm || pcm_samples == 0)
        return 0;

    // CRITICAL: Buffer MUST handle full frame size from codec decode
    static int16_t scaled[1024];
    if (pcm_samples > 1024) pcm_samples = 1024;

    for (size_t i = 0; i < pcm_samples; i++) {
        scaled[i] = (pcm[i] * volume) / 100;
    }

    size_t bytes_written = 0;
    i2s_write(
        cfg_.i2s_port,
        scaled,
        pcm_samples * sizeof(int16_t),
        &bytes_written,
        portMAX_DELAY
    );

    return bytes_written / sizeof(int16_t); // trả về số sample
}


// ============================================================================
// Control
// ============================================================================

void I2SAudioOutput_MAX98357::setVolume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    volume = percent;
}

void I2SAudioOutput_MAX98357::setLowPower(bool enable)
{
    if (enable && running) {
        i2s_stop(cfg_.i2s_port);
    } else if (!enable && running) {
        i2s_start(cfg_.i2s_port);
    }
}
