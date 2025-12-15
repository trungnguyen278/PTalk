#include "AudioManager.hpp"

#include "AudioInput.hpp"
#include "AudioOutput.hpp"
#include "AudioCodec.hpp"

#include "esp_log.h"

static const char* TAG = "AudioManager";

AudioManager::AudioManager() = default;

AudioManager::~AudioManager()
{
    stop();

    if (sub_interaction_id != -1) {
        StateManager::instance().unsubscribeInteraction(sub_interaction_id);
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

bool AudioManager::init(AudioInput* in,
                        AudioOutput* out,
                        std::unique_ptr<AudioCodec> codec_impl)
{
    if (!in || !out || !codec_impl) {
        ESP_LOGE(TAG, "init failed: null dependency");
        return false;
    }

    input  = in;
    output = out;
    codec  = std::move(codec_impl);

    // Subscribe to InteractionState
    sub_interaction_id = StateManager::instance().subscribeInteraction(
        [this](state::InteractionState s, state::InputSource src) {
            this->handleInteractionState(s, src);
        }
    );


    ESP_LOGI(TAG, "AudioManager initialized");
    return true;
}

void AudioManager::start()
{
    if (started) return;
    started = true;

    ESP_LOGI(TAG, "AudioManager started");
}

void AudioManager::stop()
{
    if (!started) return;
    started = false;

    stopAll();

    ESP_LOGI(TAG, "AudioManager stopped");
}

// ============================================================================
// Interaction control (called by AppController)
// ============================================================================

void AudioManager::startListening(state::InputSource src)
{
    if (!started || listening) return;

    current_source = src;
    listening = true;
    speaking  = false;

    ESP_LOGI(TAG, "Start listening (source=%d)", (int)src);

    if (input) {
        input->startCapture();
    }
}

void AudioManager::pauseListening()
{
    if (!listening) return;

    ESP_LOGI(TAG, "Pause listening");

    if (input) {
        input->pauseCapture();
    }
}

void AudioManager::stopListening()
{
    if (!listening) return;

    ESP_LOGI(TAG, "Stop listening");

    listening = false;

    if (input) {
        input->stopCapture();
    }
}

void AudioManager::startSpeaking()
{
    if (!started) return;

    ESP_LOGI(TAG, "Start speaking");

    listening = false;
    speaking  = true;

    if (input) {
        input->stopCapture();
    }

    if (output) {
        output->startPlayback();
    }
}

void AudioManager::stopAll()
{
    ESP_LOGI(TAG, "Stop all audio");

    listening = false;
    speaking  = false;

    if (input) {
        input->stopCapture();
    }

    if (output) {
        output->stopPlayback();
    }
}

// ============================================================================
// Network interaction
// ============================================================================

void AudioManager::onAudioPacketFromServer(const uint8_t* data, size_t len)
{
    if (!started || !speaking || !codec || !output) return;

    // Decode → PCM
    int16_t pcm_buf[2048];
    size_t pcm_len = 0;

    size_t pcm_samples = codec->decode(
        data,
        len,
        pcm_buf,
        sizeof(pcm_buf) / sizeof(int16_t)
    );

    if (pcm_samples == 0) {
        ESP_LOGW(TAG, "Decode failed");
        return;
    }


    output->writePcm(pcm_buf, pcm_len);
}

void AudioManager::onServerTtsEnd()
{
    ESP_LOGI(TAG, "Server TTS end");

    speaking = false;

    if (output) {
        output->stopPlayback();
    }

    // Back to idle
    StateManager::instance().setInteractionState(
        state::InteractionState::IDLE,
        state::InputSource::SERVER_COMMAND
    );
}

// ============================================================================
// Power & mute
// ============================================================================

void AudioManager::setMicMuted(bool mute)
{
    muted = mute;

    if (input) {
        input->setMuted(mute);
    }

    ESP_LOGI(TAG, "Mic muted: %s", mute ? "YES" : "NO");
}

void AudioManager::setPowerSaving(bool enable)
{
    ESP_LOGI(TAG, "Power saving: %s", enable ? "ON" : "OFF");

    if (input)  input->setLowPower(enable);
    if (output) output->setLowPower(enable);
}

// ============================================================================
// State-driven behavior
// ============================================================================

void AudioManager::handleInteractionState(state::InteractionState s, state::InputSource src)
{
    switch (s) {
    case state::InteractionState::LISTENING:
        startListening(src);
        break;

    case state::InteractionState::PROCESSING:
        pauseListening();
        break;

    case state::InteractionState::SPEAKING:
        startSpeaking();
        break;

    case state::InteractionState::CANCELLING:
    case state::InteractionState::IDLE:
        stopAll();
        break;

    case state::InteractionState::SLEEPING:
        stopAll();
        setPowerSaving(true);
        break;

    default:
        break;
    }
}
