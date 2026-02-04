#!/usr/bin/env python3
"""
PTalk Device Simulator - MEO SDK Compatible
============================================
Simulates a PTalk device with full MQTT MEO protocol support.

Features:
- MEO SDK topic patterns (meo/{userId}/{deviceId}/feature)
- Feature invoke/response model
- Event publishing
- Real audio I/O via PyAudio
- IMA ADPCM codec
- WebSocket for audio streaming
- OTA update support

Usage:
    python sim_ptalk.py --id D4E9F4C13B1C --user_id default --mqtt localhost --ws ws://localhost:8000/ws
"""

import asyncio
import websockets
import paho.mqtt.client as mqtt
import json
import time
import struct
import binascii
import logging
import argparse
import pyaudio
import random
import uuid
from typing import Optional, Dict, Any

# Cấu hình logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
logger = logging.getLogger("PTalkMeoSim")

# ============================================================================
# IMA ADPCM CODEC (Ported from AdpcmCodec.cpp)
# ============================================================================
class IMA_ADPCM_Codec:
    INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
    STEP_TABLE = [
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
        253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
        1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
        3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
        12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    ]

    def __init__(self):
        self.enc_predictor = 0
        self.enc_index = 0
        self.dec_predictor = 0
        self.dec_index = 0

    def clamp(self, n, minn, maxn):
        return max(min(maxn, n), minn)

    def encode(self, pcm_data):
        samples = struct.unpack(f'<{len(pcm_data)//2}h', pcm_data)
        out = bytearray()
        high_nibble = False
        out_byte = 0
        for sample in samples:
            step = self.STEP_TABLE[self.enc_index]
            diff = sample - self.enc_predictor
            sign = 8 if diff < 0 else 0
            if sign: diff = -diff
            delta = 0
            temp_step = step
            if diff >= temp_step: delta |= 4; diff -= temp_step
            temp_step >>= 1
            if diff >= temp_step: delta |= 2; diff -= temp_step
            temp_step >>= 1
            if diff >= temp_step: delta |= 1
            nibble = delta | sign
            diffq = step >> 3
            if delta & 4: diffq += step
            if delta & 2: diffq += step >> 1
            if delta & 1: diffq += step >> 2
            self.enc_predictor += -diffq if sign else diffq
            self.enc_predictor = self.clamp(self.enc_predictor, -32768, 32767)
            self.enc_index = self.clamp(self.enc_index + self.INDEX_TABLE[nibble], 0, 88)
            if not high_nibble:
                out_byte = (nibble & 0x0F) << 4
                high_nibble = True
            else:
                out.append(out_byte | (nibble & 0x0F))
                high_nibble = False
        return bytes(out)

    def decode(self, adpcm_data):
        out_samples = []
        for byte in adpcm_data:
            for shift in [4, 0]:
                nibble = (byte >> shift) & 0x0F
                step = self.STEP_TABLE[self.dec_index]
                sign = nibble & 8
                delta = nibble & 7
                diffq = step >> 3
                if delta & 4: diffq += step
                if delta & 2: diffq += step >> 1
                if delta & 1: diffq += step >> 2
                self.dec_predictor += -diffq if sign else diffq
                self.dec_predictor = self.clamp(self.dec_predictor, -32768, 32767)
                self.dec_index = self.clamp(self.dec_index + self.INDEX_TABLE[nibble], 0, 88)
                out_samples.append(self.dec_predictor)
        return struct.pack(f'<{len(out_samples)}h', *out_samples)


# ============================================================================
# MEO FEATURE NAMES (from MeoFeature.hpp)
# ============================================================================
class MeoFeatures:
    # Device configuration
    SET_VOLUME = "set_volume"
    SET_BRIGHTNESS = "set_brightness"
    SET_DEVICE_NAME = "set_device_name"
    SET_WIFI = "set_wifi"
    
    # Device control
    REBOOT = "reboot"
    REQUEST_STATUS = "request_status"
    REQUEST_OTA = "request_ota"
    REQUEST_BLE_CONFIG = "request_ble_config"
    
    # PTalk specific
    PLAY_TTS = "play_tts"
    STOP_AUDIO = "stop_audio"
    SET_EMOTION = "set_emotion"
    GET_STATUS = "get_status"


class MeoEvents:
    FEATURE_RESPONSE = "feature_response"
    STATUS = "status"
    BATTERY = "battery"
    CONNECTIVITY = "connectivity"
    AUDIO_STATE = "audio_state"
    EMOTION = "emotion"
    ERROR = "error"
    OTA_PROGRESS = "ota_progress"
    OTA_COMPLETE = "ota_complete"


# ============================================================================
# PTALK DEVICE SIMULATOR (MEO SDK COMPATIBLE)
# ============================================================================
class PTalkMeoDevice:
    """
    PTalk device simulator with full MEO SDK MQTT protocol support.
    
    Topic patterns:
    - Feature invoke: meo/{userId}/{deviceId}/feature
    - Legacy invoke: meo/{deviceId}/feature/{featureName}/invoke
    - Events: meo/{userId}/{deviceId}/event/{eventName}
    - OTA data: meo/{userId}/{deviceId}/ota_data (or legacy devices/{MAC}/ota_data)
    """
    
    FIRMWARE_VERSION = "v2.1-meo-sim"
    PRODUCT_ID = "ptalk-v1"
    DEVICE_MODEL = "PTalk-Sim"
    DEVICE_MANUFACTURER = "PTalk"
    
    def __init__(self, device_id: str, user_id: str, ws_url: str, mqtt_host: str, 
                 mqtt_port: int = 1883, tx_key: str = ""):
        self.device_id = device_id
        self.user_id = user_id
        self.ws_url = ws_url
        self.mqtt_host = mqtt_host
        self.mqtt_port = mqtt_port
        self.tx_key = tx_key  # MQTT password
        
        # MEO topic patterns
        self.feature_topic = f"meo/{self.user_id}/{self.device_id}/feature"
        self.event_base = f"meo/{self.user_id}/{self.device_id}/event"
        
        # Legacy topic (backward compatibility)
        self.legacy_topic = f"devices/{self.device_id}"
        
        # Audio
        self.pa = pyaudio.PyAudio()
        self.codec = IMA_ADPCM_Codec()
        self.mic_queue = asyncio.Queue()
        self.spk_stream = None
        
        # Device state
        self.running = True
        self.is_listening = False
        self.is_speaking = False
        self.volume = 60
        self.brightness = 100
        self.battery = 85
        self.device_name = "PTalk-Sim"
        self.emotion_code = "00"  # neutral
        self.start_time = time.time()
        
        # OTA state
        self.ota_expected_seq = 0
        self.ota_total_chunks = 0
        self.ota_received_bytes = 0
        self.ota_total_size = 0
        
        # MQTT
        self.ws = None
        self.mqtt_client = None
        self.mqtt_connected = False

    # =========================================================================
    # MQTT SETUP & HANDLERS
    # =========================================================================
    
    def _setup_mqtt(self):
        """Setup MQTT client with MEO SDK compatible configuration."""
        # Use CallbackAPIVersion.VERSION2 for paho-mqtt >= 2.0
        try:
            self.mqtt_client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION2, 
                client_id=f"PTalk_{self.device_id}"
            )
        except (AttributeError, TypeError):
            # Fallback for older paho-mqtt versions
            self.mqtt_client = mqtt.Client(client_id=f"PTalk_{self.device_id}")
        
        # Set MQTT credentials (device_id as username, tx_key as password)
        if self.tx_key:
            self.mqtt_client.username_pw_set(self.device_id, self.tx_key)
        
        self.mqtt_client.on_connect = self._on_mqtt_connect
        self.mqtt_client.on_disconnect = self._on_mqtt_disconnect
        self.mqtt_client.on_message = self._on_mqtt_message
        
        try:
            logger.info(f"Connecting to MQTT broker at {self.mqtt_host}:{self.mqtt_port}...")
            self.mqtt_client.connect(self.mqtt_host, self.mqtt_port, keepalive=60)
            self.mqtt_client.loop_start()
        except Exception as e:
            logger.error(f"Failed to connect to MQTT broker: {e}")
            logger.warning("Continuing without MQTT connection. Some features may not work.")

    def _on_mqtt_connect(self, client, userdata, flags, reason_code, properties=None):
        """Handle MQTT connection - subscribe to feature topics."""
        logger.info(f"MQTT Connected (rc={reason_code})")
        self.mqtt_connected = True
        
        # Subscribe to MEO SDK topics
        client.subscribe(self.feature_topic, qos=1)
        logger.info(f"Subscribed to: {self.feature_topic}")
        
        # Subscribe to legacy feature invoke pattern
        legacy_pattern = f"meo/{self.device_id}/feature/+/invoke"
        client.subscribe(legacy_pattern, qos=1)
        logger.info(f"Subscribed to: {legacy_pattern}")
        
        # Subscribe to OTA data topics
        client.subscribe(f"{self.legacy_topic}/ota_data", qos=1)
        client.subscribe(f"meo/{self.user_id}/{self.device_id}/ota_data", qos=1)
        
        # Publish initial status event
        self.publish_status_event()

    def _on_mqtt_disconnect(self, client, userdata, disconnect_flags, reason_code, properties=None):
        """Handle MQTT disconnection."""
        logger.warning(f"MQTT Disconnected (rc={reason_code})")
        self.mqtt_connected = False

    def _on_mqtt_message(self, client, userdata, msg):
        """Handle incoming MQTT messages."""
        topic = msg.topic
        payload = msg.payload
        
        logger.debug(f"MQTT Message: {topic}")
        
        try:
            # Handle OTA binary data
            if topic.endswith("/ota_data"):
                self._handle_ota_chunk(payload)
                return
            
            # Handle feature invoke (JSON)
            payload_str = payload.decode('utf-8')
            data = json.loads(payload_str)
            
            # Determine feature name
            feature_name = None
            params = {}
            invoke_id = data.get("invoke_id", "")
            
            if topic == self.feature_topic:
                # Cloud-compatible format: {"feature": "name", "params": {...}}
                feature_name = data.get("feature") or data.get("feature_name")
                params = data.get("params", {})
            elif "/feature/" in topic and topic.endswith("/invoke"):
                # Legacy format: meo/{deviceId}/feature/{featureName}/invoke
                parts = topic.split("/")
                feature_idx = parts.index("feature")
                feature_name = parts[feature_idx + 1]
                params = data.get("params", {})
            
            if feature_name:
                self._handle_feature_invoke(feature_name, params, invoke_id)
                
        except json.JSONDecodeError as e:
            logger.error(f"JSON decode error: {e}")
        except Exception as e:
            logger.error(f"Error handling MQTT message: {e}")

    # =========================================================================
    # FEATURE HANDLERS
    # =========================================================================
    
    def _handle_feature_invoke(self, feature: str, params: Dict[str, Any], invoke_id: str = ""):
        """Handle a feature invocation from server."""
        logger.info(f"Feature invoke: {feature} params={params}")
        
        success = True
        message = ""
        response_data = {}
        
        try:
            if feature == MeoFeatures.SET_VOLUME:
                volume = int(params.get("volume", self.volume))
                self.volume = max(0, min(100, volume))
                message = f"Volume set to {self.volume}"
                response_data = {"volume": str(self.volume)}
                
            elif feature == MeoFeatures.SET_BRIGHTNESS:
                brightness = int(params.get("brightness", self.brightness))
                self.brightness = max(0, min(100, brightness))
                message = f"Brightness set to {self.brightness}"
                response_data = {"brightness": str(self.brightness)}
                
            elif feature == MeoFeatures.SET_DEVICE_NAME:
                self.device_name = params.get("device_name", self.device_name)
                message = f"Device name set to {self.device_name}"
                response_data = {"device_name": self.device_name}
                
            elif feature == MeoFeatures.SET_EMOTION:
                self.emotion_code = params.get("code", "00")
                message = f"Emotion set to {self.emotion_code}"
                response_data = {"code": self.emotion_code}
                self.publish_event(MeoEvents.EMOTION, {"code": self.emotion_code})
                
            elif feature == MeoFeatures.REQUEST_STATUS or feature == MeoFeatures.GET_STATUS:
                self.publish_status_event()
                message = "Status published"
                
            elif feature == MeoFeatures.REBOOT:
                message = "Rebooting..."
                logger.warning("Reboot requested!")
                self.running = False
                
            elif feature == MeoFeatures.REQUEST_BLE_CONFIG:
                message = "BLE config mode not available in simulator"
                success = False
                
            elif feature == MeoFeatures.REQUEST_OTA:
                self.ota_total_size = int(params.get("size", 0))
                self.ota_total_chunks = int(params.get("total_chunks", 0))
                self.ota_expected_seq = 0
                self.ota_received_bytes = 0
                message = f"OTA initiated, expecting {self.ota_total_chunks} chunks"
                logger.info(message)
                
            elif feature == MeoFeatures.PLAY_TTS:
                text = params.get("text", "")
                voice = params.get("voice", "default")
                message = f"Playing TTS: '{text}' with voice '{voice}'"
                logger.info(message)
                self.publish_event(MeoEvents.AUDIO_STATE, {"state": "playing"})
                
            elif feature == MeoFeatures.STOP_AUDIO:
                message = "Audio stopped"
                self.is_speaking = False
                self.publish_event(MeoEvents.AUDIO_STATE, {"state": "stopped"})
                
            else:
                success = False
                message = f"Unknown feature: {feature}"
                logger.warning(message)
                
        except Exception as e:
            success = False
            message = str(e)
            logger.error(f"Feature {feature} failed: {e}")
        
        # Send feature response
        self._send_feature_response(feature, success, message, invoke_id, response_data)

    def _send_feature_response(self, feature: str, success: bool, message: str, 
                                invoke_id: str = "", data: Dict[str, str] = None):
        """Send feature response event."""
        response = {
            "feature_name": feature,
            "device_id": self.device_id,
            "success": success,
            "message": message,
        }
        if invoke_id:
            response["invoke_id"] = invoke_id
        if data:
            response["data"] = data
            
        self.publish_event(MeoEvents.FEATURE_RESPONSE, response)

    # =========================================================================
    # EVENT PUBLISHING
    # =========================================================================
    
    def publish_event(self, event_name: str, data: Dict[str, Any]):
        """Publish a device event to MQTT."""
        if not self.mqtt_client or not self.mqtt_connected:
            logger.warning("MQTT not connected, cannot publish event")
            return
        
        topic = f"{self.event_base}/{event_name}"
        payload = {
            "event": event_name,
            "device_id": self.device_id,
            **data
        }
        
        retain = (event_name == MeoEvents.STATUS)
        self.mqtt_client.publish(topic, json.dumps(payload), qos=1, retain=retain)
        logger.debug(f"Published event: {event_name}")

    def publish_status_event(self):
        """Publish full device status."""
        uptime = int(time.time() - self.start_time)
        
        status_data = {
            "device_id": self.device_id,
            "device_name": self.device_name,
            "firmware_version": self.FIRMWARE_VERSION,
            "product_id": self.PRODUCT_ID,
            "device_model": self.DEVICE_MODEL,
            "manufacturer": self.DEVICE_MANUFACTURER,
            "battery_percent": self.battery,
            "battery_charging": False,
            "connectivity_state": "ONLINE",
            "rssi": random.randint(-70, -40),
            "volume": self.volume,
            "brightness": self.brightness,
            "emotion_code": self.emotion_code,
            "uptime_sec": uptime,
        }
        
        self.publish_event(MeoEvents.STATUS, status_data)
        logger.info("Status event published")

    # =========================================================================
    # OTA HANDLING
    # =========================================================================
    
    def _handle_ota_chunk(self, data: bytes):
        """Handle OTA binary chunk."""
        if len(data) < 12:
            logger.error("OTA chunk too small")
            return
        
        # Parse header (12 bytes, little endian)
        seq, size, recv_crc = struct.unpack("<III", data[:12])
        chunk_data = data[12:]
        
        # Verify CRC
        calc_crc = binascii.crc32(chunk_data) & 0xFFFFFFFF
        
        if calc_crc == recv_crc and seq == self.ota_expected_seq:
            self.ota_received_bytes += len(chunk_data)
            progress = 0
            if self.ota_total_size > 0:
                progress = int(self.ota_received_bytes * 100 / self.ota_total_size)
            
            logger.info(f"OTA Chunk {seq} OK ({progress}%)")
            
            # Send ACK
            ack_topic = f"{self.legacy_topic}/ota_ack"
            self.mqtt_client.publish(ack_topic, json.dumps({"ota_ack": seq}))
            
            # Publish progress event
            self.publish_event(MeoEvents.OTA_PROGRESS, {
                "percent": progress,
                "bytes_received": self.ota_received_bytes,
                "current_chunk": seq,
                "total_chunks": self.ota_total_chunks
            })
            
            self.ota_expected_seq = seq + 1
            
            # Check if complete
            if self.ota_total_chunks > 0 and seq >= self.ota_total_chunks - 1:
                self.publish_event(MeoEvents.OTA_COMPLETE, {
                    "success": True,
                    "message": "OTA update complete (simulated)"
                })
                logger.info("OTA complete!")
        else:
            error_msg = "CRC mismatch" if calc_crc != recv_crc else f"Seq mismatch (expected {self.ota_expected_seq})"
            logger.error(f"OTA Chunk {seq} FAIL: {error_msg}")
            
            # Send NACK
            ack_topic = f"{self.legacy_topic}/ota_ack"
            self.mqtt_client.publish(ack_topic, json.dumps({
                "ota_nack": seq,
                "expected_seq": self.ota_expected_seq
            }))

    # =========================================================================
    # AUDIO HANDLERS
    # =========================================================================
    
    def mic_callback(self, in_data, frame_count, time_info, status):
        """PyAudio callback for microphone input."""
        if self.is_listening:
            self.mic_queue.put_nowait(in_data)
        return (None, pyaudio.paContinue)

    async def mic_sender(self):
        """Send microphone data to WebSocket."""
        while self.running:
            try:
                pcm_data = await asyncio.wait_for(self.mic_queue.get(), timeout=1.0)
                if self.is_listening and self.ws:
                    adpcm = self.codec.encode(pcm_data)
                    try:
                        await self.ws.send(adpcm)
                    except Exception as e:
                        logger.error(f"Error sending audio: {e}")
            except asyncio.TimeoutError:
                continue

    # =========================================================================
    # WEBSOCKET HANDLERS
    # =========================================================================
    
    async def ws_handler(self):
        """Handle WebSocket connection for audio streaming."""
        while self.running:
            try:
                async with websockets.connect(self.ws_url) as ws:
                    self.ws = ws
                    logger.info("WebSocket Connected")
                    
                    # Send handshake
                    await ws.send(json.dumps({
                        "cmd": "device_handshake",
                        "device_id": self.device_id,
                        "user_id": self.user_id,
                        "firmware_version": self.FIRMWARE_VERSION,
                        "battery_percent": self.battery
                    }))
                    
                    async for message in ws:
                        if isinstance(message, bytes):
                            # Binary audio data
                            if self.is_speaking and self.spk_stream:
                                pcm = self.codec.decode(message)
                                self.spk_stream.write(pcm)
                        else:
                            # Text command
                            self._handle_ws_text(message)
                            
            except websockets.exceptions.ConnectionClosed:
                logger.warning("WebSocket connection closed")
            except Exception as e:
                logger.error(f"WebSocket error: {e}")
            
            if self.running:
                logger.info("Reconnecting WebSocket in 5 seconds...")
                await asyncio.sleep(5)

    def _handle_ws_text(self, msg: str):
        """Handle WebSocket text messages."""
        logger.info(f"WS Text: {msg}")
        
        # Handle audio state changes
        if msg in ["AUDIO_START", "SPEAKING", "SPEAK_START"]:
            self.is_speaking = True
            self.codec.dec_predictor = 0
            self.codec.dec_index = 0
            self.publish_event(MeoEvents.AUDIO_STATE, {"state": "speaking"})
        elif msg in ["IDLE", "SPEAK_END", "DONE", "TTS_END"]:
            self.is_speaking = False
            self.publish_event(MeoEvents.AUDIO_STATE, {"state": "idle"})

    # =========================================================================
    # CLI CONTROL
    # =========================================================================
    
    async def cli_input(self):
        """Handle command-line input for testing."""
        print("\n" + "=" * 60)
        print("PTALK MEO SIMULATOR (MEO SDK Compatible)")
        print("=" * 60)
        print(f"Device ID:  {self.device_id}")
        print(f"User ID:    {self.user_id}")
        print(f"MQTT:       {self.mqtt_host}:{self.mqtt_port}")
        print(f"WebSocket:  {self.ws_url}")
        print("-" * 60)
        print("Commands:")
        print("  L - Toggle microphone (Listen)")
        print("  S - Publish status")
        print("  V - Set volume (e.g., 'V 75')")
        print("  B - Set brightness (e.g., 'B 80')")
        print("  E - Set emotion (e.g., 'E 01')")
        print("  Q - Quit")
        print("=" * 60 + "\n")
        
        loop = asyncio.get_event_loop()
        while self.running:
            try:
                cmd = await loop.run_in_executor(None, input, "DEVICE> ")
                cmd = cmd.strip()
                
                if not cmd:
                    continue
                    
                parts = cmd.upper().split()
                action = parts[0]
                
                if action == 'L':
                    self.is_listening = not self.is_listening
                    if self.ws:
                        await self.ws.send("START" if self.is_listening else "END")
                    state = "ON" if self.is_listening else "OFF"
                    logger.info(f"Microphone: {state}")
                    self.publish_event(MeoEvents.AUDIO_STATE, {
                        "state": "listening" if self.is_listening else "idle"
                    })
                    
                elif action == 'S':
                    self.publish_status_event()
                    
                elif action == 'V' and len(parts) > 1:
                    try:
                        self.volume = max(0, min(100, int(parts[1])))
                        logger.info(f"Volume set to {self.volume}")
                        self.publish_status_event()
                    except ValueError:
                        print("Invalid volume value")
                        
                elif action == 'B' and len(parts) > 1:
                    try:
                        self.brightness = max(0, min(100, int(parts[1])))
                        logger.info(f"Brightness set to {self.brightness}")
                        self.publish_status_event()
                    except ValueError:
                        print("Invalid brightness value")
                        
                elif action == 'E' and len(parts) > 1:
                    self.emotion_code = parts[1][:2].zfill(2)
                    logger.info(f"Emotion set to {self.emotion_code}")
                    self.publish_event(MeoEvents.EMOTION, {"code": self.emotion_code})
                    
                elif action == 'Q':
                    self.running = False
                    
                else:
                    print(f"Unknown command: {cmd}")
                    
            except EOFError:
                break

    # =========================================================================
    # MAIN RUN LOOP
    # =========================================================================
    
    async def run(self):
        """Main run loop."""
        # Setup MQTT
        self._setup_mqtt()
        
        # Setup audio streams
        self.spk_stream = self.pa.open(
            format=pyaudio.paInt16, 
            channels=1, 
            rate=16000, 
            output=True
        )
        mic_stream = self.pa.open(
            format=pyaudio.paInt16, 
            channels=1, 
            rate=16000, 
            input=True,
            frames_per_buffer=1024, 
            stream_callback=self.mic_callback
        )
        
        try:
            # Run all handlers concurrently
            await asyncio.gather(
                self.ws_handler(),
                self.mic_sender(),
                self.cli_input()
            )
        finally:
            # Cleanup
            mic_stream.stop_stream()
            mic_stream.close()
            self.spk_stream.stop_stream()
            self.spk_stream.close()
            self.pa.terminate()
            
            if self.mqtt_client:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()


# ============================================================================
# MAIN
# ============================================================================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PTalk MEO Device Simulator")
    parser.add_argument("--id", default="SIM001", 
                        help="Device ID (default: SIM001)")
    parser.add_argument("--user_id", default="default", 
                        help="MEO User ID (default: default)")
    parser.add_argument("--ws", default="ws://171.226.10.121:8000/ws", 
                        help="WebSocket URL")
    parser.add_argument("--mqtt", default="171.226.10.121", 
                        help="MQTT broker host")
    parser.add_argument("--mqtt_port", type=int, default=1883, 
                        help="MQTT broker port (default: 1883)")
    parser.add_argument("--tx_key", default="", 
                        help="MQTT password (tx_key)")
    
    args = parser.parse_args()
    
    device = PTalkMeoDevice(
        device_id=args.id,
        user_id=args.user_id,
        ws_url=args.ws,
        mqtt_host=args.mqtt,
        mqtt_port=args.mqtt_port,
        tx_key=args.tx_key
    )
    
    try:
        asyncio.run(device.run())
    except KeyboardInterrupt:
        print("\nShutting down...")
