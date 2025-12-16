#pragma once
#include <memory>
#include <atomic>
#include "system/StateManager.hpp"
#include "system/StateTypes.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Optional External Messages / Intent / Commands (Key future extensibility)
namespace event
{
    enum class AppEvent : uint8_t
    {
        USER_BUTTON,             // UI physical press
        WAKEWORD_DETECTED,       // Wakeword engine triggers
        SERVER_FORCE_LISTEN,     // Remote control command
        SERVER_PROCESSING_START, // Server tells device to start processing
        SERVER_TTS_END,          // Server finished TTS playback
        MIC_STREAM_TIMEOUT,      // Mic stream idle timeout
        OTA_BEGIN,               // Server indicates firmware update available
        OTA_DOWNLOAD_START,      // Server has firmware, start download
        OTA_DOWNLOAD_CHUNK,      // Receiving firmware chunk from server
        OTA_DOWNLOAD_COMPLETE,   // Download finished, ready to write
        OTA_FINISHED,            // OTA process finished (success or fail)
        POWER_LOW,               // Battery low warning
        POWER_RECOVER,           // Battery recovered from low
        BATTERY_PERCENT_CHANGED, // Battery percentage changed
        CANCEL_REQUEST,          // User requests to cancel current interaction
        SLEEP_REQUEST,           // Request to enter sleep mode
        WAKE_REQUEST             // Request to wake from sleep mode
    };
}

class NetworkManager; // WiFi + WebSocket
class AudioManager;   // Mic / Speaker
class DisplayManager; // UI / Animation
class PowerManager;   // Battery & power strategy
class TouchInput;     // Buttons or Touch Sensor
class OTAUpdater;     // Firmware update
// class ConfigManager;       // Configuration, NVS

/**
 * Class: AppController
 * Author: Trung Nguyen
 * Email: Trung.nt20271@gmail.com
 * Date: 10 Dec 2025
 *
 * Description:
 * - Trung tâm điều phối ứng dụng chính
 * - Quản lý vòng lặp chính, nhận sự kiện từ các module
 * - Cập nhật trạng thái hệ thống qua StateManager
 * - Xử lý các sự kiện ứng dụng (AppEvent)
 * - Điều phối các module: NetworkManager, AudioManager, DisplayManager, PowerManager, TouchInput, OTAUpdater
 * - Cung cấp API bên ngoài để khởi động, tắt, reset, sleep, wake, reboot
 * - Được thiết kế theo mô hình singleton để đảm bảo chỉ có một instance duy nhất
 * - Sử dụng hàng đợi FreeRTOS để xử lý bất đồng bộ các sự kiện
 * - Đăng ký callback với StateManager để phản hồi thay đổi trạng thái
 * - Được khởi tạo và cấu hình thông qua dependency injection của các module con
 */
class AppController
{
public:
    static AppController &instance();

    // ======= Lifecycle control =======
    bool init();    // create modules, subscribe state callback
    void start();   // create task
    void stop();    // future

    // ======= External Actions =======
    void reboot();
    void enterSleep();
    void wake();
    void factoryReset();

    // ======= Post application-level event to queue =======
    /**
     * Post an application event to the internal queue
     * @param evt Event to post
     */
    void postEvent(event::AppEvent evt);

    // ======= Dependency injection =======
    void attachModules(std::unique_ptr<DisplayManager> displayIn,
                       std::unique_ptr<AudioManager> audioIn,
                       std::unique_ptr<NetworkManager> networkIn,
                       std::unique_ptr<PowerManager> powerIn,
                       std::unique_ptr<TouchInput> touchIn,
                       std::unique_ptr<OTAUpdater> otaIn);

private:
    AppController() = default;
    ~AppController() = default;
    AppController(const AppController &) = delete;
    AppController &operator=(const AppController &) = delete;

    // ======= Task Loop =======
    // Controller Task
    static void controllerTask(void *param);
    void processQueue();

    // ======= State callbacks =======
    void onInteractionStateChanged(state::InteractionState, state::InputSource);
    void onConnectivityStateChanged(state::ConnectivityState);
    void onSystemStateChanged(state::SystemState);
    void onPowerStateChanged(state::PowerState);

private:
    // ======= Subscription ID =======
    int sub_inter_id = -1;
    int sub_conn_id = -1;
    int sub_sys_id = -1;
    int sub_power_id = -1;

    // ======= Module pointers =======
    std::unique_ptr<NetworkManager> network;
    std::unique_ptr<AudioManager> audio;
    std::unique_ptr<DisplayManager> display;
    std::unique_ptr<PowerManager> power;
    // std::unique_ptr<ConfigManager> config;
    std::unique_ptr<OTAUpdater> ota;
    std::unique_ptr<TouchInput> touch;

    // ======= Task & Queue =======
    QueueHandle_t app_queue = nullptr;
    TaskHandle_t app_task = nullptr;

    // ======= Internal state =======
    std::atomic<bool> started{false};
};
