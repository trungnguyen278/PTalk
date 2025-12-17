#include "AudioManager.hpp"

#include "AudioInput.hpp"
#include "AudioOutput.hpp"
#include "AudioCodec.hpp"

#include "esp_log.h"
#include <cstring>

static const char* TAG = "AudioManager";

// ============================================================================
// Constructor / Destructor
// ============================================================================
AudioManager::AudioManager() = default;

AudioManager::~AudioManager()
{
    stop();

    if (rb_mic_pcm)     vRingbufferDelete(rb_mic_pcm);
    if (rb_mic_encoded) vRingbufferDelete(rb_mic_encoded);
    if (rb_spk_pcm)     vRingbufferDelete(rb_spk_pcm);
    if (rb_spk_encoded) vRingbufferDelete(rb_spk_encoded);
}

// ============================================================================
// Dependency injection
// ============================================================================
void AudioManager::setInput(std::unique_ptr<AudioInput> in)
{
    input = std::move(in);
}

void AudioManager::setOutput(std::unique_ptr<AudioOutput> out)
{
    output = std::move(out);
}

void AudioManager::setCodec(std::unique_ptr<AudioCodec> cdc)
{
    codec = std::move(cdc);
}

// ============================================================================
// Init / Start / Stop
// ============================================================================
bool AudioManager::init()
{
    ESP_LOGI(TAG, "init()");

    if (!input || !output || !codec) {
        ESP_LOGE(TAG, "Missing input/output/codec");
        return false;
    }

    // -------------------------------
    // Ring buffers
    // -------------------------------
    rb_mic_pcm     = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF);
    rb_mic_encoded = xRingbufferCreate(4 * 1024, RINGBUF_TYPE_BYTEBUF);
    rb_spk_pcm     = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF);
    rb_spk_encoded = xRingbufferCreate(4 * 1024, RINGBUF_TYPE_BYTEBUF);

    if (!rb_mic_pcm || !rb_mic_encoded || !rb_spk_pcm || !rb_spk_encoded) {
        ESP_LOGE(TAG, "Failed to create ring buffers");
        return false;
    }

    // -------------------------------
    // Subscribe InteractionState
    // -------------------------------
    sub_interaction_id =
        StateManager::instance().subscribeInteraction(
            [this](state::InteractionState s, state::InputSource src) {
                this->handleInteractionState(s, src);
            });

    ESP_LOGI(TAG, "AudioManager init OK");
    return true;
}

void AudioManager::start()
{
    if (started) return;
    started = true;

    ESP_LOGI(TAG, "start()");

    // -------------------------------
    // Create MIC task
    // -------------------------------
    xTaskCreatePinnedToCore(
        &AudioManager::micTaskEntry,
        "AudioMicTask",
        4096,
        this,
        5,
        &mic_task,
        0
    );

    // -------------------------------
    // Create SPEAKER task
    // -------------------------------
    xTaskCreatePinnedToCore(
        &AudioManager::spkTaskEntry,
        "AudioSpkTask",
        4096,
        this,
        5,
        &spk_task,
        0
    );
}

void AudioManager::stop()
{
    if (!started) return;
    started = false;

    ESP_LOGW(TAG, "stop()");

    stopAll();

    // ✅ Allow tasks to exit themselves (they check `started` and self-delete)
    // Wait up to 1s for both tasks to terminate; then force delete as fallback.
    const uint32_t TIMEOUT_MS = 1000;
    uint32_t waited = 0;

    auto waitForExit = [&](TaskHandle_t &th) {
        if (!th) return;
        while (eTaskGetState(th) != eDeleted && waited < TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (eTaskGetState(th) != eDeleted) {
            ESP_LOGW(TAG, "Audio task did not exit; force deleting");
            vTaskDelete(th);
        }
        th = nullptr;
    };

    waitForExit(mic_task);
    waitForExit(spk_task);
}

// ============================================================================
// State handling
// ============================================================================
void AudioManager::handleInteractionState(state::InteractionState s,
                                          state::InputSource src)
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

// ============================================================================
// Audio actions
// ============================================================================
void AudioManager::startListening(state::InputSource src)
{
    if (listening) return;

    ESP_LOGI(TAG, "Start listening");
    current_source = src;
    listening = true;
    speaking  = false;

    input->startCapture();
}

void AudioManager::pauseListening()
{
    if (!listening) return;
    ESP_LOGI(TAG, "Pause listening");
    input->stopCapture();
}

void AudioManager::stopListening()
{
    if (!listening) return;
    ESP_LOGI(TAG, "Stop listening");
    listening = false;
    input->stopCapture();
}

void AudioManager::startSpeaking()
{
    if (speaking) return;
    ESP_LOGI(TAG, "Start speaking");
    speaking = true;
    output->startPlayback();
}

void AudioManager::stopSpeaking()
{
    if (!speaking) return;
    ESP_LOGI(TAG, "Stop speaking");
    speaking = false;
    output->stopPlayback();
}

void AudioManager::stopAll()
{
    stopListening();
    stopSpeaking();
}

void AudioManager::setPowerSaving(bool enable)
{
    power_saving = enable;
    if (enable) stopAll();
}

// ============================================================================
// Tasks
// ============================================================================
void AudioManager::micTaskEntry(void* arg)
{
    static_cast<AudioManager*>(arg)->micTaskLoop();
}

void AudioManager::spkTaskEntry(void* arg)
{
    static_cast<AudioManager*>(arg)->spkTaskLoop();
}

// ============================================================================
// MIC task: PCM → ENCODE → rb_mic_encoded
// ============================================================================
void AudioManager::micTaskLoop()
{
    ESP_LOGI(TAG, "MIC task started");

    int16_t pcm_buf[256];

    while (started) {
        if (!listening || power_saving) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t samples = input->readPcm(pcm_buf, 256);
        if (samples == 0) continue;

        uint8_t encoded[256];
        size_t enc_len = codec->encode(
            pcm_buf,
            samples,
            encoded,
            sizeof(encoded)
        );

        if (enc_len > 0) {
            xRingbufferSend(rb_mic_encoded, encoded, enc_len, 0);
        }
    }

    ESP_LOGW(TAG, "MIC task stopped");
    vTaskDelete(nullptr);
}

// ============================================================================
// SPEAKER task: rb_spk_encoded → DECODE → PCM → AudioOutput
// ============================================================================
void AudioManager::spkTaskLoop()
{
    ESP_LOGI(TAG, "Speaker task started");

    while (started) {
        if (!speaking || power_saving) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t item_size = 0;
        uint8_t* enc =
            (uint8_t*)xRingbufferReceive(rb_spk_encoded, &item_size,
                                         pdMS_TO_TICKS(20));
        if (!enc) continue;

        int16_t pcm[512];
        size_t samples = codec->decode(
            enc,
            item_size,
            pcm,
            512
        );

        vRingbufferReturnItem(rb_spk_encoded, enc);

        if (samples > 0) {
            output->writePcm(pcm, samples);
        }
    }

    ESP_LOGW(TAG, "Speaker task stopped");
    vTaskDelete(nullptr);
}
