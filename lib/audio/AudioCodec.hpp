#pragma once
#include <cstddef>
#include <cstdint>

class AudioCodec {
public:
    virtual ~AudioCodec() = default;

    // =========================================================
    // Encode: PCM (int16) -> encoded bytes
    // =========================================================
    virtual size_t encode(const int16_t* pcm,
                          size_t pcm_samples,
                          uint8_t* out,
                          size_t out_capacity) = 0;

    // =========================================================
    // Decode: encoded bytes -> PCM (int16)
    // =========================================================
    virtual size_t decode(const uint8_t* data,
                          size_t data_len,
                          int16_t* pcm_out,
                          size_t pcm_capacity) = 0;

    // =========================================================
    // Reset internal state
    //  - ADPCM: predictor + index
    //  - Opus : decoder / encoder state
    // =========================================================
    virtual void reset() = 0;

    // =========================================================
    // Frame hints (task loop KHÔNG hardcode)
    // =========================================================
    virtual size_t pcmFrameSamples() const = 0;      // e.g. 256
    virtual size_t encodedFrameBytes() const = 0;    // e.g. 512

    // =========================================================
    // Info
    // =========================================================
    virtual uint32_t sampleRate() const = 0;         // 16000
    virtual uint8_t channels() const = 0;            // 1
};
