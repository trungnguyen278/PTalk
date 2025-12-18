#pragma once

#include <memory>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

#include "system/StateTypes.hpp"
#include "system/StateManager.hpp"

// Forward declarations
class AudioInput;
class AudioOutput;
class AudioCodec;

/**
 * AudioManager
 * ============================================================================
 * - Quản lý audio state (LISTENING / SPEAKING / IDLE / SLEEPING)
 * - Điều phối AudioInput / AudioOutput / AudioCodec
 * - KHÔNG làm network
 * - Cung cấp ring buffer cho module khác (NetworkManager)
 */
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------
    bool init();
    void start();
    void stop();

    // ------------------------------------------------------------------------
    // Dependency injection
    // ------------------------------------------------------------------------
    void setInput(std::unique_ptr<AudioInput> in);
    void setOutput(std::unique_ptr<AudioOutput> out);
    void setCodec(std::unique_ptr<AudioCodec> cdc);

    // ------------------------------------------------------------------------
    // Stream buffer access (NetworkManager dùng)
    // ------------------------------------------------------------------------
    StreamBufferHandle_t getMicEncodedBuffer() const { return sb_mic_encoded; }
    StreamBufferHandle_t getSpeakerEncodedBuffer() const { return sb_spk_encoded; }

    // ------------------------------------------------------------------------
    // Power / control
    // ------------------------------------------------------------------------
    void setPowerSaving(bool enable);

private:
    // ------------------------------------------------------------------------
    // State callback
    // ------------------------------------------------------------------------
    void handleInteractionState(state::InteractionState s,
                                state::InputSource src);

    // ------------------------------------------------------------------------
    // Audio actions
    // ------------------------------------------------------------------------
    void startListening(state::InputSource src);
    void pauseListening();
    void stopListening();

    void startSpeaking();
    void stopSpeaking();

    void stopAll();

private:
    // ------------------------------------------------------------------------
    // Tasks
    // ------------------------------------------------------------------------
    static void micTaskEntry(void* arg);
    static void codecTaskEntry(void* arg);
    static void spkTaskEntry(void* arg);

    void micTaskLoop();
    void codecTaskLoop();
    void spkTaskLoop();

private:
    // ------------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------------
    std::atomic<bool> started{false};
    std::atomic<bool> listening{false};
    std::atomic<bool> speaking{false};
    std::atomic<bool> power_saving{false};
    std::atomic<bool> spk_playing{false};

    state::InputSource current_source = state::InputSource::UNKNOWN;

    // ------------------------------------------------------------------------
    // Components
    // ------------------------------------------------------------------------
    std::unique_ptr<AudioInput>  input;
    std::unique_ptr<AudioOutput> output;
    std::unique_ptr<AudioCodec>  codec;

    // ------------------------------------------------------------------------
    // Stream buffers (FreeRTOS - thread-safe, no race conditions)
    // ------------------------------------------------------------------------
    StreamBufferHandle_t sb_mic_pcm;      // PCM from mic
    StreamBufferHandle_t sb_mic_encoded;  // encoded uplink
    StreamBufferHandle_t sb_spk_pcm;      // PCM to speaker
    StreamBufferHandle_t sb_spk_encoded;  // encoded downlink
    
    // PCM decode buffer (static allocation to avoid heap alloc in task)
    int16_t spk_pcm_buffer[4096] = {};

    // ------------------------------------------------------------------------
    // Tasks
    // ------------------------------------------------------------------------
    TaskHandle_t mic_task = nullptr;
    TaskHandle_t codec_task = nullptr;
    TaskHandle_t spk_task = nullptr;

    // ------------------------------------------------------------------------
    // StateManager subscription
    // ------------------------------------------------------------------------
    int sub_interaction_id = -1;
};
