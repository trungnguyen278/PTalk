#pragma once

#include "AudioCodec.hpp"
#include <cstdint>

/**
 * AdpcmCodec (repurposed as PCM passthrough)
 * ----------------------------------------------------------------------------
 * Real-time safe, stateless per-frame codec that simply passes 16-bit PCM
 * through as bytes. This replaces the previous ADPCM implementation to ensure
 * exact real-time playback driven solely by the I2S clock.
 *
 * - Mono, 16 kHz expected by the rest of the pipeline
 * - Input (encode): int16_t PCM samples → little-endian byte stream
 * - Output (decode): little-endian byte stream → int16_t PCM samples
 * - Stateless: tolerant to packet gaps/jitter; no predictor/reset semantics
 */
class AdpcmCodec : public AudioCodec {
public:
    explicit AdpcmCodec(uint32_t sample_rate = 16000);
    ~AdpcmCodec() override = default;

    // PCM -> ADPCM
    size_t encode(const int16_t* pcm,
                  size_t pcm_samples,
                  uint8_t* out,
                  size_t out_capacity) override;

    // Encoded bytes (PCM LE) -> PCM samples
    size_t decode(const uint8_t* data,
                  size_t data_len,
                  int16_t* pcm_out,
                  size_t pcm_capacity) override;

    // No-op for PCM passthrough
    void reset() override;

    uint32_t sampleRate() const override;
    uint8_t  channels() const override;

private:
    uint32_t sample_rate_;
};
