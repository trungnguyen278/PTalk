#pragma once

#include "AudioCodec.hpp"
#include <cstdint>
#include <memory>

struct OpusEncoder;
struct OpusDecoder;

/**
 * OpusCodec
 * ============================================================================
 * Opus compression codec for real-time audio streaming.
 * 
 * - Input: 16-bit PCM mono audio
 * - Output: Opus-encoded frames (variable-length)
 * - Sample rate: 16 kHz (standard for voice)
 * - Frame duration: 20 ms (320 samples per frame)
 * - Bitrate: ~10-16 kbps (ultra-low for IoT)
 * - Quality: Good voice clarity for speech
 *
 * State:
 * - OpusEncoder & OpusDecoder maintain internal state
 * - reset() clears state for packet loss recovery
 * - NOT real-time safe if memory allocation fails mid-stream
 */
class OpusCodec : public AudioCodec {
public:
    // ========================================================================
    // Constructor / Destructor
    // ========================================================================
    explicit OpusCodec(uint32_t sample_rate = 16000,
                       int bitrate_bps = 12000);
    ~OpusCodec() override;

    // Disable copy/move (contains opaque pointers to opus state)
    OpusCodec(const OpusCodec&) = delete;
    OpusCodec& operator=(const OpusCodec&) = delete;

    // ========================================================================
    // AudioCodec Interface
    // ========================================================================

    /**
     * Encode PCM samples to Opus frames
     * 
     * @param pcm             Pointer to int16_t PCM samples (mono)
     * @param pcm_samples     Number of samples to encode
     * @param out             Output buffer for encoded frames
     * @param out_capacity    Max bytes to write
     * @return                Bytes written to output buffer
     * 
     * Note: Opus works on frames (320 samples @ 16kHz = 20ms)
     *       Partial frames are buffered internally and encoded when complete
     */
    size_t encode(const int16_t* pcm,
                  size_t pcm_samples,
                  uint8_t* out,
                  size_t out_capacity) override;

    /**
     * Decode Opus frames to PCM samples
     * 
     * @param data            Pointer to encoded Opus data
     * @param data_len        Length of encoded data
     * @param pcm_out         Output buffer for PCM samples
     * @param pcm_capacity    Max samples to write
     * @return                Number of PCM samples decoded
     */
    size_t decode(const uint8_t* data,
                  size_t data_len,
                  int16_t* pcm_out,
                  size_t pcm_capacity) override;

    /**
     * Reset encoder/decoder state
     * Important for handling packet loss or stream restarts
     */
    void reset() override;

    uint32_t sampleRate() const override;
    uint8_t  channels() const override;

private:
    // ========================================================================
    // State
    // ========================================================================
    OpusEncoder* encoder_;
    OpusDecoder* decoder_;

    uint32_t sample_rate_;
    int bitrate_bps_;

    // Frame buffering for encode
    static constexpr int FRAME_SIZE = 320;  // 20ms @ 16kHz
    int16_t frame_buffer_[FRAME_SIZE];
    size_t frame_buffer_pos_;
};
