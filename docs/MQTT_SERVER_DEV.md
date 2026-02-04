# PTalk MQTT Server Developer Guide

## Overview

PTalk sử dụng MEO SDK protocol để giao tiếp giữa device và cloud. Document này dành cho **backend/server developers** để implement MQTT broker và xử lý messages.

---

## 1. MQTT Connection

### Authentication

| Field | Value | Description |
|-------|-------|-------------|
| **Broker** | `mqtt://your-broker:1883` | Configurable via BLE |
| **Username** | `{device_efuse_id}` | 12-char hex, e.g. `A0B1C2D3E4F5` |
| **Password** | `{tx_key}` | From BLE provisioning, default: `ptalk_default_key` |
| **Client ID** | `PTalk_{device_id}` | e.g. `PTalk_A0B1C2D3E4F5` |

### Default Credentials (Development)

```
Username: {device_mac_id}  (auto-generated from ESP32 eFuse)
Password: ptalk_default_key
```

> ⚠️ Production: Provision unique `tx_key` per device via BLE app.

---

## 2. Topic Structure

### MEO SDK Topics (Primary)

```
meo/{userId}/{deviceId}/feature     # Cloud → Device: invoke features
meo/{userId}/{deviceId}/event/{name} # Device → Cloud: publish events
```

### Legacy Topics (Backward Compatibility)

```
devices/{deviceId}/cmd              # Cloud → Device: legacy commands
devices/{deviceId}/status           # Device → Cloud: status updates
devices/{deviceId}/ota_data         # Cloud → Device: OTA firmware chunks
devices/{deviceId}/ota_ack          # Device → Cloud: OTA acknowledgments
```

### Example Topics

```
# For device A0B1C2D3E4F5 with user_id "user123"
meo/user123/A0B1C2D3E4F5/feature           # Invoke features
meo/user123/A0B1C2D3E4F5/event/status      # Status events
meo/user123/A0B1C2D3E4F5/event/button      # Button press events
meo/user123/A0B1C2D3E4F5/event/ota         # OTA progress events
```

---

## 3. Feature Invoke Protocol

### Request Format (Cloud → Device)

```json
{
  "feature": "set_volume",
  "call_id": "uuid-1234-5678",
  "params": {
    "level": 80
  }
}
```

### Response Format (Device → Cloud)

**Success:**
```json
{
  "call_id": "uuid-1234-5678",
  "feature": "set_volume",
  "success": true,
  "result": {
    "level": 80
  }
}
```

**Error:**
```json
{
  "call_id": "uuid-1234-5678",
  "feature": "set_volume",
  "success": false,
  "error": {
    "code": "INVALID_PARAM",
    "message": "Volume must be 0-100"
  }
}
```

### Response Topic

```
meo/{userId}/{deviceId}/event/feature_response
```

---

## 4. Built-in Features

### Device Control

| Feature | Params | Description |
|---------|--------|-------------|
| `set_volume` | `{"level": 0-100}` | Set speaker volume |
| `set_brightness` | `{"level": 0-100}` | Set display brightness |
| `reboot` | `{}` | Restart device |
| `request_status` | `{}` | Request full status report |
| `set_emotion` | `{"emotion": "happy"}` | Set display emotion |

### Emotions

Valid values: `idle`, `happy`, `sad`, `neutral`, `thinking`, `listening`, `stun`

### Example: Set Volume

```json
// Request
{
  "feature": "set_volume",
  "call_id": "vol-001",
  "params": {"level": 75}
}

// Response
{
  "call_id": "vol-001",
  "feature": "set_volume",
  "success": true,
  "result": {"level": 75}
}
```

---

## 5. Device Events

Device publishes events to `meo/{userId}/{deviceId}/event/{eventName}`

### Status Event

Topic: `.../event/status`

```json
{
  "event": "status",
  "timestamp": 1706000000,
  "data": {
    "device_id": "A0B1C2D3E4F5",
    "firmware": "1.0.0",
    "battery": 85,
    "charging": false,
    "wifi_rssi": -45,
    "volume": 60,
    "brightness": 100,
    "state": "idle"
  }
}
```

### Button Event

Topic: `.../event/button`

```json
{
  "event": "button",
  "timestamp": 1706000000,
  "data": {
    "action": "press",      // "press", "release", "long_press"
    "duration_ms": 150
  }
}
```

### Audio Event

Topic: `.../event/audio`

```json
{
  "event": "audio",
  "timestamp": 1706000000,
  "data": {
    "action": "start_listening"  // "start_listening", "stop_listening", "start_speaking", "stop_speaking"
  }
}
```

### OTA Event

Topic: `.../event/ota`

```json
{
  "event": "ota",
  "timestamp": 1706000000,
  "data": {
    "status": "downloading",  // "downloading", "installing", "completed", "failed"
    "progress": 45,
    "error": null
  }
}
```

---

## 6. Legacy Commands (Backward Compatibility)

Topic: `devices/{deviceId}/cmd`

### JSON Commands

```json
{"cmd": "set_volume", "value": 80}
{"cmd": "set_brightness", "value": 100}
{"cmd": "reboot"}
{"cmd": "start_ota", "url": "http://...", "version": "1.1.0"}
```

### Text Commands

```
ping              → Device replies "pong"
get_status        → Device publishes status
```

---

## 7. OTA Protocol

### 1. Initiate OTA (via feature invoke)

```json
{
  "feature": "start_ota",
  "call_id": "ota-001",
  "params": {
    "url": "http://firmware.server/v1.1.0.bin",
    "version": "1.1.0",
    "checksum": "sha256:abc123..."
  }
}
```

### 2. Progress Events

Device publishes to `.../event/ota`:

```json
{"event": "ota", "data": {"status": "downloading", "progress": 25}}
{"event": "ota", "data": {"status": "downloading", "progress": 50}}
{"event": "ota", "data": {"status": "downloading", "progress": 100}}
{"event": "ota", "data": {"status": "installing"}}
{"event": "ota", "data": {"status": "completed"}}
```

### 3. Error Handling

```json
{
  "event": "ota",
  "data": {
    "status": "failed",
    "error": "CHECKSUM_MISMATCH"
  }
}
```

---

## 8. Server Implementation Checklist

### MQTT Broker Setup

- [ ] Enable authentication (username/password)
- [ ] Create ACL rules:
  ```
  user {device_id}
    topic read  meo/+/{device_id}/feature
    topic read  devices/{device_id}/cmd
    topic read  devices/{device_id}/ota_data
    topic write meo/+/{device_id}/event/#
    topic write devices/{device_id}/status
  ```

### Backend Service

- [ ] Subscribe to `meo/+/+/event/#` for all device events
- [ ] Implement feature invoke with call_id tracking
- [ ] Handle response timeout (recommend: 10s)
- [ ] Store device status from status events
- [ ] Implement OTA orchestration

### Database Schema (Suggested)

```sql
CREATE TABLE devices (
  device_id VARCHAR(12) PRIMARY KEY,
  user_id VARCHAR(64),
  tx_key VARCHAR(64),
  firmware_version VARCHAR(16),
  last_seen TIMESTAMP,
  battery_level INT,
  is_online BOOLEAN
);

CREATE TABLE device_events (
  id SERIAL PRIMARY KEY,
  device_id VARCHAR(12),
  event_type VARCHAR(32),
  payload JSONB,
  created_at TIMESTAMP DEFAULT NOW()
);
```

---

## 9. Testing

### Mosquitto CLI Examples

```bash
# Subscribe to all events from a device
mosquitto_sub -h broker -u A0B1C2D3E4F5 -P ptalk_default_key \
  -t "meo/+/A0B1C2D3E4F5/event/#" -v

# Invoke set_volume feature
mosquitto_pub -h broker -u server -P serverpass \
  -t "meo/default/A0B1C2D3E4F5/feature" \
  -m '{"feature":"set_volume","call_id":"test1","params":{"level":50}}'

# Legacy command
mosquitto_pub -h broker -t "devices/A0B1C2D3E4F5/cmd" \
  -m '{"cmd":"set_volume","value":50}'
```

### Python Example

```python
import paho.mqtt.client as mqtt
import json

def on_message(client, userdata, msg):
    print(f"{msg.topic}: {msg.payload.decode()}")

client = mqtt.Client("server")
client.username_pw_set("server", "serverpass")
client.on_message = on_message
client.connect("broker", 1883)

# Subscribe to all device events
client.subscribe("meo/+/+/event/#")

# Invoke feature
client.publish(
    "meo/default/A0B1C2D3E4F5/feature",
    json.dumps({
        "feature": "set_volume",
        "call_id": "py-001",
        "params": {"level": 70}
    })
)

client.loop_forever()
```

---

## 10. Error Codes

| Code | Description |
|------|-------------|
| `UNKNOWN_FEATURE` | Feature name not registered |
| `INVALID_PARAM` | Missing or invalid parameter |
| `DEVICE_BUSY` | Device is busy (e.g., during OTA) |
| `INTERNAL_ERROR` | Device internal error |
| `TIMEOUT` | Operation timed out |

---

## Contact

- Firmware: PTalk Team
- Protocol Version: MEO SDK v1.0
