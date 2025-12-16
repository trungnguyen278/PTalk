#pragma once
#include <memory>
#include <atomic>
#include "system/StateManager.hpp"
#include "system/StateTypes.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "system/DisplayManager.hpp"
#include "system/PowerManager.hpp"
#include "system/NetworkManager.hpp"
#include "system/AudioManager.hpp"
#include "../../lib/touch/TouchInput.hpp"

// Optional External Messages / Intent / Commands (Key future extensibility)
namespace event {
enum class AppEvent : uint8_t {
    USER_BUTTON,               // UI physical press
    WAKEWORD_DETECTED,         // Wakeword engine triggers
    SERVER_FORCE_LISTEN,       // Remote control command
    SERVER_PROCESSING_START,
    SERVER_TTS_END,
    MIC_STREAM_TIMEOUT,
    OTA_BEGIN,
    OTA_FINISHED,
    POWER_LOW,
    POWER_RECOVER,
    BATTERY_PERCENT_CHANGED,
    CANCEL_REQUEST,
    SLEEP_REQUEST,
    WAKE_REQUEST
};
}

// class NetworkManager;      // WiFi + WebSocket
// class AudioManager;        // Mic / Speaker
// class DisplayManager;     // UI / Animation
// class PowerManager;        // Battery & power strategy
// class TouchInput;          // Buttons or Touch Sensor
// class OTAUpdater;          // Firmware update
// class ConfigManager;       // Configuration, NVS

/**
 * AppController:
 * - Central coordinator
 * - Translate State + Event to Actions
 * - Completely independent from hardware driver
 */
class AppController {
public:
    static AppController& instance();

    // ======= Lifecycle control =======
    bool init();     // create modules, subscribe state callback
    void start();    // create task
    void stop();     // future

    // ======= External Actions =======
    void reboot();
    void enterSleep();
    void wake();
    void factoryReset();

    // ======= Post application-level event to queue =======
    void postEvent(event::AppEvent evt);

    // ======= Dependency injection =======
    void attachModules(std::unique_ptr<DisplayManager> displayIn,
                       std::unique_ptr<AudioManager> audioIn,
                       std::unique_ptr<NetworkManager> networkIn,
                       std::unique_ptr<PowerManager> powerIn,
                       std::unique_ptr<TouchInput> touchIn);

private:
    AppController() = default;
    ~AppController() = default;
    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // ======= Task Loop =======
        // Controller Task
    static void controllerTask(void* param);
    void processQueue();

        // UI Task
    // NetworkManager runs its own task; no UI/network tick task here


    //TODO: thêm các task khác nếu cần

    // ======= State callbacks =======
    void onInteractionStateChanged(state::InteractionState, state::InputSource);
    void onConnectivityStateChanged(state::ConnectivityState);
    void onSystemStateChanged(state::SystemState);
    void onPowerStateChanged(state::PowerState);

private:
    // ======= Subscription ID =======
    int sub_inter_id = -1;
    int sub_conn_id = -1;
    int sub_sys_id  = -1;
    int sub_power_id = -1;

    // ======= Module pointers =======
    std::unique_ptr<NetworkManager> network;
    std::unique_ptr<AudioManager> audio;
    std::unique_ptr<DisplayManager> display;
    std::unique_ptr<PowerManager> power;
    //std::unique_ptr<ConfigManager> config;
    //std::unique_ptr<OTAUpdater> ota;
    std::unique_ptr<TouchInput> touch;

    // ======= Task & Queue =======
    QueueHandle_t app_queue = nullptr;
    TaskHandle_t app_task = nullptr;

    // ======= Internal state =======
    std::atomic<bool> started {false};
};
