#include "AdpcmCodec.hpp"
#include <algorithm>
#include <cstring>

// ============================================================================
// IMA ADPCM tables
// ============================================================================

static const int indexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int stepTable[89] = {
     7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

// ============================================================================
// Constructor / Reset
// ============================================================================

AdpcmCodec::AdpcmCodec(uint32_t sample_rate)
    : sample_rate_(sample_rate)
{
    reset();
}

void AdpcmCodec::reset()
{
    predictor_ = 0;
    step_index_ = 0;
}

// ============================================================================
// Encode
// ============================================================================

size_t AdpcmCodec::encode(const int16_t* pcm,
                          size_t pcm_samples,
                          uint8_t* out,
                          size_t out_capacity)
{
    if (!pcm || !out || out_capacity == 0) return 0;

    size_t out_index = 0;
    uint8_t nibble_byte = 0;
    bool high_nibble = true;

    for (size_t i = 0; i < pcm_samples; i++) {
        int diff = pcm[i] - predictor_;
        int step = stepTable[step_index_];
        int code = 0;

        if (diff < 0) {
            code = 8;
            diff = -diff;
        }

        if (diff >= step) { code |= 4; diff -= step; }
        if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
        if (diff >= step >> 2) { code |= 1; }

        int delta = step >> 3;
        if (code & 4) delta += step;
        if (code & 2) delta += step >> 1;
        if (code & 1) delta += step >> 2;

        if (code & 8) predictor_ -= delta;
        else predictor_ += delta;

        predictor_ = std::clamp(predictor_, -32768, 32767);

        step_index_ += indexTable[code];
        step_index_ = std::clamp(step_index_, 0, 88);

        if (high_nibble) {
            nibble_byte = (code & 0x0F) << 4;
            high_nibble = false;
        } else {
            nibble_byte |= (code & 0x0F);
            if (out_index < out_capacity) {
                out[out_index++] = nibble_byte;
            }
            high_nibble = true;
        }
    }

    // flush last nibble
    if (!high_nibble && out_index < out_capacity) {
        out[out_index++] = nibble_byte;
    }

    return out_index;
}

// ============================================================================
// Decode
// ============================================================================

size_t AdpcmCodec::decode(const uint8_t* data,
                          size_t data_len,
                          int16_t* pcm_out,
                          size_t pcm_capacity)
{
    if (!data || !pcm_out) return 0;

    size_t pcm_index = 0;

    for (size_t i = 0; i < data_len; i++) {
        for (int shift = 4; shift >= 0; shift -= 4) {
            if (pcm_index >= pcm_capacity) return pcm_index;

            int code = (data[i] >> shift) & 0x0F;
            int step = stepTable[step_index_];

            int delta = step >> 3;
            if (code & 4) delta += step;
            if (code & 2) delta += step >> 1;
            if (code & 1) delta += step >> 2;

            if (code & 8) predictor_ -= delta;
            else predictor_ += delta;

            predictor_ = std::clamp(predictor_, -32768, 32767);
            step_index_ += indexTable[code];
            step_index_ = std::clamp(step_index_, 0, 88);

            pcm_out[pcm_index++] = static_cast<int16_t>(predictor_);
        }
    }

    return pcm_index;
}

// ============================================================================
// Info
// ============================================================================

uint32_t AdpcmCodec::sampleRate() const {
    return sample_rate_;
}

uint8_t AdpcmCodec::channels() const {
    return 1;
}
