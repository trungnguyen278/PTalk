#pragma once

#include "AudioOutput.hpp"
#include "driver/i2s.h"

/**
 * I2SAudioOutput_MAX98357
 * ============================================================================
 * - Concrete implementation of AudioOutput
 * - DAC + Amplifier: MAX98357
 * - Input: PCM 16-bit mono/stereo
 *
 * MAX98357:
 *   - I2S TX
 *   - 16-bit / 32-bit supported
 *   - Handles amplification internally
 */
class I2SAudioOutput_MAX98357 : public AudioOutput {
public:
    struct Config {
        i2s_port_t i2s_port = I2S_NUM_1;

        int pin_bck  = -1;  // BCLK
        int pin_ws   = -1;  // LRCLK
        int pin_dout = -1;  // DIN

        uint32_t sample_rate = 16000;
        uint8_t channels     = 1;   // mono default
    };

public:
    explicit I2SAudioOutput_MAX98357(const Config& cfg);
    ~I2SAudioOutput_MAX98357() override;

    // ========================================================================
    // AudioOutput interface
    // ========================================================================
    bool startPlayback() override;
    void stopPlayback() override;

    size_t writePcm(const int16_t* pcm, size_t pcm_samples) override;

    void setVolume(uint8_t percent) override;
    void setLowPower(bool enable) override;

    uint32_t sampleRate() const override { return cfg_.sample_rate; }
    uint8_t  channels() const override   { return cfg_.channels; }
    uint8_t  bitsPerSample() const override { return 16; }

private:
    Config cfg_;

    bool running = false;
    uint8_t volume = 100; // logical volume (0–100)
};
