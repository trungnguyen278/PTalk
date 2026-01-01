import asyncio
import wave
import os
from datetime import datetime
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
import uvicorn

# ================== IMA ADPCM TABLES ==================

STEP_TABLE = [
     7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
]

INDEX_TABLE = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
]

# ================== ADPCM DECODER ==================

def adpcm_decode(adpcm_bytes, state):
    if state is None:
        predictor = 0
        index = 0
    else:
        predictor, index = state

    pcm = bytearray()

    for b in adpcm_bytes:
        for nibble in (b & 0x0F, b >> 4):
            step = STEP_TABLE[index]
            diff = step >> 3

            if nibble & 1: diff += step >> 2
            if nibble & 2: diff += step >> 1
            if nibble & 4: diff += step
            if nibble & 8: diff = -diff

            predictor += diff
            predictor = max(-32768, min(32767, predictor))

            index += INDEX_TABLE[nibble & 0x0F]
            index = max(0, min(88, index))

            pcm += int(predictor).to_bytes(2, "little", signed=True)

    return bytes(pcm), (predictor, index)


# ================== ADPCM ENCODER ==================

def adpcm_encode(pcm_bytes, state):
    if state is None:
        predictor = 0
        index = 0
    else:
        predictor, index = state

    adpcm = bytearray()
    pcm_samples = [
        int.from_bytes(pcm_bytes[i:i+2], "little", signed=True)
        for i in range(0, len(pcm_bytes), 2)
    ]

    nibble_buffer = 0
    high = False

    for sample in pcm_samples:
        step = STEP_TABLE[index]
        diff = sample - predictor
        code = 0

        if diff < 0:
            code |= 8
            diff = -diff

        if diff >= step:
            code |= 4
            diff -= step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
        if diff >= step >> 2:
            code |= 1

        delta = step >> 3
        if code & 1: delta += step >> 2
        if code & 2: delta += step >> 1
        if code & 4: delta += step
        if code & 8: delta = -delta

        predictor += delta
        predictor = max(-32768, min(32767, predictor))

        index += INDEX_TABLE[code]
        index = max(0, min(88, index))

        if not high:
            nibble_buffer = code & 0x0F
            high = True
        else:
            adpcm.append((code << 4) | nibble_buffer)
            high = False

    if high:
        adpcm.append(nibble_buffer)

    return bytes(adpcm), (predictor, index)


HOST = "0.0.0.0"
PORT = 8000

FRAME_ADPCM = 512
SAMPLE_RATE = 16000
SEND_INTERVAL = 0.06

RECORD_DIR = "recordings"
os.makedirs(RECORD_DIR, exist_ok=True)

app = FastAPI()

def log(tag, msg):
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {tag} {msg}")

@app.websocket("/ws")
async def ws_handler(ws: WebSocket):
    await ws.accept()
    log("📡", "ESP32 connected")

    rx_state = None
    pcm_chunks = []
    recording = False

    try:
        while True:
            data = await ws.receive()

            if "bytes" in data:
                adpcm = data["bytes"]
                log("⬆️ RX", f"{len(adpcm)} bytes")

                if recording:
                    pcm, rx_state = adpcm_decode(adpcm, rx_state)
                    pcm_chunks.append(pcm)
                    log("🔓", f"Decoded {len(pcm)} PCM bytes")

            elif "text" in data:
                msg = data["text"]
                log("📩 RX", msg)

                if msg == "START":
                    pcm_chunks.clear()
                    rx_state = None
                    recording = True
                    log("🎙️", "START")

                elif msg == "END":
                    recording = False
                    wav = save_wav(pcm_chunks)
                    log("💾", f"WAV saved {wav}")
                    asyncio.create_task(send_wav(ws, wav))

    except WebSocketDisconnect:
        log("🔌", "Disconnected")


def save_wav(pcm_chunks):
    path = os.path.join(
        RECORD_DIR,
        f"rec_{datetime.now().strftime('%H%M%S')}.wav"
    )

    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(b"".join(pcm_chunks))

    return path


async def send_wav(ws: WebSocket, path):
    await ws.send_text("PROCESSING_START")
    await ws.send_text("01")
    await ws.send_text("SPEAK_START")

    tx_state = None

    with wave.open(path, "rb") as wf:
        while True:
            pcm = wf.readframes(256)  # 512 bytes PCM
            if not pcm:
                break

            adpcm, tx_state = adpcm_encode(pcm, tx_state)

            if len(adpcm) < FRAME_ADPCM:
                adpcm += bytes(FRAME_ADPCM - len(adpcm))

            await ws.send_bytes(adpcm)
            log("⬇️ TX", f"{len(adpcm)} bytes")
            await asyncio.sleep(SEND_INTERVAL)

    await ws.send_text("TTS_END")
    log("🏁", "Playback done")


if __name__ == "__main__":
    log("🚀", f"Server ws://{HOST}:{PORT}/ws")
    uvicorn.run(app, host=HOST, port=PORT)
