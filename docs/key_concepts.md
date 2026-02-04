# Key Concepts — MEO ESP ARDUINO LIBS (BLE, Provisioning, MQTT, Features)

This document summarizes the core concepts and runtime flows used by the MEO Arduino SDK: provisioning via BLE, device identity and credentials, MQTT connectivity and topic patterns, and the feature invoke/event model.

**Overview**
- Purpose: provide a small flow to provision Wi‑Fi and device credentials over BLE, connect to an MQTT gateway, declare device capabilities, and handle feature invocations/events.
- Main modules: BLE Provisioning (`MeoBleProvision`), MQTT transport (`MeoMqttClient`), Feature layer (`MeoFeature`/MeoDevice), Storage (`MeoStorage`).

**Provisioning (BLE)**
- Role: expose read-only device info and read/write characteristics so a provisioning app can configure Wi‑Fi SSID/password, device id / tx_key (MQTT password), and optional user-id.
- Lifecycle:
  1. Device advertises a provisioning BLE service with a set of characteristics.
  2. Mobile app connects and reads device info (model, manufacturer, product/build info, MAC).
  3. App writes `wifi_ssid` and `wifi_pass` characteristics (write-only for password) to configure connectivity.
  4. App writes `tx_key` and optional `user_id` to provide MQTT credentials/namespace.
  5. Device stores values via `MeoStorage` and (optionally) reboots to apply.

**Important BLE characteristics**
- CH_UUID_WIFI_SSID (RW) — SSID string.
- CH_UUID_WIFI_PASS (WO) — Wi‑Fi password (write-only).
- CH_UUID_PRODUCT_ID / CH_UUID_BUILD_INFO (RO) — cloud-compatible strings. When set in software, the SDK publishes full strings into these characteristics so the central reads the full text.
- CH_UUID_MAC_ADDR (RO) — device MAC as binary (6 bytes) for accurate identity.
- CH_UUID_DEV_MODEL / CH_UUID_DEV_MANUF (RO) — device model and manufacturer strings.

Implementation note: always pass owned strings (std::string or explicit buffer+len) to BLE `setValue()` so the NimBLE library copies the data; do not pass pointers to ephemeral buffers.

**Device identity & credentials**
- Device ID: generated from the device MAC (hex string) if not provisioned explicitly.
- Transmit key (`tx_key`): used as the MQTT password.
- Optional `user_id`: top-level namespace for multi-tenant/topic separation.
- The presence of cloud-compatible product info (productId/buildInfo) toggles a "cloud-compatible" behavior for how the device subscribes to feature invokes.

**MQTT: topics and patterns**
The SDK uses the `meo` topic namespace and supports two invoke patterns:

1) Edge/topic-encoded (legacy)
- Subscribe: `meo/{deviceId}/feature/{featureName}/invoke`
- Example: `meo/device123/feature/led/invoke`
- Payload: JSON describing parameters (convention: `{ "params": { ... } }`).

2) Cloud/payload-encoded (cloud-compatible)
- Subscribe: `meo/{userId}/{deviceId}/feature`
- Example: `meo/user42/device123/feature`
- Payload: JSON that includes a field naming the feature and params, e.g.:
  {
    "feature": "led",
    "params": { "state": "on" }
  }
- The SDK will accept `feature` or `feature_name` as the key for the feature.

Event publishing (device → server):
- Base event topic: `meo/{userId}/{deviceId}/event`
- Per-event: `meo/{userId}/{deviceId}/event/{eventName}` (used by helpers that include the event name)

Feature response: published to `meo/{userId}/{deviceId}/event/feature_response` with JSON payload including `feature_name`, `device_id`, `success`, and optional `message`.

**Feature invoke flow (device side)**
1. MQTT message arrives on subscribed topic.
2. SDK extracts the feature name either from the topic path (legacy) or from JSON `feature`/`feature_name` (cloud-compatible).
3. SDK parses `params` (preferred) or other top-level JSON keys into a MeoEventPayload (string→string map).
4. SDK constructs a `MeoFeatureCall` with `deviceId`, `featureName` and `params` and dispatches to the registered handler for that feature name.
5. Handler executes and may call `sendFeatureResponse()` to publish a result.

**Best practices and debugging**
- Always verify the bytes/length when debugging BLE: print length and raw bytes (hex) rather than relying on C-style strings (which stop at NUL).
- For MAC address, use raw 6-byte binary format (not ASCII hex) when central expects binary; conversely, use ASCII hex if central expects a readable string.
- Ensure the device has stored credentials (`tx_key` and device id) before attempting MQTT connect — otherwise provisioning must complete first.
- For troubleshooting invokes: enable debug tags `DEVICE,MQTT,PROV` via `setDebugTags()` to see subscription and incoming payload logging.

**Examples**
- Cloud-compatible invoke (topic): `meo/user42/deviceABC/feature`
  Payload:
  {
    "feature": "toggle_fan",
    "params": { "speed": "2" }
  }

- Legacy invoke (topic): `meo/deviceABC/feature/toggle_fan/invoke`
  Payload:
  { "params": { "speed": "2" } }

- Publish event (device): `meo/user42/deviceABC/event/temperature`
  Payload: `{ "value": "26.3" }`

**Where to look in code**
- Provisioning and BLE: `lib/meo/provision/Meo3_BleProvision.*`
- Device lifecycle, declare, and MQTT wiring: `lib/meo/Meo3_Device.*`
- Feature layer: `lib/meo/feature/Meo3_Feature.*`
- MQTT transport wrapper: `lib/meo/mqtt/Meo3_Mqtt.*`

If you want, I can:
- Add this file to a `docs/` folder instead.
- Expand any section with sequence diagrams or sample payloads for specific features.

---
Generated by the SDK helper to make onboarding and debugging faster.
