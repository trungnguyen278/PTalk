import asyncio
import random
import wave
import os
import struct
from datetime import datetime

from fastapi import FastAPI, WebSocket
import uvicorn

# ============================================================
# CONFIG
# ============================================================

SAMPLE_RATE = 16000
FRAME_BYTES = 512             # ESP expects exact 512-byte chunks
FRAME_SAMPLES = FRAME_BYTES // 2  # 256 samples per frame (16 ms @ 16kHz)
BURST_FRAMES = 20             # Send initial burst to fill buffer
PACE_SEC = 0.002              # 2ms pacing - fast enough to keep buffer full

RECV_MIC_SECONDS = 1.0
RESPONSE_FILENAME = "chẳng-phải-tình-đầu-sao-đau-đến-thế.wav"

INPUT_FOLDER = "input"
OUTPUT_FOLDER = "output"
os.makedirs(INPUT_FOLDER, exist_ok=True)
os.makedirs(OUTPUT_FOLDER, exist_ok=True)

EMOTIONS = ["00", "01", "10"]

app = FastAPI()

# No ADPCM anymore. We stream 16-bit PCM little-endian directly.

# ============================================================
# WEBSOCKET ENDPOINT
# ============================================================

@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    print("✅ Client connected")

    try:
        while True:
            # ====================================================
            # 1️⃣ RECEIVE MIC FROM ESP32
            # ====================================================
            pcm_in_bytes = bytearray()
            start = asyncio.get_event_loop().time()

            while asyncio.get_event_loop().time() - start < RECV_MIC_SECONDS:
                try:
                    msg = await asyncio.wait_for(ws.receive(), timeout=0.25)
                except asyncio.TimeoutError:
                    continue

                if msg["type"] == "websocket.disconnect":
                    return

                if msg.get("bytes"):
                    # Uplink now sends PCM16LE frames; append raw bytes
                    pcm_in_bytes.extend(msg["bytes"])

            if pcm_in_bytes:
                fn = f"mic_{datetime.now().strftime('%H%M%S')}.wav"
                with wave.open(os.path.join(OUTPUT_FOLDER, fn), "wb") as wf:
                    wf.setnchannels(1)
                    wf.setsampwidth(2)
                    wf.setframerate(SAMPLE_RATE)
                    wf.writeframes(bytes(pcm_in_bytes))

            # ====================================================
            # 2️⃣ SEND STATE
            # ====================================================
            await ws.send_text("PROCESSING_START")
            await ws.send_text(random.choice(EMOTIONS))
            await ws.send_text("LISTENING")

            # ====================================================
            # 3️⃣ STREAM WAV -> ESP32 (FRAME-BASED, PCM passthrough)
            # ====================================================
            if not os.path.exists(RESPONSE_FILENAME):
                print("❌ WAV not found")
                continue

            with wave.open(RESPONSE_FILENAME, "rb") as wf:
                if wf.getnchannels() != 1 or wf.getframerate() != SAMPLE_RATE or wf.getsampwidth() != 2:
                    print("❌ WAV format invalid")
                    continue

                raw = wf.readframes(wf.getnframes())  # PCM16LE bytes

            total_samples = len(raw) // 2
            print(f"▶ Streaming {total_samples} samples as PCM16LE")

            # Send initial burst without delay to fill buffer
            frame_count = 0
            for i in range(0, len(raw) - FRAME_BYTES, FRAME_BYTES):
                frame_bytes = raw[i:i+FRAME_BYTES]
                if len(frame_bytes) != FRAME_BYTES:
                    break
                await ws.send_bytes(frame_bytes)
                frame_count += 1
                
                # After initial burst, add small pacing to prevent overwhelming WS
                if frame_count > BURST_FRAMES:
                    await asyncio.sleep(PACE_SEC)

            # ====================================================
            # 4️⃣ DONE
            # ====================================================
            await ws.send_text("TTS_END")

    except Exception as e:
        print("❌ WS error:", e)
    finally:
        await ws.close()
        print("🔌 Client disconnected")

# ============================================================
# MAIN
# ============================================================

if __name__ == "__main__":
    uvicorn.run(
        "dummy_server:app",
        host="0.0.0.0",
        port=8080,
        log_level="info"
    )
