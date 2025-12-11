#pragma once
#include <cstdint>

namespace state {

// ---------- Interaction (User → Voice UX / Input trigger) ----------
enum class InteractionState : uint8_t {
    IDLE,               // ready, nothing active
    TRIGGERED,          // input detected (VAD, button, wakeword)
    LISTENING,          // mic capturing + upstream enabled
    PROCESSING,         // waiting server AI / LLM / ASR / TTS
    SPEAKING,           // speaker output
    CANCELLING,         // cancel by user / timeout
    MUTED,              // input disabled (privacy mode)
    SLEEPING            // system UX off but alive
};

// ---------- Connectivity (WiFi / Websocket) ----------
enum class ConnectivityState : uint8_t {
    OFFLINE,
    CONNECTING_WIFI,
    WIFI_PORTAL,
    CONNECTING_WS,
    ONLINE
};

// ---------- System ----------
enum class SystemState : uint8_t {
    BOOTING,
    RUNNING,
    ERROR,
    MAINTENANCE,
    UPDATING_FIRMWARE,     // OTA state future
    FACTORY_RESETTING
};

// ---------- Power ----------
enum class PowerState : uint8_t {
    NORMAL,
    LOW_BATTERY,
    CHARGING,
    FULL_BATTERY,
    POWER_SAVING,          // dim display / disable speaker
    CRITICAL,               // force shutdown
    ERROR                   // battery disconnected / fault
};

// ---------- Input Source (what triggered the interaction) ----------
enum class InputSource : uint8_t {
    VAD,                   // Voice Activity Detection
    BUTTON,                // Physical button
    WAKEWORD,           // Wakeword detected    
    SERVER_COMMAND,         // remote trigger
    UNKNOWN                 // fallback
};

} // namespace state
