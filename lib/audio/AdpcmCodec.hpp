#pragma once

#include "AudioCodec.hpp"
#include <cstdint>

/**
 * AdpcmCodec
 * ----------------------------------------------------------------------------
 * IMA-ADPCM (4-bit) codec
 *
 * - Mono
 * - Input PCM: int16_t
 * - Output: 4-bit ADPCM packed into bytes
 *
 * Statefull codec:
 *  - predictor_
 *  - step_index_
 *
 * reset() MUST be called:
 *  - when starting new utterance
 *  - when WebSocket reconnect
 *  - when server requests resync
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

    // ADPCM -> PCM
    size_t decode(const uint8_t* data,
                  size_t data_len,
                  int16_t* pcm_out,
                  size_t pcm_capacity) override;

    // Reset predictor & index
    void reset() override;

    uint32_t sampleRate() const override;
    uint8_t  channels() const override;

private:
    int predictor_   = 0;   // last PCM value
    int step_index_  = 0;   // index into step table
    uint32_t sample_rate_;
};
