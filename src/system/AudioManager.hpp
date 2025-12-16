#pragma once

#include <memory>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

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
    // Ring buffer access (NetworkManager dùng)
    // ------------------------------------------------------------------------
    RingbufHandle_t getMicEncodedBuffer() const { return rb_mic_encoded; }
    RingbufHandle_t getSpeakerEncodedBuffer() const { return rb_spk_encoded; }

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
    static void spkTaskEntry(void* arg);

    void micTaskLoop();
    void spkTaskLoop();

private:
    // ------------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------------
    std::atomic<bool> started{false};
    std::atomic<bool> listening{false};
    std::atomic<bool> speaking{false};
    std::atomic<bool> power_saving{false};

    state::InputSource current_source = state::InputSource::UNKNOWN;

    // ------------------------------------------------------------------------
    // Components
    // ------------------------------------------------------------------------
    std::unique_ptr<AudioInput>  input;
    std::unique_ptr<AudioOutput> output;
    std::unique_ptr<AudioCodec>  codec;

    // ------------------------------------------------------------------------
    // Ring buffers
    // ------------------------------------------------------------------------
    RingbufHandle_t rb_mic_pcm      = nullptr; // PCM from mic
    RingbufHandle_t rb_mic_encoded  = nullptr; // encoded uplink
    RingbufHandle_t rb_spk_encoded  = nullptr; // encoded downlink
    RingbufHandle_t rb_spk_pcm      = nullptr; // PCM to speaker

    // ------------------------------------------------------------------------
    // Tasks
    // ------------------------------------------------------------------------
    TaskHandle_t mic_task = nullptr;
    TaskHandle_t spk_task = nullptr;

    // ------------------------------------------------------------------------
    // StateManager subscription
    // ------------------------------------------------------------------------
    int sub_interaction_id = -1;
};
