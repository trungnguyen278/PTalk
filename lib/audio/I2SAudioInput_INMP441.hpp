#pragma once

#include "AudioInput.hpp"
#include "driver/i2s.h"

/**
 * I2SAudioInput_INMP441
 * ============================================================================
 * - Concrete implementation of AudioInput
 * - Mic: INMP441 (I2S digital microphone)
 * - Output: PCM 16-bit mono
 *
 * INMP441:
 *   - I2S RX only
 *   - 24-bit data (usually trimmed to 16-bit)
 *   - Mono (L or R selectable)
 */
class I2SAudioInput_INMP441 : public AudioInput {
public:
    struct Config {
        i2s_port_t i2s_port = I2S_NUM_0;

        int pin_bck = -1;   // SCK
        int pin_ws  = -1;   // WS / LRCLK
        int pin_din = -1;   // SD

        uint32_t sample_rate = 16000;
        bool use_left_channel = true; // INMP441 L/R select
    };

public:
    explicit I2SAudioInput_INMP441(const Config& cfg);
    ~I2SAudioInput_INMP441() override;

    // ========================================================================
    // AudioInput interface
    // ========================================================================
    bool startCapture() override;
    void stopCapture() override;
    void pauseCapture() override;

    size_t readPcm(int16_t* pcm, size_t max_samples) override;

    void setMuted(bool mute) override;
    void setLowPower(bool enable) override;

    uint32_t sampleRate() const override { return cfg_.sample_rate; }
    uint8_t  channels() const override   { return 1; }
    uint8_t  bitsPerSample() const override { return 16; }

private:
    Config cfg_;

    bool running = false;
    bool muted   = false;
};
