#pragma once
#include <cstdint>
#include <cstddef>

/**
 * AudioCodec
 * ----------------------------------------------------------------------------
 * Interface chung cho mọi codec (ADPCM, Opus, PCM passthrough...)
 *
 * - Input PCM: int16_t, mono
 * - Output: byte stream (uint8_t)
 *
 * AudioManager chỉ làm việc với interface này
 */
class AudioCodec {
public:
    virtual ~AudioCodec() = default;

    // =====================================================================
    // Encode: PCM -> compressed
    // =====================================================================
    virtual size_t encode(const int16_t* pcm,
                          size_t pcm_samples,
                          uint8_t* out,
                          size_t out_capacity) = 0;

    // =====================================================================
    // Decode: compressed -> PCM
    // =====================================================================
    virtual size_t decode(const uint8_t* data,
                          size_t data_len,
                          int16_t* pcm_out,
                          size_t pcm_capacity) = 0;

    // =====================================================================
    // Reset internal state (VERY IMPORTANT for ADPCM / Opus)
    // =====================================================================
    virtual void reset() = 0;

    // =====================================================================
    // Properties (optional but useful)
    // =====================================================================
    virtual uint32_t sampleRate() const = 0;
    virtual uint8_t channels()   const = 0;
};
