# 🧩 **Software Architecture.md**

# PTalk Embedded Firmware Architecture

*Version 1.0 — ESP-IDF Architecture Overview*

---

## 1. Overview

PTalk Firmware follows a **modular, event-driven architecture** inspired by modern IoT and voice assistant devices (Google Home, Alexa, ESP RainMaker).

Hệ thống được chia thành các tầng rõ ràng:

```
Hardware → Drivers → Managers → StateHub → AppController → UI/Audio/Network
```

Mục tiêu chính:

* Dễ mở rộng (scalable)
* Tách biệt giao tiếp phần cứng và logic (clean layering)
* Ít phụ thuộc (loose coupling)
* Đồng bộ state qua một hub trung tâm (StateManager)
* Đảm bảo thread-safe, không race condition
* Dễ test từng module

---

## 2. Layered Architecture

```
┌───────────────────────────────┐
│          AppController         │
│ (Orchestrator / Decision Flow) │
└──────────────▲────────────────┘
               │ State Messages
┌──────────────┴────────────────┐
│          StateManager          │
│  (State hub + publish/subscribe)│
└──────────────▲────────────────┘
         Notify │
         ┌──────┴──────────────┐
┌─────────────────┐   ┌───────────────────┐
│ PowerManager     │   │ NetworkManager    │
│ (logic)          │   │ AudioManager      │
│ DisplayAnimator  │   │ TouchInput        │
└──────▲───────────┘   └──────────▲────────┘
       │ Data                     │ Data/Input
┌──────┴───────────┐        ┌─────┴───────────┐
│ Power (Driver)    │        │ WiFi/WS Drivers │
│ Audio HAL         │        │ ADC, GPIO, I2S  │
└──────┬────────────┘        └─────┬───────────┘
       │ Hardware I/O               │ Hardware I/O
┌──────┴───────────────┐   ┌────────┴───────┐
│ TP4056 + ADC + GPIO   │   │ Speaker + Mic  │
│ WiFi Chip | MCU Pins  │   │ Sensors, Keys  │
└───────────────────────┘   └────────────────┘
```

---

## 3. Module Responsibilities

### 3.1 Power (Driver)

* Đọc ADC (chia áp R1/R2)
* Scale về điện áp thật (3.0–4.2V)
* Đọc tín hiệu TP4056:

  * CHRG (đang sạc)
  * STDBY (đầy)
* Bảo vệ khi chân hở, chân đứt, ADC floating
* Convert điện áp → % pin với hysteresis

**Không quyết định state.**

---

### 3.2 PowerManager

* Nhận voltage / percent / flag từ Power driver
* Xác định PowerState:

  * NORMAL
  * LOW_BATTERY
  * CRITICAL
  * CHARGING
  * FULL_BATTERY
  * ERROR
* Smoothing % nếu cần
* Notify state thay đổi về StateManager

**Không làm việc trực tiếp với ADC hay GPIO.**

---

### 3.3 StateManager

Vai trò: **Event Hub trung tâm**.

Chức năng:

* Lưu giữ các state:

  * InteractionState
  * ConnectivityState
  * SystemState
  * PowerState
* Thread-safe setter/getter
* Publish/subscribe (observer pattern)
* Không chứa logic, chỉ truyền trạng thái

**Tất cả modules phải thay đổi state qua đây.**

---

### 3.4 AppController (Orchestrator)

Đây là “bộ não" điều phối:

* Nhận state từ StateManager (qua queue)
* Ra quyết định điều phối:

  * Khi pin yếu → dim display
  * Khi wakeword → chuyển sang LISTENING
  * Khi PROCESSING → hiển thị animation “thinking”
  * Khi WS Connected → update UI
* Mapping AppEvents → state transition
* Dispatch hành động sang:

  * DisplayAnimator
  * AudioManager
  * NetworkManager

Chạy trong **AppControllerTask** (không block module khác).

---

### 3.5 DisplayAnimator

* Render animation
* Hiển thị trạng thái thiết bị
* Có task riêng hoặc dùng timer
* Không tự quyết định state → chỉ đọc state hoặc API UI

---

### 3.6 AudioManager

* Quản lý mic stream, echo cancel, playback
* Có ít nhất 1–2 task riêng (I2S input/output)
* Bị điều khiển bởi AppController

---

### 3.7 NetworkManager

* WiFi station/portal logic
* WebSocket/HTTP client
* Auto reconnect
* Gửi/nhận message speech-to-cloud

---

### 3.8 Input Modules

* Touch / Button
* Wakeword engine
* Motion sensor (tuỳ)

Mỗi input gửi **AppEvent** → AppController.

---

# 4. Event Pipeline

Dòng sự kiện chuẩn trong hệ thống:

### Wakeword Example:

```
Mic → Wakeword → AppEvent::WAKEWORD_DETECTED →
AppController → StateManager::setInteraction(TRIGGERED) →
AppController auto → LISTENING →
DisplayAnimator show listening →
AudioManager capture + stream →
NetworkManager send audio →
Server response →
AppController PROCESSING → SPEAKING →
AudioManager playback → back to IDLE
```

---

### Power Example:

```
PowerDriver read → voltage=3.55V, CHRG=0 →
PowerManager → LOW_BATTERY →
StateManager.update →
AppController.onPowerStateChanged(LOW_BATTERY) →
DisplayAnimator.showLowBatteryIcon() →
AudioManager.limitVolume()
```

---

# 5. Multitasking Model (ESP-IDF)

| Module          | Activity               | Task?       |
| --------------- | ---------------------- | ----------- |
| AppController   | Queue-based event loop | ✔           |
| DisplayAnimator | Animation loop         | ✔           |
| Audio Capture   | Real-time mic          | ✔           |
| Audio Playback  | Real-time output       | ✔           |
| Network         | WS/HTTP reconnect      | ✔           |
| PowerManager    | Timer callback         | ❌ (no task) |
| Power (ADC)     | run in driver          | ❌           |

AppController **không chạy hardware**, chỉ chạy logic.

---

# 6. State Definitions

### InteractionState

```
IDLE → TRIGGERED → LISTENING → PROCESSING → SPEAKING → IDLE
```

Nguồn kích hoạt:

* BUTTON
* WAKEWORD
* SERVER_COMMAND

### ConnectivityState

```
OFFLINE → CONNECTING_WIFI → WIFI_PORTAL → CONNECTING_WS → ONLINE
```

### SystemState

```
BOOTING → RUNNING → ERROR → MAINTENANCE → UPDATING_FIRMWARE
```

### PowerState

```
NORMAL → LOW_BATTERY → CRITICAL → CHARGING → FULL_BATTERY → ERROR
```

---

# 7. Naming & Coding Conventions

* Module-level prefix: `PowerManager`, `NetworkManager`, `DisplayAnimator`
* Private members: trailing `_`
* StateHub functions thread-safe
* Các module không set state trực tiếp cho nhau

---

# 8. Future Extensions

* OTA Manager integration
* Audio Echo Cancellation (AEC)
* Display theme engine
* Plugin system for input sources
* PowerSaver module: automatic dim + sleep
* ESP-NOW fallback mode

---

# 9. Summary

Bạn đã triển khai **kiến trúc chuẩn chuyên nghiệp**, tổng quát như:

* Clean layering
* Event-driven loop
* Thread-safe StateHub
* AppController orchestrator
* Hardware isolation
* UI-friendly model

Hệ thống hiện đã đủ nền tảng để build:

* Voice UI
* Smart assistant
* Battery-powered IoT device
* OTA update
* Modular firmware

---

