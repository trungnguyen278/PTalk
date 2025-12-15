#pragma once

#include <cstdint>
#include <cstddef>

/**
 * AudioInput
 * ============================================================================
 * Vai trò:
 *  - Thu PCM thô từ mic (I2S / ADC / PDM / test source)
 *  - KHÔNG encode
 *  - KHÔNG gửi network
 *  - AudioManager điều khiển vòng đời
 *
 * Dòng dữ liệu:
 *   MIC → AudioInput → PCM → AudioManager → AudioCodec
 */
class AudioInput {
public:
    virtual ~AudioInput() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    // Bắt đầu capture mic
    virtual bool startCapture() = 0;

    // Dừng capture hoàn toàn
    virtual void stopCapture() = 0;

    // Tạm dừng capture (ví dụ khi server đang xử lý)
    virtual void pauseCapture() = 0;

    // ========================================================================
    // Data access
    // ========================================================================

    /**
     * Read PCM samples
     * @param buffer      output buffer
     * @param max_bytes   max bytes to read
     * @return number of bytes read (0 = no data)
     */
    virtual size_t readPcm(int16_t* pcm, size_t max_samples) = 0;


    // ========================================================================
    // Control
    // ========================================================================

    // Mute mic (logic mute, không tắt phần cứng)
    virtual void setMuted(bool mute) = 0;

    // Power saving mode
    virtual void setLowPower(bool enable) = 0;

    // ========================================================================
    // Info
    // ========================================================================

    virtual uint32_t sampleRate() const = 0;
    virtual uint8_t  channels() const   = 0;
    virtual uint8_t  bitsPerSample() const = 0;
};
