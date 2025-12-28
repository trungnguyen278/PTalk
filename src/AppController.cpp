#include "AppController.hpp"
#include "system/NetworkManager.hpp"
#include "system/AudioManager.hpp"
#include "system/DisplayManager.hpp"
#include "system/PowerManager.hpp"
#include "../../lib/touch/TouchInput.hpp"
#include "system/OTAUpdater.hpp"

#include "esp_log.h"

#include <utility>

static const char *TAG = "AppController";

// ===================== Internal message type for queue =====================
struct AppMessage
{
    enum class Type : uint8_t
    {
        INTERACTION,
        CONNECTIVITY,
        SYSTEM,
        POWER,
        APP_EVENT
    } type;

    // payloads (chỉ dùng field tương ứng với type)
    state::InteractionState interaction_state;
    state::InputSource interaction_source;

    state::ConnectivityState connectivity_state;
    state::SystemState system_state;
    state::PowerState power_state;

    event::AppEvent app_event;
};

// ===================== Emotion parsing =====================

state::EmotionState AppController::parseEmotionCode(const std::string &code)
{
    // Emotion parsing moved to NetworkManager::parseEmotionCode()
    // This function kept for backward compatibility if needed
    return NetworkManager::parseEmotionCode(code);
}

// ===================== Singleton =====================

AppController &AppController::instance()
{
    static AppController inst;
    return inst;
}

// ===================== Lifecycle =====================

AppController::~AppController()
{
    stop();

    // Unsubscribe from StateManager to avoid callbacks after destruction
    auto &sm = StateManager::instance();
    if (sub_inter_id != -1)
        sm.unsubscribeInteraction(sub_inter_id);
    if (sub_conn_id != -1)
        sm.unsubscribeConnectivity(sub_conn_id);
    if (sub_sys_id != -1)
        sm.unsubscribeSystem(sub_sys_id);
    if (sub_power_id != -1)
        sm.unsubscribePower(sub_power_id);

    if (app_queue)
    {
        vQueueDelete(app_queue);
        app_queue = nullptr;
    }
}

void AppController::attachModules(std::unique_ptr<DisplayManager> displayIn,
                                  std::unique_ptr<AudioManager> audioIn,
                                  std::unique_ptr<NetworkManager> networkIn,
                                  std::unique_ptr<PowerManager> powerIn,
                                  std::unique_ptr<TouchInput> touchIn,
                                  std::unique_ptr<OTAUpdater> otaIn)
{
    if (started.load())
    {
        ESP_LOGW(TAG, "attachModules called after start; ignoring");
        return;
    }

    display = std::move(displayIn);
    audio = std::move(audioIn);
    network = std::move(networkIn);
    power = std::move(powerIn);
    touch = std::move(touchIn);
    ota = std::move(otaIn);
}

bool AppController::init()
{
    ESP_LOGI(TAG, "AppController init()");

    if (app_queue == nullptr)
    {
        app_queue = xQueueCreate(16, sizeof(AppMessage));
        if (!app_queue)
        {
            ESP_LOGE(TAG, "Failed to create app_queue");
            return false;
        }
    }

    if (!display)
        ESP_LOGW(TAG, "DisplayManager not attached");
    if (!audio)
        ESP_LOGW(TAG, "AudioManager not attached");
    if (!network)
        ESP_LOGW(TAG, "NetworkManager not attached");
    if (!power)
        ESP_LOGW(TAG, "PowerManager not attached");
    if (!touch)
        ESP_LOGW(TAG, "TouchInput not attached");
    if (!ota)
        ESP_LOGW(TAG, "OTAUpdater not attached");

    // ======================================================================
    // SUBSCRIPTION ARCHITECTURE (Enterprise Pattern):
    // ======================================================================
    // AppController mediates state changes for CONTROL LOGIC only:
    //   onInteractionStateChanged  → TRIGGERED→LISTENING transition, cancel logic
    //   onConnectivityStateChanged → audio streaming enable/disable
    //   onSystemStateChanged       → error handling, audio stop
    //   onPowerStateChanged        → network/audio power down, sleep
    //
    // Other managers subscribe directly for their concerns:
    //   DisplayManager   → Handles ALL UI (InteractionState, ConnectivityState, SystemState, PowerState)
    //   AudioManager     → Handles audio state machine (InteractionState)
    //   NetworkManager   → (no subscription; explicit method calls)
    //
    // Benefits:
    // ✅ No cross-cutting concerns (UI logic stays in Display, audio in Audio)
    // ✅ Deterministic: all notifications go through single queue
    // ✅ Testable: mock StateManager and verify queue messages
    // ======================================================================

    auto &sm = StateManager::instance();

    sub_inter_id = sm.subscribeInteraction(
        [this](state::InteractionState s, state::InputSource src)
        {
            AppMessage msg{};
            msg.type = AppMessage::Type::INTERACTION;
            msg.interaction_state = s;
            msg.interaction_source = src;
            if (app_queue)
            {
                xQueueSend(app_queue, &msg, 0);
            }
        });

    sub_conn_id = sm.subscribeConnectivity(
        [this](state::ConnectivityState s)
        {
            AppMessage msg{};
            msg.type = AppMessage::Type::CONNECTIVITY;
            msg.connectivity_state = s;
            if (app_queue)
            {
                xQueueSend(app_queue, &msg, 0);
            }
        });

    sub_sys_id = sm.subscribeSystem(
        [this](state::SystemState s)
        {
            AppMessage msg{};
            msg.type = AppMessage::Type::SYSTEM;
            msg.system_state = s;
            if (app_queue)
            {
                xQueueSend(app_queue, &msg, 0);
            }
        });

    sub_power_id = sm.subscribePower(
        [this](state::PowerState s)
        {
            AppMessage msg{};
            msg.type = AppMessage::Type::POWER;
            msg.power_state = s;
            if (app_queue)
            {
                xQueueSend(app_queue, &msg, 0);
            }
        });

    return true;
}

void AppController::start()
{
    if (started.load())
    {
        ESP_LOGW(TAG, "AppController already started");
        return;
    }

    started.store(true);

    // ⚠️  CRITICAL ORDER: Task must be running BEFORE any module triggers state changes
    // Otherwise state notifications arrive at app_queue before it's ready

    // 1️⃣ Start the main controller task FIRST (must be ready before other modules post)
    BaseType_t res = xTaskCreatePinnedToCore(
        &AppController::controllerTask,
        "AppControllerTask",
        4096,
        this,
        4,
        &app_task,
        1 // core 1
    );

    if (res != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create AppControllerTask");
        started.store(false);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Ensure task is running before modules start

    // 2️⃣ Start PowerManager to monitor battery--
    if (power)
    {
        if (!power->init())
        {
            ESP_LOGE(TAG, "PowerManager init failed");
        }
        else
        {
            power->start();
            // Lấy mẫu ngay để biết trạng thái pin sớm
            power->sampleNow();

            // Check if waking from deep sleep due to battery check
            // esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            // if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
            //     ESP_LOGI(TAG, "Woke from deep sleep - checking battery");
            //     auto ps = StateManager::instance().getPowerState();
            //     if (ps == state::PowerState::CRITICAL || ps == state::PowerState::LOW_BATTERY) {
            //         ESP_LOGW(TAG, "Battery still low/critical - going back to sleep");
            //         vTaskDelay(pdMS_TO_TICKS(1000));
            //         enterSleep();
            //         // Will not reach here
            //     } else {
            //         ESP_LOGI(TAG, "Battery recovered to acceptable level - continuing boot");
            //     }
            // }
        }
    }

    // ---------------------------------------------------------------------
    // 3) Start DisplayManager để hiển thị trạng thái portal
    // ---------------------------------------------------------------------
    if (display)
    {
        if (!display->isLoopRunning() && !display->startLoop(33, 3, 4096, 1))
        {
            ESP_LOGE(TAG, "DisplayManager startLoop failed");
        }
    }

    // ---------------------------------------------------------------------
    // 4) Start NetworkManager nếu pin không thấp/critical
    // ---------------------------------------------------------------------
    if (network)
    {
        auto ps = StateManager::instance().getPowerState();
        if (/*ps == state::PowerState::LOW_BATTERY || */ ps == state::PowerState::CRITICAL)
        {
            ESP_LOGW(TAG, "Skipping NetworkManager start due to low battery");
        }
        else
        {
            network->start();
        }
    }
    // ---------------------------------------------------------------------
    // 5) Start AudioManager cuối cùng (cần network để stream)
    // ---------------------------------------------------------------------
    if (audio)
    {
        auto ps = StateManager::instance().getPowerState();
        if (/*ps == state::PowerState::LOW_BATTERY || */ ps == state::PowerState::CRITICAL)
        {
            ESP_LOGW(TAG, "Skipping AudioManager start due to low battery");
        }
        else
        {
            audio->start();
        }
    }
    // ---------------------------------------------------------------------
    // 6) TouchInput start
    // ---------------------------------------------------------------------
    if (touch)
    {
        auto ps = StateManager::instance().getPowerState();
        if (/*ps == state::PowerState::LOW_BATTERY || */ ps == state::PowerState::CRITICAL)
        {
            ESP_LOGW(TAG, "Skipping TouchInput start due to low battery");
        }
        else
        {
            touch->start();
        }
    }

    ESP_LOGI(TAG, "AppController started");
}

void AppController::stop()
{
    started.store(false);

    ESP_LOGI(TAG, "AppController stopping (reverse startup order)...");

    // Stop modules in REVERSE order of startup
    // Startup: PowerManager → DisplayManager → NetworkManager → AudioManager
    // Shutdown: AudioManager → NetworkManager → DisplayManager → PowerManager

    if (network)
    {
        network->stopPortal();
        network->stop();
        ESP_LOGD(TAG, "NetworkManager stopped");
    }

    if (audio)
    {
        audio->stop();
        ESP_LOGD(TAG, "AudioManager stopped");
    }

    if (display)
    {
        display->stopLoop();
        ESP_LOGD(TAG, "DisplayManager stopped");
    }

    if (power)
    {
        power->stop();
        ESP_LOGD(TAG, "PowerManager stopped");
    }

    // Wait for controllerTask to exit
    vTaskDelay(pdMS_TO_TICKS(100));
    if (app_task)
    {
        app_task = nullptr;
    }

    ESP_LOGI(TAG, "AppController stopped");
}

// ===================== External actions =====================

void AppController::reboot()
{
    ESP_LOGW(TAG, "System reboot requested");
    // TODO: Clean shutdown modules nếu cần
    esp_restart();
}

void AppController::enterSleep()
{
    // ✅ Guard against re-entrance
    if (sleeping.exchange(true))
    {
        ESP_LOGW(TAG, "enterSleep() already in progress");
        return;
    }

    ESP_LOGI(TAG, "Entering deep sleep due to critical battery");

    // Stop all modules before deep sleep
    if (network)
    {
        network->stopPortal();
        network->stop();
    }
    if (audio)
    {
        audio->stop();
    }
    if (display)
    {
        // Keep last frame visible briefly; turn off BL just before sleep
        display->stopLoop();
        // Delay to show last frame
        vTaskDelay(pdMS_TO_TICKS(5000));
        display->setBacklight(false);
    }

    // Wake up periodically to check battery
    const uint64_t wakeup_time_us = static_cast<uint64_t>(config_.deep_sleep_wakeup_sec) * 1000000ULL;
    esp_sleep_enable_timer_wakeup(wakeup_time_us);

    ESP_LOGI(TAG, "Configured to wake in %us to check battery", static_cast<unsigned>(config_.deep_sleep_wakeup_sec));
    esp_deep_sleep_start(); // DOES NOT RETURN
}

void AppController::wake()
{
    ESP_LOGI(TAG, "Wake requested");
    // TODO: xử lý wake logic (nếu dùng light sleep)
}

void AppController::factoryReset()
{
    ESP_LOGW(TAG, "Factory reset requested");
    // TODO:
    // 1) Xoá NVS config (config->factoryReset()?)
    // 2) set SystemState::FACTORY_RESETTING trong StateManager
    // 3) restart
}

void AppController::setConfig(const Config &cfg)
{
    config_ = cfg;
}

// ===================== Event Posting =====================

void AppController::postEvent(event::AppEvent evt)
{
    if (!app_queue)
        return;

    AppMessage msg{};
    msg.type = AppMessage::Type::APP_EVENT;
    msg.app_event = evt;
    xQueueSend(app_queue, &msg, 0);
}

// ===================== Task & Queue loop =====================

void AppController::controllerTask(void *param)
{
    auto *self = static_cast<AppController *>(param);
    self->processQueue();
}

void AppController::processQueue()
{
    ESP_LOGI(TAG, "AppController task started");

    AppMessage msg{};
    while (started.load())
    {
        if (xQueueReceive(app_queue, &msg, portMAX_DELAY) == pdTRUE)
        {
            switch (msg.type)
            {
            case AppMessage::Type::INTERACTION:
                onInteractionStateChanged(msg.interaction_state, msg.interaction_source);
                break;
            case AppMessage::Type::CONNECTIVITY:
                onConnectivityStateChanged(msg.connectivity_state);
                break;
            case AppMessage::Type::SYSTEM:
                onSystemStateChanged(msg.system_state);
                break;
            case AppMessage::Type::POWER:
                onPowerStateChanged(msg.power_state);
                break;
            case AppMessage::Type::APP_EVENT:
                // Map AppEvent → state hoặc action
                switch (msg.app_event)
                {
                case event::AppEvent::USER_BUTTON:
                    ESP_LOGI(TAG, "Button Pressed -> Start Listening");
                    // Chuyển thẳng sang LISTENING (hoặc TRIGGERED nếu muốn có tiếng Beep trước)
                    StateManager::instance().setInteractionState(
                        state::InteractionState::LISTENING,
                        state::InputSource::BUTTON);
                    break;
                case event::AppEvent::WAKEWORD_DETECTED:
                    StateManager::instance().setInteractionState(
                        state::InteractionState::TRIGGERED,
                        state::InputSource::WAKEWORD);
                    break;
                case event::AppEvent::SERVER_FORCE_LISTEN:
                    StateManager::instance().setInteractionState(
                        state::InteractionState::TRIGGERED,
                        state::InputSource::SERVER_COMMAND);
                    break;
                case event::AppEvent::SLEEP_REQUEST:
                    enterSleep();
                    break;
                case event::AppEvent::WAKE_REQUEST:
                    wake();
                    break;
                case event::AppEvent::RELEASE_BUTTON:
                    StateManager::instance().setInteractionState(
                        state::InteractionState::IDLE,
                        state::InputSource::BUTTON);
                    break;
                case event::AppEvent::BATTERY_PERCENT_CHANGED:
                    // ✅ Removed: DisplayManager.update() queries power directly
                    break;
                case event::AppEvent::OTA_BEGIN:
                    // ✅ Only set state; DisplayManager subscribes and handles UI
                    StateManager::instance().setSystemState(state::SystemState::UPDATING_FIRMWARE);

                    if (network)
                    {
                        // Request firmware from server
                        network->onFirmwareChunk([this](const uint8_t *data, size_t size)
                                                 {
                                    if (ota) {
                                        ota->writeChunk(data, size);
                                    } });

                        network->onFirmwareComplete([this](bool success, const std::string &msg)
                                                    {
                                    if (success) {
                                        postEvent(event::AppEvent::OTA_FINISHED);
                                    } else {
                                        StateManager::instance().setSystemState(state::SystemState::ERROR);
                                    } });

                        if (!network->requestFirmwareUpdate())
                        {
                            StateManager::instance().setSystemState(state::SystemState::ERROR);
                        }
                    }
                    break;
                case event::AppEvent::OTA_FINISHED:
                    if (ota && ota->isUpdating())
                    {
                        // TODO: ota->finish();
                        if (ota->finishUpdate())
                        {
                            if (display)
                            {
                                display->showOTACompleted();
                            }
                            // Schedule reboot after a delay
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            if (display)
                            {
                                display->showRebooting();
                            }
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            reboot();
                        }
                        else
                        {
                            if (display)
                            {
                                display->showOTAError("Update validation failed");
                            }
                            StateManager::instance().setSystemState(state::SystemState::ERROR);
                        }
                    }
                    else
                    {
                        if (display)
                        {
                            display->showOTAError("No update in progress");
                        }
                        StateManager::instance().setSystemState(state::SystemState::ERROR);
                    }
                    break;
                }
                break;
            }
        }
    }

    ESP_LOGW(TAG, "AppController task stopping");
    vTaskDelete(nullptr);
}

// NetworkManager now owns its own update task
// ===================== State callbacks logic =====================
//
// IMPORTANT: These handlers execute in AppController task context (safe for queue operations)
// DisplayManager and AudioManager handle their own concerns in parallel via direct subscription
// AppController only handles cross-cutting control logic here
//
// NOTE: AppController no longer handles UI state changes.
// - InteractionState → AudioManager subscribes (controls audio)
// - ConnectivityState → DisplayManager subscribes (shows UI)
// - SystemState → DisplayManager subscribes (shows UI)
// - PowerState → DisplayManager subscribes (shows UI)

// Flow A: auto từ TRIGGERED → LISTENING
void AppController::onInteractionStateChanged(state::InteractionState s, state::InputSource src)
{
    ESP_LOGI(TAG, "Interaction changed: state=%d source=%d", (int)s, (int)src);

    auto &sm = StateManager::instance();

    // DisplayManager subscribes InteractionState directly and handles UI
    // AppController handles only control logic (audio/device commands)

    switch (s)
    {
    case state::InteractionState::TRIGGERED:
        // 🔁 Auto chuyển sang LISTENING
        sm.setInteractionState(state::InteractionState::LISTENING, src);
        break;

    case state::InteractionState::LISTENING:
        // Audio will auto-subscribe InteractionState changes
        break;

    case state::InteractionState::PROCESSING:
        // Pause capture (audio will handle via subscription)
        break;

    case state::InteractionState::SPEAKING:
        // Audio will handle via subscription
        break;

    case state::InteractionState::CANCELLING:
        // Sau khi cancel → đưa về IDLE
        sm.setInteractionState(state::InteractionState::IDLE, state::InputSource::UNKNOWN);
        break;

    case state::InteractionState::MUTED:
    case state::InteractionState::SLEEPING:
    case state::InteractionState::IDLE:
    default:
        break;
    }
}

void AppController::onConnectivityStateChanged(state::ConnectivityState s)
{
    ESP_LOGI(TAG, "Connectivity changed: %d", (int)s);

    // DisplayManager subscribes ConnectivityState directly and handles UI
    // AppController handles only control logic

    switch (s)
    {
    case state::ConnectivityState::OFFLINE:
        if (audio)
        {
            // TODO: audio->stopStreaming();
        }
        break;

    case state::ConnectivityState::ONLINE:
        if (audio)
        {
            // TODO: audio->readyForStream();
        }
        break;

    case state::ConnectivityState::CONNECTING_WIFI:
    case state::ConnectivityState::WIFI_PORTAL:
    case state::ConnectivityState::CONNECTING_WS:
    default:
        break;
    }
}

void AppController::onSystemStateChanged(state::SystemState s)
{
    ESP_LOGI(TAG, "SystemState changed: %d", (int)s);

    // DisplayManager subscribes SystemState directly and handles UI
    // AppController handles only control logic

    switch (s)
    {
    case state::SystemState::ERROR:
        if (audio)
        {
            // TODO: audio->stopAll();
        }
        break;

    case state::SystemState::UPDATING_FIRMWARE:
        if (audio)
        {
            // TODO: audio->stopAll();
        }
        break;

    case state::SystemState::BOOTING:
    case state::SystemState::RUNNING:
    case state::SystemState::MAINTENANCE:
    case state::SystemState::FACTORY_RESETTING:
    default:
        break;
    }
}

void AppController::onPowerStateChanged(state::PowerState s)
{
    ESP_LOGI(TAG, "PowerState changed: %d", (int)s);

    switch (s)
    {
    case state::PowerState::NORMAL:
        if (audio)
        {
            // TODO: audio->setPowerSaving(false);
            audio->start();
        }
        // Khôi phục network nếu trước đó đã dừng
        if (network)
        {
            network->start();
        }
        if (touch)
        {
            touch->start();
        }
        break;

        // case state::PowerState::LOW_BATTERY:
        //     if (audio) {
        //         // TODO: audio->limitVolume();
        //     }
        //     // Dừng mọi task nặng (WS/Portal/STA)
        //     if (network) {
        //         network->stopPortal();
        //         network->stop();
        //     }
        //     break;

    case state::PowerState::CHARGING:
        break;

    case state::PowerState::FULL_BATTERY:
        if (network)
        {
            network->start();
        }
        break;

        // case state::PowerState::POWER_SAVING:
        //     if (audio) {
        //         // TODO: audio->setPowerSaving(true);
        //     }
        //     break;

    case state::PowerState::CRITICAL:
        if (audio)
        {
            audio->stop();
        }
        if (network)
        {
            network->stopPortal();
            network->stop();
        }
        if (touch)
        {
            touch->stop();
        }
        // ✅ Auto-sleep on critical battery
        ESP_LOGW(TAG, "Critical battery detected - entering deep sleep");
        enterSleep(); // Does not return
        break;

    case state::PowerState::ERROR:
        if (audio)
        {
            audio->stop();
        }
        break;
    }
}
