#pragma once
#include "AudioCodec.hpp"

class AdpcmCodec : public AudioCodec {
public:
    explicit AdpcmCodec(uint32_t sample_rate = 16000);

    size_t encode(const int16_t* pcm,
                  size_t pcm_samples,
                  uint8_t* out,
                  size_t out_capacity) override;

    size_t decode(const uint8_t* data,
                  size_t data_len,
                  int16_t* pcm_out,
                  size_t pcm_capacity) override;

    void reset() override;

    size_t pcmFrameSamples() const override;
    size_t encodedFrameBytes() const override;

    uint32_t sampleRate() const override;
    uint8_t channels() const override;

private:
    struct AdpcmState {
        int16_t predictor = 0;
        int8_t  index = 0;
    };

    AdpcmState enc_;
    AdpcmState dec_;
    uint32_t sample_rate_;
};
