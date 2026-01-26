#!/usr/bin/env python3
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
from typing import Optional

# Cấu hình logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
logger = logging.getLogger("PTalkFullSim")

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
# FULL DEVICE SIMULATOR
# ============================================================================
class PTalkFullDevice:
    def __init__(self, device_id, ws_url, mqtt_host):
        self.device_id = device_id
        self.ws_url = ws_url
        self.mqtt_host = mqtt_host
        self.base_topic = f"devices/{self.device_id}"
        
        # Audio
        self.pa = pyaudio.PyAudio()
        self.codec = IMA_ADPCM_Codec()
        self.mic_queue = asyncio.Queue()
        
        # State
        self.running = True
        self.is_listening = False
        self.is_speaking = False
        self.volume = 60
        self.brightness = 100
        self.battery = 85
        self.ota_expected_seq = 0
        
        self.ws = None
        self.mqtt_client = None

    # --- MQTT Handlers ---
    def _setup_mqtt(self):
        self.mqtt_client = mqtt.Client(client_id=f"PTalk_Sim_{self.device_id}")
        self.mqtt_client.on_connect = self._on_mqtt_connect
        self.mqtt_client.on_message = self._on_mqtt_message
        self.mqtt_client.connect(self.mqtt_host, 1883)
        self.mqtt_client.loop_start()

    def _on_mqtt_connect(self, client, userdata, flags, rc):
        logger.info(f"MQTT Connected (rc={rc})")
        client.subscribe(f"{self.base_topic}/cmd")
        client.subscribe(f"{self.base_topic}/ota_data")
        self.publish_status()

    def _on_mqtt_message(self, client, userdata, msg):
        topic = msg.topic
        payload = msg.payload
        if topic.endswith("/cmd"):
            self.handle_mqtt_command(json.loads(payload.decode()))
        elif topic.endswith("/ota_data"):
            self.handle_ota_chunk(payload)

    def handle_mqtt_command(self, data):
        cmd = data.get("cmd")
        logger.info(f"MQTT Cmd: {cmd}")
        if cmd == "set_volume":
            self.volume = data.get("volume", self.volume)
            self.publish_status()
        elif cmd == "set_brightness":
            self.brightness = data.get("brightness", self.brightness)
            self.publish_status()
        elif cmd == "request_status":
            self.publish_status()
        elif cmd == "reboot":
            logger.warning("Reboot requested!")
            self.running = False
        elif cmd == "request_ota":
            self.ota_expected_seq = 0
            self.mqtt_client.publish(f"{self.base_topic}/status", json.dumps({"status": "ok", "message": "Ready for OTA"}))

    def handle_ota_chunk(self, data):
        if len(data) < 12: return
        seq, size, recv_crc = struct.unpack("<III", data[:12])
        chunk_data = data[12:]
        calc_crc = binascii.crc32(chunk_data) & 0xFFFFFFFF
        if calc_crc == recv_crc:
            logger.info(f"OTA Chunk {seq} OK")
            self.mqtt_client.publish(f"{self.base_topic}/ota_ack", json.dumps({"ota_ack": seq}))
            self.ota_expected_seq = seq + 1
        else:
            logger.error(f"OTA CRC Fail at {seq}")
            self.mqtt_client.publish(f"{self.base_topic}/ota_ack", json.dumps({"ota_nack": seq, "expected_seq": self.ota_expected_seq}))

    def publish_status(self):
        status = {
            "status": "ok",
            "device_id": self.device_id,
            "device_name": "PC-Full-Sim",
            "battery_percent": self.battery,
            "connectivity_state": "ONLINE",
            "volume": self.volume,
            "brightness": self.brightness,
            "uptime_sec": int(time.time() % 10000)
        }
        self.mqtt_client.publish(f"{self.base_topic}/status", json.dumps(status), retain=True)

    # --- Audio Handlers ---
    def mic_callback(self, in_data, frame_count, time_info, status):
        if self.is_listening:
            self.mic_queue.put_nowait(in_data)
        return (None, pyaudio.paContinue)

    async def mic_sender(self):
        while self.running:
            pcm_data = await self.mic_queue.get()
            if self.is_listening and self.ws:
                adpcm = self.codec.encode(pcm_data)
                try: await self.ws.send(adpcm)
                except: pass

    # --- WebSocket Handlers ---
    async def ws_handler(self):
        while self.running:
            try:
                async with websockets.connect(self.ws_url) as ws:
                    self.ws = ws
                    logger.info("WebSocket Connected")
                    # Handshake khớp NetworkManager.cpp
                    await ws.send(json.dumps({
                        "cmd": "device_handshake",
                        "device_id": self.device_id,
                        "firmware_version": "v2.0-full",
                        "battery_percent": self.battery
                    }))
                    async for message in ws:
                        if isinstance(message, bytes):
                            if self.is_speaking:
                                pcm = self.codec.decode(message)
                                self.spk_stream.write(pcm)
                        else:
                            self.handle_ws_text(message)
            except Exception as e:
                logger.error(f"WS Error: {e}")
                await asyncio.sleep(5)

    def handle_ws_text(self, msg):
        logger.info(f"WS Text: {msg}")
        if msg in ["AUDIO_START", "SPEAKING", "SPEAK_START"]:
            self.is_speaking = True
            self.codec.dec_predictor = 0
            self.codec.dec_index = 0
        elif msg in ["IDLE", "SPEAK_END", "DONE", "TTS_END"]:
            self.is_speaking = False

    # --- Control ---
    async def cli_input(self):
        print("\n" + "="*50)
        print("PTALK FULL SIMULATOR (REAL AUDIO + MQTT)")
        print("Commands: 'L' (Listen/Mic), 'S' (Status), 'Q' (Quit)")
        print("="*50 + "\n")
        loop = asyncio.get_event_loop()
        while self.running:
            cmd = await loop.run_in_executor(None, input, "DEVICE> ")
            cmd = cmd.strip().upper()
            if cmd == 'L':
                self.is_listening = not self.is_listening
                if self.ws: await self.ws.send("START" if self.is_listening else "END")
                logger.info(f"MIC: {'ON' if self.is_listening else 'OFF'}")
            elif cmd == 'S':
                self.publish_status()
                logger.info("Status published to MQTT")
            elif cmd == 'Q':
                self.running = False

    async def run(self):
        self._setup_mqtt()
        self.spk_stream = self.pa.open(format=pyaudio.paInt16, channels=1, rate=16000, output=True)
        mic_stream = self.pa.open(format=pyaudio.paInt16, channels=1, rate=16000, input=True, 
                                 frames_per_buffer=1024, stream_callback=self.mic_callback)
        
        await asyncio.gather(self.ws_handler(), self.mic_sender(), self.cli_input())
        
        mic_stream.stop_stream(); mic_stream.close()
        self.spk_stream.stop_stream(); self.spk_stream.close()
        self.pa.terminate()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--id", default="FULL_SIM_001")
    parser.add_argument("--ws", default="ws://171.226.10.121:8000/ws")
    parser.add_argument("--mqtt", default="171.226.10.121")
    args = parser.parse_args()
    dev = PTalkFullDevice(args.id, args.ws, args.mqtt)
    try: asyncio.run(dev.run())
    except KeyboardInterrupt: pass