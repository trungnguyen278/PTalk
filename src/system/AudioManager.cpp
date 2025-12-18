#include "AudioManager.hpp"

#include "AudioInput.hpp"
#include "AudioOutput.hpp"
#include "AudioCodec.hpp"
#include "esp_wifi.h"

#include "esp_log.h"
#include <cstring>

static const char *TAG = "AudioManager";

// ============================================================================
// Constructor / Destructor
// ============================================================================
AudioManager::AudioManager() = default;

AudioManager::~AudioManager()
{
    stop();
    // Stream buffers cleanup via xStreamBufferDelete if created
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

    if (!input || !output || !codec)
    {
        ESP_LOGE(TAG, "Missing input/output/codec");
        return false;
    }

    // -------------------------------
    // Stream buffers (FreeRTOS - thread-safe, no race conditions)
    // -------------------------------
    sb_mic_pcm = xStreamBufferCreate(
        4 * 1024,   // Buffer size
        1           // Trigger level (1 byte to unblock reader)
    );
    sb_mic_encoded = xStreamBufferCreate(
        2 * 1024,
        1
    );
    sb_spk_pcm = xStreamBufferCreate(
        4 * 1024,
        1
    );
    sb_spk_encoded = xStreamBufferCreate(
        32 * 1024,  // Increased for better jitter tolerance
        1
    );

    if (!sb_mic_pcm || !sb_mic_encoded || !sb_spk_pcm || !sb_spk_encoded)
    {
        ESP_LOGE(TAG, "Failed to create stream buffers");
        return false;
    }

    // -------------------------------
    // Subscribe InteractionState
    // -------------------------------
    sub_interaction_id =
        StateManager::instance().subscribeInteraction(
            [this](state::InteractionState s, state::InputSource src)
            {
                this->handleInteractionState(s, src);
            });

    ESP_LOGI(TAG, "AudioManager init OK");
    return true;
}

void AudioManager::start()
{
    if (started)
        return;
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
        0 // Core 0
    );

    // -------------------------------
    // Create CODEC task (decodes ADPCM to PCM)
    // -------------------------------
    xTaskCreatePinnedToCore(
        &AudioManager::codecTaskEntry,
        "AudioCodecTask",
        8192, // Stack for codec operations
        this,
        4, // Priority 4 - between mic and speaker
        &codec_task,
        0 // Core 0 with mic task
    );

    // -------------------------------
    // Create SPEAKER task (lower priority to not starve WiFi)
    // -------------------------------
    xTaskCreatePinnedToCore(
        &AudioManager::spkTaskEntry,
        "AudioSpkTask",
        4096, // Reduced stack - no longer doing decode
        this,
        3, // Priority 3 - below WiFi task (prio 23) to prevent beacon timeout
        &spk_task,
        1 // Core 1 for smooth playback
    );
}

void AudioManager::stop()
{
    if (!started)
        return;
    started = false;

    ESP_LOGW(TAG, "stop()");

    stopAll();

    // ✅ Allow tasks to exit themselves (they check `started` and self-delete)
    // Wait up to 1s for both tasks to terminate; then force delete as fallback.
    const uint32_t TIMEOUT_MS = 1000;
    uint32_t waited = 0;

    auto waitForExit = [&](TaskHandle_t &th)
    {
        if (!th)
            return;
        while (eTaskGetState(th) != eDeleted && waited < TIMEOUT_MS)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (eTaskGetState(th) != eDeleted)
        {
            ESP_LOGW(TAG, "Audio task did not exit; force deleting");
            vTaskDelete(th);
        }
        th = nullptr;
    };

    waitForExit(mic_task);
    waitForExit(codec_task);
    waitForExit(spk_task);
}

// ============================================================================
// State handling
// ============================================================================
void AudioManager::handleInteractionState(state::InteractionState s,
                                          state::InputSource src)
{
    switch (s)
    {
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
    if (listening)
        return;

    ESP_LOGI(TAG, "Start listening");
    current_source = src;
    listening = true;
    speaking = false;

    input->startCapture();
}

void AudioManager::pauseListening()
{
    if (!listening)
        return;
    ESP_LOGI(TAG, "Pause listening");
    input->stopCapture();
}

void AudioManager::stopListening()
{
    if (!listening)
        return;
    ESP_LOGI(TAG, "Stop listening");
    listening = false;
    input->stopCapture();
}

void AudioManager::startSpeaking()
{
    if (speaking)
        return;
    ESP_LOGI(TAG, "Start speaking");
    speaking = true;

    // DO NOT reset codec here - it breaks ADPCM predictor continuity
    // Only reset when switching to a completely new audio stream/session
}

void AudioManager::stopSpeaking()
{
    if (!speaking)
        return;
    ESP_LOGI(TAG, "Stop speaking");
    speaking = false;
    if (spk_playing)
    {
        output->stopPlayback();
        spk_playing = false;
    }
}

void AudioManager::stopAll()
{
    stopListening();
    stopSpeaking();
}

void AudioManager::setPowerSaving(bool enable)
{
    power_saving = enable;
    if (enable)
        stopAll();
}

// ============================================================================
// Tasks
// ============================================================================
void AudioManager::micTaskEntry(void *arg)
{
    static_cast<AudioManager *>(arg)->micTaskLoop();
}

void AudioManager::codecTaskEntry(void *arg)
{
    static_cast<AudioManager *>(arg)->codecTaskLoop();
}

void AudioManager::spkTaskEntry(void *arg)
{
    static_cast<AudioManager *>(arg)->spkTaskLoop();
}

// ============================================================================
// MIC task: PCM → ENCODE → rb_mic_encoded
// ============================================================================
void AudioManager::micTaskLoop()
{
    ESP_LOGI(TAG, "MIC task started");

    int16_t pcm_buf[256];

    while (started)
    {
        if (!listening || power_saving)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t samples = input->readPcm(pcm_buf, 256);
        if (samples == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10)); // Yield when no data
            continue;
        }

        uint8_t encoded[256];
        size_t enc_len = codec->encode(
            pcm_buf,
            samples,
            encoded,
            sizeof(encoded));

        if (enc_len > 0)
        {
            xStreamBufferSend(sb_mic_encoded, encoded, enc_len, pdMS_TO_TICKS(10));
        }

        // No manual delay - let scheduler decide when to context switch
        // readPcm() and encode() already provide natural blocking points
    }

    ESP_LOGW(TAG, "MIC task stopped");
    vTaskDelete(nullptr);
}

// ============================================================================
// CODEC task: ADPCM buffer → decode → PCM buffer
// Separates decode logic from I2S timing - flexible for different codecs
// ============================================================================
void AudioManager::codecTaskLoop()
{
    ESP_LOGI(TAG, "Codec task started");

    uint8_t adpcm_chunk[512];
    bool first_frame = true;

    while (started)
    {

        // Idle when not speaking
        if (!speaking || power_saving)
        {
            // Cleanup for next session
            xStreamBufferReset(sb_spk_encoded);
            xStreamBufferReset(sb_spk_pcm);
            codec->reset();
            first_frame = true;

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // BLOCK read ADPCM with timeout
        size_t got = xStreamBufferReceive(
            sb_spk_encoded,
            adpcm_chunk,
            sizeof(adpcm_chunk),
            pdMS_TO_TICKS(50) // 50ms timeout
        );

        // Timeout or underrun - yield and check speaking flag
        if (got != sizeof(adpcm_chunk))
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Reset predictor on first frame of new session ONLY
        if (first_frame)
        {
            codec->reset();
            first_frame = false;
            ESP_LOGI(TAG, "ADPCM decode session started");
        }

        // Decode ADPCM → PCM (1024 samples @ 16kHz = 64ms audio)
        size_t samples = codec->decode(
            adpcm_chunk,
            sizeof(adpcm_chunk),
            spk_pcm_buffer,
            4096 // Buffer for 1024 samples * 2 bytes
        );

        // Write PCM to speaker buffer
        // This may block if buffer is full, which provides backpressure
        if (samples > 0)
        {
            size_t pcm_bytes = samples * sizeof(int16_t);
            xStreamBufferSend(
                sb_spk_pcm,
                reinterpret_cast<uint8_t *>(spk_pcm_buffer),
                pcm_bytes,
                portMAX_DELAY // Block until space available
            );
        }
    }

    ESP_LOGW(TAG, "Codec task ended");
    vTaskDelete(nullptr);
}

// ============================================================================
// SPEAKER task: PCM buffer → I2S output
// Simplified - only handles I2S timing, no decode logic
// I2S clock controls timing naturally
// ============================================================================
void AudioManager::spkTaskLoop()
{
    const size_t PCM_CHUNK_SAMPLES = 1024; // 64ms @ 16kHz
    const size_t PCM_CHUNK_BYTES = PCM_CHUNK_SAMPLES * sizeof(int16_t);
    int16_t pcm_chunk[PCM_CHUNK_SAMPLES];

    bool i2s_started = false;

    while (started)
    {

        // Stop I2S when not speaking
        if (!speaking || power_saving)
        {
            if (i2s_started)
            {
                output->stopPlayback();
                i2s_started = false;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Start I2S on first frame
        if (!i2s_started)
        {
            if (output->startPlayback())
            {
                i2s_started = true;
                ESP_LOGI(TAG, "I2S playback started");
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        // BLOCK read PCM with timeout
        size_t got = xStreamBufferReceive(
            sb_spk_pcm,
            reinterpret_cast<uint8_t *>(pcm_chunk),
            PCM_CHUNK_BYTES,
            pdMS_TO_TICKS(100) // 100ms timeout - allows checking speaking flag
        );

        // Timeout or underrun - yield and loop back
        if (got != PCM_CHUNK_BYTES)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Write to I2S output
        // This BLOCKS naturally according to I2S DMA clock (16kHz = 64ms per frame)
        // NO manual delays - i2s_write() is the only timing source
        output->writePcm(pcm_chunk, PCM_CHUNK_SAMPLES);
    }

    // Clean shutdown
    if (i2s_started)
    {
        output->stopPlayback();
        i2s_started = false;
    }

    ESP_LOGW(TAG, "Speaker task ended");
    vTaskDelete(nullptr);
}
