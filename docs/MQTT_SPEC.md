# PTalk MQTT Communication Specification (MEO SDK Compatible)

Tài liệu này đặc tả cấu trúc bản tin và quy trình giao tiếp giữa **PTalk Server** và **PTalk Device** (ESP32) thông qua giao thức MQTT, tương thích với **MEO SDK**.

## 1. Tổng quan kiến trúc

PTalk hỗ trợ 2 chế độ giao tiếp MQTT:
- **MEO SDK Mode**: Tương thích với MEO Arduino SDK (feature invoke/event model)
- **Legacy Mode**: Tương thích ngược với giao thức PTalk cũ

---

## 2. MEO SDK Topic Patterns

### 2.1 Namespace và Topic Structure

| Pattern | Hướng | Mô tả |
|---------|-------|-------|
| `meo/{userId}/{deviceId}/feature` | Server → Device | Cloud-compatible feature invoke |
| `meo/{deviceId}/feature/{featureName}/invoke` | Server → Device | Legacy feature invoke |
| `meo/{userId}/{deviceId}/event/{eventName}` | Device → Server | Device events |
| `meo/{userId}/{deviceId}/event/feature_response` | Device → Server | Feature invoke response |

### 2.2 Device Identity

- **deviceId**: MAC address hex string (12 ký tự viết hoa, ví dụ: `D4E9F4C13B1C`)
- **userId**: Namespace cho multi-tenant (default: `"default"`)
- **tx_key**: MQTT password (được provision qua BLE)

### 2.3 Topic Examples

```
# Cloud-compatible invoke (userId = "user42", deviceId = "D4E9F4C13B1C")
meo/user42/D4E9F4C13B1C/feature

# Legacy invoke (deviceId only)
meo/D4E9F4C13B1C/feature/set_volume/invoke

# Event publishing
meo/user42/D4E9F4C13B1C/event/status
meo/user42/D4E9F4C13B1C/event/feature_response
meo/user42/D4E9F4C13B1C/event/battery
```

---

## 3. Feature Invoke Model

### 3.1 Cloud-Compatible Invoke (Preferred)

**Topic**: `meo/{userId}/{deviceId}/feature`

**Payload**:
```json
{
  "feature": "set_volume",
  "params": {
    "volume": 75
  },
  "invoke_id": "abc123"
}
```

Hoặc sử dụng `feature_name` thay cho `feature`:
```json
{
  "feature_name": "set_brightness",
  "params": {
    "brightness": 80
  }
}
```

### 3.2 Legacy Invoke

**Topic**: `meo/{deviceId}/feature/{featureName}/invoke`

**Payload**:
```json
{
  "params": {
    "volume": 75
  }
}
```

### 3.3 Feature Response

**Topic**: `meo/{userId}/{deviceId}/event/feature_response`

**Payload**:
```json
{
  "feature_name": "set_volume",
  "device_id": "D4E9F4C13B1C",
  "success": true,
  "message": "Volume set to 75",
  "invoke_id": "abc123",
  "data": {
    "volume": "75"
  }
}
```

---

## 4. Built-in Features (PTalk)

### 4.1 Device Configuration Features

| Feature Name | Params | Description |
|--------------|--------|-------------|
| `set_volume` | `{"volume": 0-100}` | Set speaker volume |
| `set_brightness` | `{"brightness": 0-100}` | Set display brightness |
| `set_device_name` | `{"device_name": "string"}` | Set device display name |
| `set_wifi` | N/A | **Not supported** - Use BLE provisioning |

### 4.2 Device Control Features

| Feature Name | Params | Description |
|--------------|--------|-------------|
| `request_status` | (none) | Request full device status |
| `reboot` | (none) | Reboot device |
| `request_ble_config` | (none) | Enter BLE config mode |
| `request_ota` | `{"size": uint32, "sha256": "...", "chunk_size": int, "total_chunks": int}` | Initiate OTA update |

### 4.3 PTalk-Specific Features

| Feature Name | Params | Description |
|--------------|--------|-------------|
| `set_emotion` | `{"code": "01"}` | Set display emotion (2-char code) |
| `play_tts` | `{"text": "...", "voice": "..."}` | Play TTS audio |
| `stop_audio` | (none) | Stop current audio playback |

---

## 5. Device Events

### 5.1 Event Structure

**Topic**: `meo/{userId}/{deviceId}/event/{eventName}`

**Payload**:
```json
{
  "event": "status",
  "device_id": "D4E9F4C13B1C",
  "key": "value",
  ...
}
```

### 5.2 Built-in Events

| Event Name | Data Fields | Description |
|------------|-------------|-------------|
| `status` | `connectivity`, `firmware_version`, `battery_percent`, ... | Device status update |
| `feature_response` | `feature_name`, `success`, `message`, `data` | Response to feature invoke |
| `battery` | `percent`, `charging` | Battery status change |
| `connectivity` | `state`, `rssi` | Connectivity change |
| `ota_progress` | `percent`, `bytes_received` | OTA download progress |
| `ota_complete` | `success`, `message` | OTA completion status |
| `error` | `code`, `message` | Error notification |

---

## 6. Legacy PTalk Topics (Backward Compatibility)

Các topic cũ vẫn được hỗ trợ để tương thích ngược:

| Topic | Hướng | Mô tả |
|-------|-------|-------|
| `devices/{MAC}/cmd` | Server → Device | JSON config commands |
| `devices/{MAC}/status` | Device → Server | Device status (retained) |
| `devices/{MAC}/ota_data` | Server → Device | OTA binary chunks |
| `devices/{MAC}/ota_ack` | Device → Server | OTA acknowledgments |

### 6.1 Legacy Command Format

```json
{
  "cmd": "set_volume",
  "volume": 75
}
```

---

## 7. BLE Provisioning Characteristics

### 7.1 Service UUID

**Service**: `0xFF01`

### 7.2 Characteristics

| UUID | Name | Access | Description |
|------|------|--------|-------------|
| `0xFF02` | DEVICE_NAME | RW | Device display name |
| `0xFF03` | VOLUME | RW | Volume level (0-100) |
| `0xFF04` | BRIGHTNESS | RW | Brightness level (0-100) |
| `0xFF05` | WIFI_SSID | W | WiFi SSID |
| `0xFF06` | WIFI_PASS | W | WiFi password |
| `0xFF07` | APP_VERSION | R | Firmware version |
| `0xFF08` | BUILD_INFO | R | Build info string |
| `0xFF09` | SAVE_CMD | W | Save config (write 0x01) |
| `0xFF0A` | DEVICE_ID | R | Device MAC string |
| `0xFF0B` | WIFI_LIST | R | Scanned WiFi list (streaming) |
| `0xFF0C` | WS_URL | RW | WebSocket URL (auth required) |
| `0xFF0D` | MQTT_URL | RW | MQTT broker URL (auth required) |
| `0xFF0E` | USER_ID | RW | MEO user ID |
| `0xFF0F` | TX_KEY | W | MEO tx_key / MQTT password |
| `0xFF10` | PRODUCT_ID | R | Cloud product ID |
| `0xFF11` | DEV_MODEL | R | Device model |
| `0xFF12` | DEV_MANUF | R | Device manufacturer |
| `0xFF13` | MAC_ADDR | R | Raw MAC address (6 bytes) |

### 7.3 Authentication

Các characteristics nhạy cảm (WS_URL, MQTT_URL, TX_KEY) yêu cầu unlock trước khi write:
1. Write auth token `"PTALK_OK"` vào characteristic
2. Sau khi unlock, có thể read/write giá trị thực

---

## 8. OTA Protocol

### 8.1 Initiate OTA

**Feature invoke**:
```json
{
  "feature": "request_ota",
  "params": {
    "size": 1234567,
    "sha256": "abc123...",
    "chunk_size": 2048,
    "total_chunks": 603
  }
}
```

### 8.2 Binary Chunk Format

**Topic**: `devices/{MAC}/ota_data` hoặc `meo/{userId}/{deviceId}/ota_data`

**Binary Header (12 bytes, Little Endian)**:
| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Sequence number (0, 1, 2...) |
| 4 | 4 | Chunk data size |
| 8 | 4 | CRC32 of chunk data |
| 12+ | N | Chunk data |

### 8.3 OTA Acknowledgment

**Topic**: `devices/{MAC}/ota_ack`

**ACK**:
```json
{"ota_ack": 0}
```

**NACK**:
```json
{"ota_nack": 5, "expected_seq": 4}
```

---

## 9. Configuration Summary

| Parameter | Value |
|-----------|-------|
| MQTT Buffer Size | 4096 bytes |
| Keep Alive | 60 seconds |
| Command QoS | 1 |
| OTA Data QoS | 1 |
| Status QoS | 1 (retained) |
| Max JSON Length | 1024 bytes |
| Max Binary Length | 4000 bytes |
| OTA Chunk Size | 2048 bytes (recommended) |

---

## 10. Migration Guide

### From Legacy PTalk to MEO SDK

1. **Topics**: Change `devices/{MAC}/cmd` → `meo/{userId}/{deviceId}/feature`
2. **Commands**: Wrap in feature invoke format
3. **Responses**: Parse from `feature_response` event topic

### Example Migration

**Old (Legacy)**:
```
Topic: devices/D4E9F4C13B1C/cmd
Payload: {"cmd": "set_volume", "volume": 75}
```

**New (MEO SDK)**:
```
Topic: meo/default/D4E9F4C13B1C/feature
Payload: {"feature": "set_volume", "params": {"volume": 75}}
```

---

*Document updated: 2026-02-04*
*MEO SDK Version: 3.x Compatible*
