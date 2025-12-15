#pragma once

#include <cstdint>
#include <memory>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "system/StateManager.hpp"
#include "system/StateTypes.hpp"

// Low-level audio interfaces
class AudioInput;      // Mic (INMP441)
class AudioOutput;     // Speaker (MAX98357)
class AudioCodec;      // ADPCM / Opus (interface)

// Network forward (AudioManager does NOT include NetworkManager.hpp)
class NetworkManager;

/**
 * AudioManager
 * ============================================================================
 * Responsibilities:
 *  - React to InteractionState (LISTENING / SPEAKING / IDLE / SLEEPING)
 *  - Own audio data pipeline
 *  - Manage ring buffers
 *  - Encode / decode audio
 *  - Push / pull data to NetworkManager (binary only)
 *
 * DOES NOT:
 *  - Touch WiFi / WebSocket directly
 *  - Handle UI
 *  - Decide system state
 */
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // =========================================================================
    // Lifecycle
    // =========================================================================
    bool init(NetworkManager* net);   // must be called before start()
    void start();
    void stop();

    // =========================================================================
    // Dependency injection (set BEFORE start)
    // =========================================================================
    void setInput(std::unique_ptr<AudioInput> in);
    void setOutput(std::unique_ptr<AudioOutput> out);
    void setCodec(std::unique_ptr<AudioCodec> c);

    // =========================================================================
    // State-driven control (called by AppController)
    // =========================================================================
    void startListening(state::InputSource src);
    void pauseListening();
    void startSpeaking();
    void stopAll();
    void setPowerSaving(bool enable);

    // =========================================================================
    // Network → Audio (called by NetworkManager)
    // =========================================================================
    void onAudioPacketFromServer(const uint8_t* data, size_t len);

private:
    // =========================================================================
    // Internal state handling
    // =========================================================================
    void handleInteractionState(state::InteractionState s);

    // =========================================================================
    // Audio Tasks
    // =========================================================================
    static void micTaskEntry(void* arg);
    static void spkTaskEntry(void* arg);

    void micTask();   // Capture → Encode → Send
    void spkTask();   // Receive → Decode → Play

private:
    // =========================================================================
    // Dependencies
    // =========================================================================
    NetworkManager* network = nullptr;

    std::unique_ptr<AudioInput>  input;
    std::unique_ptr<AudioOutput> output;
    std::unique_ptr<AudioCodec>  codec;

private:
    // =========================================================================
    // Ring Buffers
    // =========================================================================
    // Mic pipeline
    RingbufHandle_t rb_mic_pcm      = nullptr;  // int16_t
    RingbufHandle_t rb_mic_encoded  = nullptr;  // uint8_t

    // Speaker pipeline
    RingbufHandle_t rb_spk_encoded  = nullptr;  // uint8_t
    RingbufHandle_t rb_spk_pcm      = nullptr;  // int16_t

private:
    // =========================================================================
    // Tasks
    // =========================================================================
    TaskHandle_t mic_task = nullptr;
    TaskHandle_t spk_task = nullptr;

private:
    // =========================================================================
    // Runtime flags
    // =========================================================================
    std::atomic<bool> started   {false};
    std::atomic<bool> listening {false};
    std::atomic<bool> speaking  {false};
    std::atomic<bool> power_save{false};

    state::InputSource current_source = state::InputSource::UNKNOWN;

private:
    // =========================================================================
    // State subscription
    // =========================================================================
    int sub_interaction_id = -1;
};
