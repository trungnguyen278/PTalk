#include "OpusCodec.hpp"
#include <algorithm>
#include <cstring>
#include <esp_log.h>

// ============================================================================
// Opus headers (from opus library)
// ============================================================================
extern "C" {
#include "opus.h"
}

static const char* TAG = "OpusCodec";

// ============================================================================
// Constructor / Destructor
// ============================================================================

OpusCodec::OpusCodec(uint32_t sample_rate, int bitrate_bps)
    : encoder_(nullptr),
      decoder_(nullptr),
      sample_rate_(sample_rate),
      bitrate_bps_(bitrate_bps),
      frame_buffer_pos_(0)
{
    std::memset(frame_buffer_, 0, sizeof(frame_buffer_));

    int err = OPUS_OK;

    // Create encoder (mono, hardcoded for voice)
    encoder_ = opus_encoder_create(sample_rate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !encoder_) {
        ESP_LOGE(TAG, "Failed to create Opus encoder: %d", err);
        encoder_ = nullptr;
        return;
    }

    // Set bitrate (lower = more compression, lower quality)
    err = opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_bps));
    if (err != OPUS_OK) {
        ESP_LOGW(TAG, "Failed to set bitrate: %d", err);
    }

    // Create decoder (mono, same sample rate)
    decoder_ = opus_decoder_create(sample_rate, 1, &err);
    if (err != OPUS_OK || !decoder_) {
        ESP_LOGE(TAG, "Failed to create Opus decoder: %d", err);
        decoder_ = nullptr;
        if (encoder_) {
            opus_encoder_destroy(encoder_);
            encoder_ = nullptr;
        }
        return;
    }

    ESP_LOGI(TAG, "OpusCodec initialized: %u Hz, %d bps", sample_rate, bitrate_bps);
}

OpusCodec::~OpusCodec()
{
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}

// ============================================================================
// Reset
// ============================================================================

void OpusCodec::reset()
{
    if (encoder_) {
        opus_encoder_ctl(encoder_, OPUS_RESET_STATE);
    }
    if (decoder_) {
        opus_decoder_ctl(decoder_, OPUS_RESET_STATE);
    }
    frame_buffer_pos_ = 0;
    std::memset(frame_buffer_, 0, sizeof(frame_buffer_));
}

// ============================================================================
// Encode: PCM -> Opus frames
// ============================================================================

size_t OpusCodec::encode(const int16_t* pcm,
                         size_t pcm_samples,
                         uint8_t* out,
                         size_t out_capacity)
{
    if (!encoder_ || !pcm || !out || out_capacity == 0) {
        return 0;
    }

    size_t total_encoded = 0;
    const int frame_size = FRAME_SIZE;
    const int16_t* src = pcm;
    size_t remaining = pcm_samples;

    // If we have leftover samples from previous call, prepend them
    if (frame_buffer_pos_ > 0) {
        size_t to_fill = frame_size - frame_buffer_pos_;
        size_t to_copy = std::min(to_fill, remaining);

        std::memcpy(&frame_buffer_[frame_buffer_pos_],
                    src,
                    to_copy * sizeof(int16_t));

        frame_buffer_pos_ += to_copy;
        src += to_copy;
        remaining -= to_copy;

        // If frame is complete, encode it
        if (frame_buffer_pos_ == frame_size) {
            int encoded_bytes = opus_encode(encoder_,
                                           frame_buffer_,
                                           frame_size,
                                           &out[total_encoded],
                                           (opus_int32)(out_capacity - total_encoded));

            if (encoded_bytes > 0) {
                total_encoded += encoded_bytes;
                frame_buffer_pos_ = 0;
            } else if (encoded_bytes < 0) {
                ESP_LOGW(TAG, "Opus encode error: %d", encoded_bytes);
                frame_buffer_pos_ = 0;
                return total_encoded;
            }
        }
    }

    // Encode complete frames from input
    while (remaining >= frame_size && total_encoded + 4096 <= out_capacity) {
        int encoded_bytes = opus_encode(encoder_,
                                       src,
                                       frame_size,
                                       &out[total_encoded],
                                       (opus_int32)(out_capacity - total_encoded));

        if (encoded_bytes > 0) {
            total_encoded += encoded_bytes;
            src += frame_size;
            remaining -= frame_size;
        } else if (encoded_bytes < 0) {
            ESP_LOGW(TAG, "Opus encode error: %d", encoded_bytes);
            return total_encoded;
        } else {
            // Zero-length frame (e.g., DTX), skip
            src += frame_size;
            remaining -= frame_size;
        }
    }

    // Buffer any remaining partial frame
    if (remaining > 0) {
        std::memcpy(frame_buffer_, src, remaining * sizeof(int16_t));
        frame_buffer_pos_ = remaining;
    }

    return total_encoded;
}

// ============================================================================
// Decode: Opus frames -> PCM
// ============================================================================

size_t OpusCodec::decode(const uint8_t* data,
                         size_t data_len,
                         int16_t* pcm_out,
                         size_t pcm_capacity)
{
    if (!decoder_ || !data || !pcm_out || pcm_capacity == 0) {
        return 0;
    }

    // Opus decode returns number of samples (not bytes)
    int decoded_samples = opus_decode(decoder_,
                                      data,
                                      (opus_int32)data_len,
                                      pcm_out,
                                      (int)pcm_capacity,
                                      0);  // decode_fec = 0 (no FEC)

    if (decoded_samples < 0) {
        ESP_LOGW(TAG, "Opus decode error: %d", decoded_samples);
        return 0;
    }

    return (size_t)decoded_samples;
}

// ============================================================================
// Properties
// ============================================================================

uint32_t OpusCodec::sampleRate() const {
    return sample_rate_;
}

uint8_t OpusCodec::channels() const {
    return 1;  // mono
}
