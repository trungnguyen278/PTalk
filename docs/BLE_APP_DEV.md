# PTalk BLE Mobile App Developer Guide

## Overview

Document này dành cho **mobile app developers** để implement BLE provisioning cho PTalk device. App sử dụng BLE GATT để cấu hình WiFi, MQTT broker, và MEO credentials.

---

## 1. BLE Service

### Service UUID

```
Service: 0xFF01 (PTalk Config Service)
```

### Scanning

- **Device Name:** `PTalk` hoặc custom name
- **Advertising:** Connectable, Scannable

---

## 2. Characteristics

### Full Characteristics Table

| UUID | Name | Properties | Auth Required | Description |
|------|------|------------|---------------|-------------|
| 0xFF02 | DEVICE_NAME | R/W | No | Device name (max 32 chars) |
| 0xFF03 | VOLUME | R/W | No | Volume 0-100 (1 byte) |
| 0xFF04 | BRIGHTNESS | R/W | No | Brightness 0-100 (1 byte) |
| 0xFF05 | WIFI_SSID | R/W | No | WiFi SSID (max 32 chars) |
| 0xFF06 | WIFI_PASS | W | No | WiFi Password (max 64 chars) |
| 0xFF07 | APP_VERSION | R | - | Firmware version string |
| 0xFF08 | BUILD_INFO | R | - | Build date/commit info |
| 0xFF09 | SAVE_CMD | W | No | Write 0x01 to save config |
| 0xFF0A | DEVICE_ID | R | - | Device MAC ID (12 hex chars) |
| 0xFF0B | WIFI_LIST | R | - | Cached WiFi networks (JSON) |
| 0xFF0C | WS_URL | R/W | **Yes** | WebSocket server URL |
| 0xFF0D | MQTT_URL | R/W | **Yes** | MQTT broker URL |
| 0xFF0E | USER_ID | R/W | No | MEO user namespace |
| 0xFF0F | TX_KEY | W | **Yes** | MQTT password (write-only) |
| 0xFF10 | PRODUCT_ID | R | - | Product identifier |
| 0xFF11 | DEV_MODEL | R | - | Device model name |
| 0xFF12 | DEV_MANUF | R | - | Manufacturer name |
| 0xFF13 | MAC_ADDR | R | - | Raw MAC address (6 bytes) |

---

## 3. Authentication Mechanism

### Protected Characteristics

Các characteristics sau yêu cầu **unlock trước khi write**:
- `WS_URL` (0xFF0C)
- `MQTT_URL` (0xFF0D)
- `TX_KEY` (0xFF0F)

### Auth Token

```
Token: "PTALK_OK"
```

### Unlock Flow

1. **Write token** vào characteristic cần unlock
2. Device sẽ log `"... auth unlocked by token"`
3. **Write actual value** vào cùng characteristic
4. Repeat cho các protected chars khác

```
// Ví dụ unlock MQTT_URL
Write "PTALK_OK" → 0xFF0D   // Unlock
Write "mqtt://broker:1883" → 0xFF0D   // Set value
```

> ⚠️ Token chỉ cần write 1 lần. Sau khi unlock, có thể write nhiều protected chars.

---

## 4. Provisioning Flow

### Complete Flow Diagram

```
┌─────────────────────────────────────────────────────┐
│  1. SCAN & CONNECT                                  │
├─────────────────────────────────────────────────────┤
│  • Scan for devices with service 0xFF01             │
│  • Connect to selected device                       │
│  • Discover services & characteristics              │
│  • Request MTU (recommend 512)                      │
└───────────────────────────┬─────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────┐
│  2. READ DEVICE INFO (Optional)                     │
├─────────────────────────────────────────────────────┤
│  • Read 0xFF0A → Device ID                          │
│  • Read 0xFF07 → Firmware version                   │
│  • Read 0xFF0B → WiFi list (for network picker)     │
│  • Read 0xFF02 → Current device name                │
└───────────────────────────┬─────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────┐
│  3. WRITE BASIC CONFIG                              │
├─────────────────────────────────────────────────────┤
│  • Write 0xFF05 ← WiFi SSID                         │
│  • Write 0xFF06 ← WiFi Password                     │
│  • Write 0xFF03 ← Volume (optional)                 │
│  • Write 0xFF04 ← Brightness (optional)             │
└───────────────────────────┬─────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────┐
│  4. UNLOCK PROTECTED CHARS                          │
├─────────────────────────────────────────────────────┤
│  • Write "PTALK_OK" → 0xFF0D (or 0xFF0C/0xFF0F)     │
└───────────────────────────┬─────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────┐
│  5. WRITE MEO CREDENTIALS                           │
├─────────────────────────────────────────────────────┤
│  • Write 0xFF0D ← MQTT URL (e.g., mqtt://x.x.x:1883)│
│  • Write 0xFF0E ← User ID (namespace)               │
│  • Write 0xFF0F ← TX Key (MQTT password)            │
│  • Write 0xFF0C ← WS URL (optional)                 │
└───────────────────────────┬─────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────┐
│  6. SAVE & RESTART                                  │
├─────────────────────────────────────────────────────┤
│  • Write 0x01 → 0xFF09 (SAVE_CMD)                   │
│  • Device saves to NVS & restarts                   │
│  • Connection will be lost (expected)               │
└─────────────────────────────────────────────────────┘
```

---

## 5. Code Examples

### Android (Kotlin)

```kotlin
// UUIDs
val SERVICE_UUID = UUID.fromString("0000FF01-0000-1000-8000-00805f9b34fb")
val CHAR_WIFI_SSID = UUID.fromString("0000FF05-0000-1000-8000-00805f9b34fb")
val CHAR_WIFI_PASS = UUID.fromString("0000FF06-0000-1000-8000-00805f9b34fb")
val CHAR_MQTT_URL = UUID.fromString("0000FF0D-0000-1000-8000-00805f9b34fb")
val CHAR_USER_ID = UUID.fromString("0000FF0E-0000-1000-8000-00805f9b34fb")
val CHAR_TX_KEY = UUID.fromString("0000FF0F-0000-1000-8000-00805f9b34fb")
val CHAR_SAVE_CMD = UUID.fromString("0000FF09-0000-1000-8000-00805f9b34fb")

val AUTH_TOKEN = "PTALK_OK"

// Provisioning function
suspend fun provisionDevice(
    gatt: BluetoothGatt,
    ssid: String,
    password: String,
    mqttUrl: String,
    userId: String,
    txKey: String
) {
    val service = gatt.getService(SERVICE_UUID)
    
    // 1. Write WiFi credentials
    writeCharacteristic(service, CHAR_WIFI_SSID, ssid.toByteArray())
    writeCharacteristic(service, CHAR_WIFI_PASS, password.toByteArray())
    
    // 2. Unlock protected chars
    writeCharacteristic(service, CHAR_MQTT_URL, AUTH_TOKEN.toByteArray())
    delay(100)
    
    // 3. Write MEO credentials
    writeCharacteristic(service, CHAR_MQTT_URL, mqttUrl.toByteArray())
    writeCharacteristic(service, CHAR_USER_ID, userId.toByteArray())
    writeCharacteristic(service, CHAR_TX_KEY, txKey.toByteArray())
    
    // 4. Save and restart
    writeCharacteristic(service, CHAR_SAVE_CMD, byteArrayOf(0x01))
    
    // Device will restart - connection will be lost
}
```

### iOS (Swift)

```swift
import CoreBluetooth

class PTalkProvisioner: NSObject, CBPeripheralDelegate {
    let serviceUUID = CBUUID(string: "FF01")
    let wifiSSIDChar = CBUUID(string: "FF05")
    let wifiPassChar = CBUUID(string: "FF06")
    let mqttURLChar = CBUUID(string: "FF0D")
    let userIDChar = CBUUID(string: "FF0E")
    let txKeyChar = CBUUID(string: "FF0F")
    let saveCmdChar = CBUUID(string: "FF09")
    
    let authToken = "PTALK_OK".data(using: .utf8)!
    
    func provision(
        peripheral: CBPeripheral,
        ssid: String,
        password: String,
        mqttUrl: String,
        userId: String,
        txKey: String
    ) {
        guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }),
              let chars = service.characteristics else { return }
        
        // Helper to find characteristic
        func char(_ uuid: CBUUID) -> CBCharacteristic? {
            chars.first { $0.uuid == uuid }
        }
        
        // 1. Write WiFi
        if let c = char(wifiSSIDChar) {
            peripheral.writeValue(ssid.data(using: .utf8)!, for: c, type: .withResponse)
        }
        if let c = char(wifiPassChar) {
            peripheral.writeValue(password.data(using: .utf8)!, for: c, type: .withResponse)
        }
        
        // 2. Unlock & write MQTT
        if let c = char(mqttURLChar) {
            peripheral.writeValue(authToken, for: c, type: .withResponse)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                peripheral.writeValue(mqttUrl.data(using: .utf8)!, for: c, type: .withResponse)
            }
        }
        
        // 3. Write MEO credentials
        if let c = char(userIDChar) {
            peripheral.writeValue(userId.data(using: .utf8)!, for: c, type: .withResponse)
        }
        if let c = char(txKeyChar) {
            peripheral.writeValue(txKey.data(using: .utf8)!, for: c, type: .withResponse)
        }
        
        // 4. Save
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            if let c = char(self.saveCmdChar) {
                peripheral.writeValue(Data([0x01]), for: c, type: .withResponse)
            }
        }
    }
}
```

### Flutter (Dart)

```dart
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class PTalkProvisioner {
  static const serviceUuid = Guid("0000FF01-0000-1000-8000-00805f9b34fb");
  static const wifiSsidUuid = Guid("0000FF05-0000-1000-8000-00805f9b34fb");
  static const wifiPassUuid = Guid("0000FF06-0000-1000-8000-00805f9b34fb");
  static const mqttUrlUuid = Guid("0000FF0D-0000-1000-8000-00805f9b34fb");
  static const userIdUuid = Guid("0000FF0E-0000-1000-8000-00805f9b34fb");
  static const txKeyUuid = Guid("0000FF0F-0000-1000-8000-00805f9b34fb");
  static const saveCmdUuid = Guid("0000FF09-0000-1000-8000-00805f9b34fb");
  
  static const authToken = "PTALK_OK";

  Future<void> provision(
    BluetoothDevice device, {
    required String ssid,
    required String password,
    required String mqttUrl,
    required String userId,
    required String txKey,
  }) async {
    final services = await device.discoverServices();
    final service = services.firstWhere((s) => s.uuid == serviceUuid);
    
    BluetoothCharacteristic char(Guid uuid) =>
        service.characteristics.firstWhere((c) => c.uuid == uuid);
    
    // 1. WiFi credentials
    await char(wifiSsidUuid).write(ssid.codeUnits);
    await char(wifiPassUuid).write(password.codeUnits);
    
    // 2. Unlock protected chars
    await char(mqttUrlUuid).write(authToken.codeUnits);
    await Future.delayed(Duration(milliseconds: 100));
    
    // 3. MEO credentials
    await char(mqttUrlUuid).write(mqttUrl.codeUnits);
    await char(userIdUuid).write(userId.codeUnits);
    await char(txKeyUuid).write(txKey.codeUnits);
    
    // 4. Save & restart
    await char(saveCmdUuid).write([0x01]);
    
    // Device will disconnect
  }
}
```

---

## 6. Reading WiFi List

### Characteristic: 0xFF0B (WIFI_LIST)

Returns JSON array of cached networks:

```json
[
  {"ssid": "HomeWiFi", "rssi": -45, "auth": 3},
  {"ssid": "Office5G", "rssi": -62, "auth": 4},
  {"ssid": "Guest", "rssi": -78, "auth": 0}
]
```

### Auth Types

| Value | Meaning |
|-------|---------|
| 0 | Open (no password) |
| 1 | WEP |
| 2 | WPA_PSK |
| 3 | WPA2_PSK |
| 4 | WPA_WPA2_PSK |
| 5 | WPA2_ENTERPRISE |

### Reading Large Data (MTU Chunking)

WiFi list có thể lớn hơn MTU. Device hỗ trợ streaming:
- Mỗi read trả về chunk tiếp theo
- Kết thúc khi nhận được chunk rỗng hoặc `]`

```kotlin
// Android example
suspend fun readWifiList(gatt: BluetoothGatt): String {
    val char = gatt.getService(SERVICE_UUID)
        .getCharacteristic(CHAR_WIFI_LIST)
    
    val builder = StringBuilder()
    do {
        val chunk = readCharacteristic(char)
        builder.append(String(chunk))
    } while (chunk.isNotEmpty() && !builder.endsWith("]"))
    
    return builder.toString()
}
```

---

## 7. URL Formats

### MQTT URL

```
mqtt://hostname:port
mqtt://192.168.1.100:1883
mqtts://secure.broker.com:8883
```

### WebSocket URL

```
ws://hostname:port/ws
ws://192.168.1.100:8000/ws
wss://secure.server.com/ws
```

> Device sẽ tự động normalize URLs (thêm scheme/path nếu thiếu)

---

## 8. Error Handling

### BLE Connection Errors

| Error | Cause | Solution |
|-------|-------|----------|
| Connection timeout | Device out of range | Move closer |
| Service not found | Wrong device | Verify device name |
| Write failed | Auth not unlocked | Send "PTALK_OK" first |
| Disconnected after save | Expected behavior | Wait & reconnect if needed |

### Validation

- **SSID:** Max 32 chars, non-empty
- **Password:** Max 64 chars (can be empty for open networks)
- **MQTT URL:** Must be valid URL or host:port
- **User ID:** Non-empty string
- **TX Key:** Non-empty string (sensitive)

---

## 9. UI/UX Recommendations

### Provisioning Screen Flow

```
┌─────────────────────────┐
│  1. Scan for Devices    │
│  ┌───────────────────┐  │
│  │ PTalk-A0B1C2      │  │
│  │ PTalk-D3E4F5      │  │
│  └───────────────────┘  │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  2. WiFi Selection      │
│  ┌───────────────────┐  │
│  │ HomeWiFi    -45dB │  │
│  │ Office5G    -62dB │  │
│  └───────────────────┘  │
│  Password: [________]   │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  3. Server Settings     │
│  MQTT: [broker:1883___] │
│  User: [my_namespace__] │
│  Key:  [************__] │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  4. Provisioning...     │
│  ████████░░░░ 60%       │
│  Writing MQTT URL...    │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│  ✓ Setup Complete!      │
│  Device will restart    │
└─────────────────────────┘
```

### Progress States

1. Connecting...
2. Reading device info...
3. Writing WiFi credentials...
4. Writing server settings...
5. Saving configuration...
6. ✓ Complete (device restarting)

---

## 10. Testing Checklist

- [ ] Scan finds PTalk devices
- [ ] Can read device ID and version
- [ ] Can read WiFi list
- [ ] Basic writes work (volume, brightness)
- [ ] Auth unlock works for MQTT_URL
- [ ] All MEO credentials save successfully
- [ ] Device restarts after SAVE_CMD
- [ ] Device connects to WiFi after restart
- [ ] Device connects to MQTT after restart

---

## 11. Troubleshooting

### "Write blocked" error

**Cause:** Protected characteristic not unlocked  
**Solution:** Write `"PTALK_OK"` to the characteristic first

### Device doesn't restart after save

**Cause:** SAVE_CMD not received properly  
**Solution:** Ensure writing `0x01` byte (not string "1")

### WiFi not connecting

**Cause:** Wrong credentials or SSID  
**Solution:** Verify SSID matches exactly (case-sensitive)

### MQTT not connecting

**Cause:** Wrong URL format or credentials  
**Solution:** Check format is `mqtt://host:port`, verify tx_key

---

## Contact

- Firmware: PTalk Team  
- BLE Protocol Version: 1.0
