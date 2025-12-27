#include "AdpcmCodec.hpp"
#include <algorithm>

// ================= IMA ADPCM tables =================

static const int8_t indexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int16_t stepTable[89] = {
     7, 8, 9, 10, 11, 12, 13, 14,
    16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97,107,118,130,143,
   157,173,190,209,230,253,279,307,
   337,371,408,449,494,544,598,658,
   724,796,876,963,1060,1166,1282,1411,
   1552,1707,1878,2066,2272,2499,2749,3024,
   3327,3660,4026,4428,4871,5358,5894,6484,
   7132,7845,8630,9493,10442,11487,12635,13899,
   15289,16818,18500,20350,22385,24623,27086,29794,
   32767
};

// ===================================================

AdpcmCodec::AdpcmCodec(uint32_t sample_rate)
    : sample_rate_(sample_rate) {}

void AdpcmCodec::reset()
{
    enc_ = {};
    dec_ = {};
}

// ===================================================
// Encode PCM -> ADPCM (4:1)
// ===================================================

size_t AdpcmCodec::encode(const int16_t* pcm,
                             size_t pcm_samples,
                             uint8_t* out,
                             size_t out_capacity)
{
    int predictor = enc_.predictor;
    int index = enc_.index;
    int step = stepTable[index];

    size_t out_index = 0;
    uint8_t out_byte = 0;
    bool high_nibble = false;

    for (size_t i = 0; i < pcm_samples && out_index < out_capacity; ++i)
    {
        int diff = pcm[i] - predictor;
        int sign = (diff < 0) ? 8 : 0;
        if (sign) diff = -diff;

        int delta = 0;
        int tempStep = step;

        if (diff >= tempStep) { delta |= 4; diff -= tempStep; }
        tempStep >>= 1;
        if (diff >= tempStep) { delta |= 2; diff -= tempStep; }
        tempStep >>= 1;
        if (diff >= tempStep) delta |= 1;

        int nibble = delta | sign;

        int diffq = step >> 3;
        if (delta & 4) diffq += step;
        if (delta & 2) diffq += step >> 1;
        if (delta & 1) diffq += step >> 2;

        predictor += sign ? -diffq : diffq;
        predictor = std::clamp(predictor, -32768, 32767);

        index += indexTable[nibble];
        index = std::clamp(index, 0, 88);
        step = stepTable[index];

        if (!high_nibble) {
            out_byte = nibble & 0x0F;
            high_nibble = true;
        } else {
            out[out_index++] = out_byte | ((nibble & 0x0F) << 4);
            high_nibble = false;
        }
    }

    if (high_nibble && out_index < out_capacity)
        out[out_index++] = out_byte;

    enc_.predictor = predictor;
    enc_.index = index;

    return out_index;
}

// ===================================================
// Decode ADPCM -> PCM
// ===================================================

size_t AdpcmCodec::decode(const uint8_t* data,
                             size_t data_len,
                             int16_t* pcm_out,
                             size_t pcm_capacity)
{
    int predictor = dec_.predictor;
    int index = dec_.index;
    int step = stepTable[index];

    size_t out_samples = 0;

    for (size_t i = 0; i < data_len && out_samples < pcm_capacity; ++i)
    {
        uint8_t byte = data[i];

        for (int shift = 0; shift <= 4; shift += 4)
        {
            int nibble = (byte >> shift) & 0x0F;
            int sign = nibble & 8;
            int delta = nibble & 7;

            int diffq = step >> 3;
            if (delta & 4) diffq += step;
            if (delta & 2) diffq += step >> 1;
            if (delta & 1) diffq += step >> 2;

            predictor += sign ? -diffq : diffq;
            predictor = std::clamp(predictor, -32768, 32767);

            index += indexTable[nibble];
            index = std::clamp(index, 0, 88);
            step = stepTable[index];

            pcm_out[out_samples++] = predictor;
            if (out_samples >= pcm_capacity) break;
        }
    }

    dec_.predictor = predictor;
    dec_.index = index;

    return out_samples;
}

// ===================================================

size_t AdpcmCodec::pcmFrameSamples() const { return 256; }
size_t AdpcmCodec::encodedFrameBytes() const { return 512; }

uint32_t AdpcmCodec::sampleRate() const { return sample_rate_; }
uint8_t AdpcmCodec::channels() const { return 1; }
