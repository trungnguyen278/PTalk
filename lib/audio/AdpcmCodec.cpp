#include "AdpcmCodec.hpp"
#include <algorithm>
#include <cstring>

// ============================================================================
// Constructor / Reset (no-op for passthrough)
// ============================================================================

AdpcmCodec::AdpcmCodec(uint32_t sample_rate)
    : sample_rate_(sample_rate)
{
}

void AdpcmCodec::reset()
{
    // Stateless passthrough – nothing to reset
}

// ============================================================================
// Encode: PCM (int16) -> bytes (LE)
// ============================================================================

size_t AdpcmCodec::encode(const int16_t* pcm,
                          size_t pcm_samples,
                          uint8_t* out,
                          size_t out_capacity)
{
    if (!pcm || !out || out_capacity == 0) return 0;

    const size_t bytes_needed = pcm_samples * sizeof(int16_t);
    const size_t bytes_to_write = std::min(out_capacity, bytes_needed);

    // Copy as little-endian bytes
    std::memcpy(out, reinterpret_cast<const uint8_t*>(pcm), bytes_to_write);
    return bytes_to_write;
}

// ============================================================================
// Decode: bytes (LE) -> PCM (int16)
// ============================================================================

size_t AdpcmCodec::decode(const uint8_t* data,
                          size_t data_len,
                          int16_t* pcm_out,
                          size_t pcm_capacity)
{
    if (!data || !pcm_out || pcm_capacity == 0) return 0;

    // Each sample is 2 bytes. Ignore trailing odd byte if present.
    const size_t max_samples_from_bytes = data_len / 2;
    const size_t samples_to_write = std::min(pcm_capacity, max_samples_from_bytes);

    // Direct little-endian copy into int16 buffer
    std::memcpy(pcm_out, data, samples_to_write * sizeof(int16_t));
    return samples_to_write;
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
