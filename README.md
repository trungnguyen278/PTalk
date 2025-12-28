# PTalk - ESP32 Voice Assistant Firmware

A modular, event-driven firmware for ESP32-based voice assistant devices with WiFi connectivity, audio I/O, display management, and power optimization.

## 🎯 Features

- **Voice Input/Output**: I2S-based audio capture (INMP441 mic) and playback (MAX98357 speaker)
- **Audio Codecs**: Support for ADPCM and Opus compression
- **Display Management**: ST7789 display driver with animation support, direct rendering via AnimationPlayer (no framebuffer)
- **Network Connectivity**: WiFi and WebSocket client integration
- **Emotion System**: Server-driven emotion codes (WebSocket → `NetworkManager::parseEmotionCode()` → `StateManager` → Display)
- **Power Management**: Battery monitoring with ADC, TP4056 charging detection
- **State Management**: Central event hub with publish-subscribe pattern
- **Multi-threaded**: FreeRTOS task-based architecture
- **Modular Design**: Clean separation between hardware drivers and application logic

## 📋 System Requirements

- **Platform**: ESP32 (ESP-IDF framework)
- **Framework**: ESP-IDF with C++17 support
- **Build Tool**: PlatformIO
- **Monitor Speed**: 115200 baud

## 🏗️ Architecture Overview

PTalk follows a layered, event-driven architecture:

```
AppController (Orchestrator)
        ↑
    StateManager (Event Hub)
        ↑
   Managers (Business Logic)
   ├── AudioManager
   ├── DisplayManager
   ├── NetworkManager
   └── PowerManager
        ↑
    Drivers (Hardware Abstraction)
        ↑
    Hardware (MCU peripherals)
```

### Core Components

| Component | Responsibility |
|-----------|---|
| **AppController** | Central coordinator, translates state & events to actions |
| **StateManager** | Event hub with publish-subscribe for state changes |
| **AudioManager** | Manages microphone capture and speaker playback |
| **DisplayManager** | Controls ST7789 display with animations |
| **NetworkManager** | Handles WiFi and WebSocket connectivity |
| **PowerManager** | Battery monitoring and power strategy |

## 📁 Project Structure

```
PTalk/
├── src/
│   ├── main.cpp                      # Entry point
│   ├── AppController.cpp/hpp          # Main orchestrator
│   ├── config.h                      # Configuration constants
│   ├── system/
│   │   ├── StateManager.cpp/hpp       # Central state hub
│   │   ├── StateTypes.hpp             # State enumerations
│   │   ├── AudioManager.cpp/hpp       # Audio logic
│   │   ├── DisplayManager.cpp/hpp     # Display logic
│   │   ├── NetworkManager.cpp/hpp     # Network logic
│   │   └── PowerManager.cpp/hpp       # Power logic
│   └── CMakeLists.txt
├── lib/
│   ├── audio/
│   │   ├── AudioCodec.hpp             # Abstract codec interface
│   │   ├── AudioInput.hpp/Output.hpp  # Audio I/O abstractions
│   │   ├── I2SAudioInput_INMP441      # INMP441 mic driver
│   │   ├── I2SAudioOutput_MAX98357    # MAX98357 speaker driver
│   │   ├── AdpcmCodec.cpp/hpp         # ADPCM compression
│   │   └── OpusCodec.cpp/hpp          # Opus compression
│   ├── display/
│   │   ├── DisplayDriver.cpp/hpp      # ST7789 low-level driver
│   │   ├── Framebuffer.cpp/hpp        # Offscreen buffer
│   │   ├── AnimationPlayer.cpp/hpp    # Multi-frame animation engine
│   │   └── Font8x8.hpp                # Bitmap font data
│   ├── network/
│   │   ├── WifiService.cpp/hpp        # WiFi connectivity
│   │   ├── WebSocketClient.cpp/hpp    # WebSocket client
│   │   └── web_page.hpp               # Web UI assets
│   └── power/
│       └── Power.cpp/hpp              # Power driver (ADC, GPIO)
├── CMakeLists.txt
├── platformio.ini
├── sdkconfig.esp32dev
├── Software Architecture.md
└── README.md
```

## 🔄 State Management

The system uses a centralized state machine with the following state types:

### Interaction State
- `IDLE` - System ready, no activity
- `TRIGGERED` - Input detected (button, wakeword, VAD)
- `LISTENING` - Capturing audio from microphone
- `PROCESSING` - Waiting for server/AI response
- `SPEAKING` - Playing response audio
- `CANCELLING` - User cancelled interaction
- `MUTED` - Privacy mode (input disabled)
- `SLEEPING` - Low power UX mode

### Connectivity State
- `OFFLINE` - No WiFi connection
- `CONNECTING_WIFI` - WiFi connection in progress
- `WIFI_PORTAL` - AP mode for configuration
- `CONNECTING_WS` - WebSocket connection in progress
- `ONLINE` - Full connectivity established

### Power State
- `NORMAL` - Battery healthy
- `CHARGING` - Device charging
- `FULL_BATTERY` - Fully charged
- `CRITICAL` - Battery critically low (auto deep sleep behavior)
- `ERROR` - Battery fault/disconnected

### System State
- `BOOTING` - Initial startup
- `RUNNING` - Normal operation
- `ERROR` - System fault
- `MAINTENANCE` - Service mode
- `UPDATING_FIRMWARE` - OTA update in progress
- `FACTORY_RESETTING` - Factory reset in progress

## 🚀 Building and Flashing

### Prerequisites
```bash
# Install PlatformIO
pip install platformio

# Ensure ESP-IDF is set up
export IDF_PATH=/path/to/esp-idf
```

### Build
```bash
# Build the project
pio run -e esp32dev

# Build and monitor serial output
pio run -e esp32dev -t monitor
```

### Upload
```bash
# Upload to device
pio run -e esp32dev -t upload

# Upload and open monitor
pio run -e esp32dev -t uploadandmonitor
```

## 🔧 Configuration

Edit config.h to configure:
- Pin assignments for I2S, display, buttons
- WiFi credentials
- Audio buffer sizes
- Display parameters
- Power thresholds

## 📡 Hardware Requirements

### Required Components
- **MCU**: ESP32 (30-pin or 36-pin module)
- **Microphone**: INMP441 (I2S digital audio)
- **Speaker**: MAX98357 amplifier (I2S audio)
- **Display**: ST7789 1.3" LCD (240x240, SPI)
- **Battery**: Li-ion (3.7V nominal) with TP4056 charging circuit
- **WiFi**: Built-in ESP32 WiFi (2.4GHz)

### Pin Mapping (Typical)
Configure in config.h:
- **I2S Audio**: BCLK, LRCLK, DIN (INMP441), DOUT (MAX98357)
- **SPI Display**: MOSI, MISO, CLK, CS, DC, RST
- **Power ADC**: ADC input for battery voltage
- **Charging Detect**: GPIO for TP4056 CHRG/STDBY signals
- **Buttons/Touch**: GPIO for user input

## 📚 Module Details

### Audio System
- **Input**: INMP441 digital microphone via I2S
- **Output**: MAX98357 class-D amplifier via I2S
- **Codecs**: ADPCM (lower bandwidth) and Opus (better quality)
- **Streaming**: Real-time audio capture/playback with codec support

### Display System
- **Driver**: ST7789 SPI interface
- **Resolution**: 240x240 pixels
- **Features**: Direct rendering (AnimationPlayer) — no framebuffer required, animation playback
- **Animations**: RLE-encoded image sequences (see `scripts/convert_assets.py`) 
- **Registered assets (DeviceProfile)**: `neutral`, `idle`, `listening`, `happy`, `sad`, `thinking`, `stun` (others may be added)

### Network System
- **WiFi**: ESP32 native 802.11b/g/n (2.4GHz)
- **WebSocket**: Persistent connection for bidirectional communication
- **Web Portal**: Captive portal for WiFi provisioning

### Emotion System
- **Flow**: Server sends 2-char emotion codes via WebSocket → `NetworkManager::parseEmotionCode()` → `StateManager::setEmotionState()` → `DisplayManager` plays animation
- See `docs/EMOTION_SYSTEM.md` for details and mapping

### Power System
- **Battery Monitoring**: ADC-based voltage measurement
- **Charging Detection**: TP4056 control signals (CHRG, STDBY)
- **Sleep Modes**: Light sleep and deep sleep support
- **Hysteresis**: Smooth battery percentage reporting

## 🧵 Threading Model

- **Core 0**: FreeRTOS + WiFi driver, **Audio MIC & Codec tasks** (mic capture, codec decode/encode pinned to core 0)
- **Core 1**: `AppControllerTask` (priority 4) - Main event loop; `DisplayLoop`/UI task (priority 3); `AudioSpkTask` (speaker playback pinned to core 1)
- **NetworkLoop**: uses `tskNO_AFFINITY` (no fixed core)

Note: Task priorities and core pinning are set in `AppController::start()` / `AudioManager::start()` / `DisplayManager::startLoop()`.

## 🔌 Event Flow

```
Hardware Event
    ↓
Driver detects state change
    ↓
Manager posts state to StateManager
    ↓
StateManager notifies all subscribers
    ↓
AppController receives notification
    ↓
AppController orchestrates response
    ↓
Managers update hardware state
```

## 📝 Implementation Notes

### TODOs
Current status / TODOs:
- ✅ Audio capture and codec pipeline implemented (mic, codec, speaker tasks) — integrate end-to-end streaming tests
- ✅ Audio playback control implemented via `AudioOutput` APIs
- ✅ Emotion parsing (`NetworkManager::parseEmotionCode()`) and `DisplayManager` emotion handling implemented
- ⚠️ Full OTA firmware update: partial support (OTAUpdater implemented, integration tests recommended)
- ⚠️ Configuration management (NVS) — read/write helpers and UI to modify settings
- ⚠️ Advanced touch/button input features and polish
- ⚠️ Sleep/wake logic refinement and edge-case testing
- ⚠️ Mute/unmute functionality (privacy mode) - supported in state but UI/UX polish recommended

### Code Style
- C++17 with Modern C++ idioms
- RAII for resource management
- Smart pointers (`std::unique_ptr`) for memory safety
- FreeRTOS for concurrency
- No raw `new`/`delete`

## 🐛 Known Issues

**Fixed**: Removed undefined `TouchInput` class reference that was causing compilation errors.

See [Software Architecture.md](Software Architecture.md) for detailed architectural documentation.

## 📄 License

[Add your license here]

## 👥 Contributors

Trungnguyen

---

**Status**: Development  
**Last Updated**: December 2025
```
