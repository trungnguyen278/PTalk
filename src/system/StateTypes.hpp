#pragma once
#include <cstdint>

namespace state
{

    // ---------- Interaction (User → Voice UX / Input trigger) ----------
    enum class InteractionState : uint8_t
    {
        IDLE,       // ready, nothing active
        TRIGGERED,  // input detected (VAD, button, wakeword)
        LISTENING,  // mic capturing + upstream enabled
        PROCESSING, // waiting server AI / LLM / ASR / TTS
        SPEAKING,   // speaker output
        CANCELLING, // cancel by user / timeout
        MUTED,      // input disabled (privacy mode)
        SLEEPING    // system UX off but alive
    };

    // ---------- Connectivity (WiFi / Websocket) ----------
    enum class ConnectivityState : uint8_t
    {
        OFFLINE,         // No network
        CONNECTING_WIFI, // Trying to connect to WiFi
        WIFI_PORTAL,     // Captive portal mode
        CONNECTING_WS,   // Connecting to WebSocket server
        ONLINE           // Connected and operational
    };

    // ---------- System ----------
    enum class SystemState : uint8_t
    {
        BOOTING,
        RUNNING,
        ERROR,
        MAINTENANCE,
        UPDATING_FIRMWARE, // OTA state future
        FACTORY_RESETTING
    };

    // ---------- Power ----------
    enum class PowerState : uint8_t
    {
        NORMAL,
        // LOW_BATTERY,
        CHARGING,
        FULL_BATTERY,
        POWER_SAVING, // dim display / disable speaker
        CRITICAL,     // force shutdown
        ERROR         // battery disconnected / fault
    };

    // ---------- Input Source (what triggered the interaction) ----------
    enum class InputSource : uint8_t
    {
        VAD,            // Voice Activity Detection
        BUTTON,         // Physical button
        WAKEWORD,       // Wakeword detected
        SERVER_COMMAND, // remote trigger
        SYSTEM,         // system-triggered (e.g. auto-listen)
        UNKNOWN         // fallback
    };

    // ---------- Emotion (from WebSocket server message codes) ----------
    // Server sends emotion codes like "01", "11", etc. during SPEAKING phase
    // Maps to UI animations and voice characteristics
    enum class EmotionState : uint8_t
    {
        NEUTRAL,    // Default, natural expression (00, no emotion code)
        HAPPY,      // Cheerful, friendly (e.g., "01")
        SAD,        // Empathetic, concerned (e.g., "11")
        ANGRY,      // Alert, urgent (e.g., "02")
        CONFUSED,   // Uncertain, seeking clarification (e.g., "12")
        EXCITED,    // Surprise, delight, enthusiasm (e.g., "03")
        CALM,       // Soothing, peaceful, reassuring (e.g., "13")
        THINKING    // Processing state (internal use)
    };

} // namespace state
