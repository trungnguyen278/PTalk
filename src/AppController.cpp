#include "AppController.hpp"
#include "esp_log.h"

#include <utility>

static const char* TAG = "AppController";

// ===================== Internal message type for queue =====================
struct AppMessage {
    enum class Type : uint8_t {
        INTERACTION,
        CONNECTIVITY,
        SYSTEM,
        POWER,
        APP_EVENT
    } type;

    // payloads (chỉ dùng field tương ứng với type)
    state::InteractionState interaction_state;
    state::InputSource      interaction_source;

    state::ConnectivityState connectivity_state;
    state::SystemState       system_state;
    state::PowerState        power_state;

    event::AppEvent          app_event;
};

// ===================== Singleton =====================

AppController& AppController::instance() {
    static AppController inst;
    return inst;
}

// ===================== Lifecycle =====================

void AppController::attachModules(std::unique_ptr<DisplayManager> displayIn,
                                  std::unique_ptr<AudioManager> audioIn,
                                  std::unique_ptr<NetworkManager> networkIn,
                                  std::unique_ptr<PowerManager> powerIn,
                                  std::unique_ptr<TouchInput> touchIn)
{
    if (started.load()) {
        ESP_LOGW(TAG, "attachModules called after start; ignoring");
        return;
    }

    display = std::move(displayIn);
    audio   = std::move(audioIn);
    network = std::move(networkIn);
    power   = std::move(powerIn);
    touch   = std::move(touchIn);
}

bool AppController::init() {
    ESP_LOGI(TAG, "AppController init()");

    if (app_queue == nullptr) {
        app_queue = xQueueCreate(16, sizeof(AppMessage));
        if (!app_queue) {
            ESP_LOGE(TAG, "Failed to create app_queue");
            return false;
        }
    }

    if (!display) ESP_LOGW(TAG, "DisplayManager not attached");
    if (!audio)   ESP_LOGW(TAG, "AudioManager not attached");
    if (!network) ESP_LOGW(TAG, "NetworkManager not attached");
    if (!power)   ESP_LOGW(TAG, "PowerManager not attached");
    if (!touch)   ESP_LOGW(TAG, "TouchInput not attached");

    // Subcribes StateManager
    auto& sm = StateManager::instance();

    sub_inter_id = sm.subscribeInteraction(
        [this](state::InteractionState s, state::InputSource src) {
            AppMessage msg{};
            msg.type = AppMessage::Type::INTERACTION;
            msg.interaction_state = s;
            msg.interaction_source = src;
            if (app_queue) {
                xQueueSend(app_queue, &msg, 0);
            }
        }
    );

    sub_conn_id = sm.subscribeConnectivity(
        [this](state::ConnectivityState s) {
            AppMessage msg{};
            msg.type = AppMessage::Type::CONNECTIVITY;
            msg.connectivity_state = s;
            if (app_queue) {
                xQueueSend(app_queue, &msg, 0);
            }
        }
    );

    sub_sys_id = sm.subscribeSystem(
        [this](state::SystemState s) {
            AppMessage msg{};
            msg.type = AppMessage::Type::SYSTEM;
            msg.system_state = s;
            if (app_queue) {
                xQueueSend(app_queue, &msg, 0);
            }
        }
    );

    sub_power_id = sm.subscribePower(
        [this](state::PowerState s) {
            AppMessage msg{};
            msg.type = AppMessage::Type::POWER;
            msg.power_state = s;
            if (app_queue) {
                xQueueSend(app_queue, &msg, 0);
            }
        }
    );

    return true;
}

void AppController::start() {
    if (started.load()) {
        ESP_LOGW(TAG, "AppController already started");
        return;
    }

    started.store(true);

    // ---------------------------------------------------------------------
    // 1) Start the main controller task (state + event dispatcher)
    // ---------------------------------------------------------------------
    BaseType_t res = xTaskCreatePinnedToCore(
        &AppController::controllerTask,
        "AppControllerTask",
        4096,
        this,
        4,
        &app_task,
        1  // core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AppControllerTask");
        started.store(false);
        return;
    }

    // ---------------------------------------------------------------------
    // 2) Start PowerManager trước để theo dõi pin
    // ---------------------------------------------------------------------
    if (power) {
        if (!power->init()) {
            ESP_LOGE(TAG, "PowerManager init failed");
        } else {
            power->start();
        }
    }

    // ---------------------------------------------------------------------
    // 3) Start DisplayManager để hiển thị trạng thái portal
    // ---------------------------------------------------------------------
    if (display) {
        if (!display->isLoopRunning() && !display->startLoop(33, 3, 4096, 1)) {
            ESP_LOGE(TAG, "DisplayManager startLoop failed");
        }
    }

    // ---------------------------------------------------------------------
    // 4) Start NetworkManager (captive portal)
    // ---------------------------------------------------------------------
    if (network) {
        network->start();
    }

    ESP_LOGI(TAG, "AppController started");
}


void AppController::stop() {
    started.store(false);

    if (power) {
        power->stop();   // stop timer
    }

    if (display) {
        display->stopLoop();
    }

    // controllerTask sẽ tự hủy trong processQueue()
    // Net_Task sẽ tự hủy trong uiLoop()

    ESP_LOGW(TAG, "AppController stopping...");
}

// ===================== External actions =====================

void AppController::reboot() {
    ESP_LOGW(TAG, "System reboot requested");
    // TODO: Clean shutdown modules nếu cần
    esp_restart();
}

void AppController::enterSleep() {
    ESP_LOGI(TAG, "Enter sleep requested");
    // TODO: gọi power->enterSleep() hoặc esp_deep_sleep_* nếu bạn muốn
}

void AppController::wake() {
    ESP_LOGI(TAG, "Wake requested");
    // TODO: xử lý wake logic (nếu dùng light sleep)
}

void AppController::factoryReset() {
    ESP_LOGW(TAG, "Factory reset requested");
    // TODO:
    // 1) Xoá NVS config (config->factoryReset()?)
    // 2) set SystemState::FACTORY_RESETTING trong StateManager
    // 3) restart
}

// ===================== Event Posting =====================

void AppController::postEvent(event::AppEvent evt) {
    if (!app_queue) return;

    AppMessage msg{};
    msg.type = AppMessage::Type::APP_EVENT;
    msg.app_event = evt;
    xQueueSend(app_queue, &msg, 0);
}

// ===================== Task & Queue loop =====================

void AppController::controllerTask(void* param) {
    auto* self = static_cast<AppController*>(param);
    self->processQueue();
}

void AppController::processQueue() {
    ESP_LOGI(TAG, "AppController task started");

    AppMessage msg{};
    while (started.load()) {
        if (xQueueReceive(app_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.type) {
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
                    switch (msg.app_event) {
                        case event::AppEvent::USER_BUTTON:
                            // Ví dụ: dùng nút nhấn để trigger voice
                            StateManager::instance().setInteractionState(
                                state::InteractionState::TRIGGERED,
                                state::InputSource::BUTTON
                            );
                            break;
                        case event::AppEvent::WAKEWORD_DETECTED:
                            StateManager::instance().setInteractionState(
                                state::InteractionState::TRIGGERED,
                                state::InputSource::WAKEWORD
                            );
                            break;
                        case event::AppEvent::SERVER_FORCE_LISTEN:
                            StateManager::instance().setInteractionState(
                                state::InteractionState::TRIGGERED,
                                state::InputSource::SERVER_COMMAND
                            );
                            break;
                        case event::AppEvent::SLEEP_REQUEST:
                            enterSleep();
                            break;
                        case event::AppEvent::WAKE_REQUEST:
                            wake();
                            break;
                        case event::AppEvent::CANCEL_REQUEST:
                            StateManager::instance().setInteractionState(
                                state::InteractionState::CANCELLING,
                                state::InputSource::UNKNOWN
                            );
                            break;
                        case event::AppEvent::POWER_LOW:
                            StateManager::instance().setPowerState(state::PowerState::CRITICAL);
                            break;
                        case event::AppEvent::POWER_RECOVER:
                            StateManager::instance().setPowerState(state::PowerState::NORMAL);
                            break;
                        case event::AppEvent::BATTERY_PERCENT_CHANGED:
                            // Có thể dùng để điều chỉnh UX tuỳ theo mức pin
                            if (power && display) {
                                display->setBatteryPercent(power->getPercent());
                            }
                            break;
                            // Hiện tại chưa làm gì
                            break;
                        case event::AppEvent::OTA_BEGIN:
                            StateManager::instance().setSystemState(state::SystemState::UPDATING_FIRMWARE);
                            // TODO: ota->begin();
                            break;
                        case event::AppEvent::OTA_FINISHED:
                            // TODO: ota->finish();
                            StateManager::instance().setSystemState(state::SystemState::RUNNING);
                            break;
                        case event::AppEvent::SERVER_PROCESSING_START:
                            StateManager::instance().setInteractionState(
                                state::InteractionState::PROCESSING,
                                state::InputSource::SERVER_COMMAND
                            );
                            break;
                        case event::AppEvent::SERVER_TTS_END:
                            StateManager::instance().setInteractionState(
                                state::InteractionState::IDLE,
                                state::InputSource::SERVER_COMMAND
                            );
                            break;
                        case event::AppEvent::MIC_STREAM_TIMEOUT:
                            StateManager::instance().setInteractionState(
                                state::InteractionState::CANCELLING,
                                state::InputSource::UNKNOWN
                            );
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

// Flow A: auto từ TRIGGERED → LISTENING
void AppController::onInteractionStateChanged(state::InteractionState s, state::InputSource src) {
    ESP_LOGI(TAG, "Interaction changed: state=%d source=%d", (int)s, (int)src);

    auto& sm = StateManager::instance();

    switch (s) {
        case state::InteractionState::TRIGGERED:
            // 🔁 Auto chuyển sang LISTENING
            // TODO: chuẩn bị audio trước nếu cần (audio->prepareListening(src);)
            sm.setInteractionState(state::InteractionState::LISTENING, src);
            // UI có thể show hiệu ứng "wake"
            if (display) {
                // TODO: display->onTriggered(src);
            }
            break;

        case state::InteractionState::LISTENING:
            if (audio) {
                // TODO: audio->startCaptureAndStream();
            }
            if (display) {
                // TODO: display->showListening(src);
            }
            break;

        case state::InteractionState::PROCESSING:
            if (audio) {
                // TODO: audio->pauseCapture(); (giữ mic nhưng không gửi)
            }
            if (display) {
                // TODO: display->showThinking();
            }
            break;

        case state::InteractionState::SPEAKING:
            if (audio) {
                // TODO: audio->startPlayback();
            }
            if (display) {
                // TODO: display->showSpeaking();
            }
            break;

        case state::InteractionState::CANCELLING:
            if (audio) {
                // TODO: audio->stopAll();
            }
            if (display) {
                // TODO: display->showCancelled();
            }
            // Sau khi cancel → đưa về IDLE
            sm.setInteractionState(state::InteractionState::IDLE, state::InputSource::UNKNOWN);
            break;

        case state::InteractionState::MUTED:
            if (audio) {
                // TODO: audio->setMicMuted(true);
            }
            if (display) {
                // TODO: display->showMutedIcon(true);
            }
            break;

        case state::InteractionState::SLEEPING:
            if (audio) {
                // TODO: audio->enterLowPower();
            }
            if (display) {
                // TODO: display->sleep();
            }
            break;

        case state::InteractionState::IDLE:
        default:
            if (audio) {
                // TODO: audio->stopCapture();
            }
            if (display) {
                // TODO: display->showIdle();
            }
            break;
    }
}

void AppController::onConnectivityStateChanged(state::ConnectivityState s) {
    ESP_LOGI(TAG, "Connectivity changed: %d", (int)s);

    switch (s) {
        case state::ConnectivityState::OFFLINE:
            if (audio) {
                // TODO: audio->stopAll();
            }
            if (display) {
                // TODO: display->showWifiOffline();
            }
            if (network) {
                // TODO: network->startPortalOrReconnect();
            }
            break;

        case state::ConnectivityState::CONNECTING_WIFI:
            if (display) {
                // TODO: display->showWifiConnecting();
            }
            break;

        case state::ConnectivityState::WIFI_PORTAL:
            if (display) {
                // TODO: display->showWifiPortal();
            }
            break;

        case state::ConnectivityState::CONNECTING_WS:
            if (display) {
                // TODO: display->showServerConnecting();
            }
            break;

        case state::ConnectivityState::ONLINE:
            if (display) {
                // TODO: display->showOnline();
            }
            if (audio) {
                // TODO: audio->readyForStream();
            }
            break;
    }
}

void AppController::onSystemStateChanged(state::SystemState s) {
    ESP_LOGI(TAG, "SystemState changed: %d", (int)s);

    switch (s) {
        case state::SystemState::BOOTING:
            if (display) {
                // TODO: display->showBootLogo();
            }
            break;

        case state::SystemState::RUNNING:
            if (display) {
                // TODO: display->showNormal();
            }
            break;

        case state::SystemState::ERROR:
            if (display) {
                // TODO: display->showError();
            }
            if (audio) {
                // TODO: audio->stopAll();
            }
            break;

        case state::SystemState::MAINTENANCE:
            if (display) {
                // TODO: display->showMaintenance();
            }
            break;

        case state::SystemState::UPDATING_FIRMWARE:
            if (display) {
                // TODO: display->showUpdating();
            }
            if (audio) {
                // TODO: audio->stopAll();
            }
            break;

        case state::SystemState::FACTORY_RESETTING:
            if (display) {
                // TODO: display->showFactoryReset();
            }
            break;
    }
}

void AppController::onPowerStateChanged(state::PowerState s) {
    ESP_LOGI(TAG, "PowerState changed: %d", (int)s);

    switch (s) {
        case state::PowerState::NORMAL:
            if (display) {
                // TODO: display->setBrightnessNormal();
            }
            if (audio) {
                // TODO: audio->setPowerSaving(false);
            }
            break;

        case state::PowerState::LOW_BATTERY:
            if (display) {
                // TODO: display->showLowBatteryIcon();
            }
            if (audio) {
                // TODO: audio->limitVolume();
            }
            break;

        case state::PowerState::CHARGING:
            if (display) {
                // TODO: display->showCharging();
            }
            break;

        case state::PowerState::FULL_BATTERY:
            if (display) {
                // TODO: display->showFullBattery();
            }
            break;

        case state::PowerState::POWER_SAVING:
            if (display) {
                // TODO: display->dim();
            }
            if (audio) {
                // TODO: audio->setPowerSaving(true);
            }
            break;

        case state::PowerState::CRITICAL:
            if (display) {
                // TODO: display->showCriticalBattery();
            }
            if (audio) {
                // TODO: audio->stopAll();
            }
            // Có thể gọi enterSleep() hoặc reboot tuỳ chiến lược
            break;
        
        case state::PowerState::ERROR:
            if (display) {
                // TODO: display->showPowerError();
            }
            if (audio) {
                // TODO: audio->stopAll();
            }
            break;
        
    }
}
