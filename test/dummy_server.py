import asyncio
import random
import wave
import os
import struct
from datetime import datetime

from fastapi import FastAPI, WebSocket
import uvicorn

app = FastAPI()

# ================= CONFIG =================
EMOTIONS = ["00", "01", "10"]

CHUNK_BYTES = 512              # khớp với ESP32
INPUT_FOLDER = "input"
OUTPUT_FOLDER = "output"
os.makedirs(OUTPUT_FOLDER, exist_ok=True)

RECV_MIC_SECONDS = 10.0        # thời gian thu mic (giây)

# tốc độ tự nhiên của ADPCM ~ 8000 bytes/s (16kHz, 16-bit, mono, nén 4:1)
REAL_CHUNK_DELAY = 0.06  # ≈ 0.064s


# ================= ADPCM TABLES =================
step_table = [
    7, 8, 9, 10, 11, 12, 13, 14,
    16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
]

index_table = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
]


# ================= ADPCM ENCODER (server -> ESP) =================
class ADPCMEncoder:
    def __init__(self):
        self.predictor = 0
        self.step_index = 0
        self.high_nibble = True
        self.nibble = 0
        self.out_bytes = bytearray()

    def encode_sample(self, sample: int):
        step = step_table[self.step_index]
        diff = sample - self.predictor
        code = 0

        if diff < 0:
            code |= 8
            diff = -diff

        if diff >= step:
            code |= 4
            diff -= step
        if diff >= step // 2:
            code |= 2
            diff -= step // 2
        if diff >= step // 4:
            code |= 1

        delta = step >> 3
        if code & 4:
            delta += step
        if code & 2:
            delta += step >> 1
        if code & 1:
            delta += step >> 2

        if code & 8:
            self.predictor -= delta
        else:
            self.predictor += delta

        # clamp
        # if self.predictor > 32:
        #     self.predictor = 30000
        # elif self.predictor < -30000:
        #     self.predictor = -30000

        self.step_index += index_table[code]
        if self.step_index < 0:
            self.step_index = 0
        elif self.step_index > 88:
            self.step_index = 88

        # pack 2 nibble / byte  
        if self.high_nibble:
            self.nibble = code & 0x0F
            self.high_nibble = False
        else:
            self.out_bytes.append(self.nibble | ((code & 0x0F) << 4))
            self.high_nibble = True

    def encode_block(self, pcm_samples):
        self.out_bytes = bytearray()
        self.high_nibble = True
        for s in pcm_samples:
            self.encode_sample(s)
        if not self.high_nibble:
            self.out_bytes.append(self.nibble)  # flush odd nibble
        return bytes(self.out_bytes)


# ================= ADPCM DECODER (ESP -> server) =================
class ADPCMDecoder:
    def __init__(self):
        self.predictor = 0
        self.step_index = 0

    def decode(self, adpcm_bytes: bytes):
        pcm_out = []
        predictor = self.predictor
        index = self.step_index

        for b in adpcm_bytes:
            # low nibble, high nibble
            for shift in (0, 4):
                code = (b >> shift) & 0x0F
                step = step_table[index]

                diffq = step >> 3
                if code & 4:
                    diffq += step
                if code & 2:
                    diffq += step >> 1
                if code & 1:
                    diffq += step >> 2

                if code & 8:
                    predictor -= diffq
                else:
                    predictor += diffq

                if predictor > 32767:
                    predictor = 32767
                elif predictor < -32768:
                    predictor = -32768

                index += index_table[code]
                if index < 0:
                    index = 0
                elif index > 88:
                    index = 88

                pcm_out.append(predictor)

        self.predictor = predictor
        self.step_index = index
        return pcm_out
# ... (Keep imports and Config config top of file exactly as they are) ...
# ... (Keep ADPCMEncoder and ADPCMDecoder classes exactly as they are) ...

# Name of the file you want to send to ESP32
RESPONSE_FILENAME = "Em Không Cần.wav" 

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    print("Client connected")

    # Increase recv time if needed, e.g., 20.0
    RECV_MIC_SECONDS = 1.0

    try:
        while True:
            loop = asyncio.get_event_loop()

            # ==============================================================
            # 1️⃣ & 2️⃣ THU MIC (Receive from ESP32) & LƯU FILE (Save Input)
            # ==============================================================
            # (Logic này giữ nguyên để bạn vẫn nhận được giọng nói từ ESP32)
            print(f"=== BAT DAU THU MIC ({RECV_MIC_SECONDS}s) ===")
            decoder = ADPCMDecoder()
            recorded_pcm = []

            start_time = loop.time()
            while loop.time() - start_time < RECV_MIC_SECONDS:
                try:
                    msg = await asyncio.wait_for(websocket.receive(), timeout=0.25)
                except asyncio.TimeoutError:
                    continue

                if msg["type"] == "websocket.disconnect":
                    print("Client disconnected while recording")
                    return

                data_bytes = msg.get("bytes")
                if data_bytes:
                    pcm_chunk = decoder.decode(data_bytes)
                    recorded_pcm.extend(pcm_chunk)

            print(f"=== KET THUC THU MIC, saved {len(recorded_pcm)} samples ===")

            # Lưu file mic thu được (Input)
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            out_path = os.path.join(OUTPUT_FOLDER, f"mic_{timestamp}.wav")
            with wave.open(out_path, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(16000)
                if recorded_pcm:
                    pcm_bytes = struct.pack("<{}h".format(len(recorded_pcm)), *recorded_pcm)
                else:
                    pcm_bytes = b""
                wf.writeframes(pcm_bytes)

            # ==============================================================
            # 3️⃣ GỬI TRẠNG THÁI (Send Status)
            # ==============================================================
            await websocket.send_text("PROCESSING_START")
            await websocket.send_text(random.choice(EMOTIONS))
            await websocket.send_text("LISTENING")

            # ==============================================================
            # 4️⃣ [CHANGED] ĐỌC FILE WAV TỪ SERVER & GỬI CHO ESP32
            # ==============================================================
            
            # Kiểm tra file có tồn tại không
            if os.path.exists(RESPONSE_FILENAME):
                print(f"=== Phat file: {RESPONSE_FILENAME} ===")
                
                pcm_to_send = []

                # Mở file WAV
                with wave.open(RESPONSE_FILENAME, "rb") as wf:
                    # Validate format (Bắt buộc phải đúng format ESP32 hỗ trợ)
                    if wf.getnchannels() != 1 or wf.getframerate() != 16000 or wf.getsampwidth() != 2:
                        print("❌ ERROR: File response.wav phai la 16kHz, 16-bit, Mono!")
                    else:
                        # Đọc toàn bộ dữ liệu file
                        # raw_bytes = wf.readframes(wf.getnframes())
                        # total_samples = len(raw_bytes) // 2
                        
                        # # Convert bytes thành mảng số nguyên (short int)
                        # # '<' means little-endian, 'h' means short (2 bytes)
                        # pcm_to_send = struct.unpack(f"<{total_samples}h", raw_bytes)
                        raw_bytes = wf.readframes(wf.getnframes())
                        total_samples = len(raw_bytes) // 2
                        
                        # Unpack returns a tuple (immutable), so we convert it to a list later
                        raw_pcm = struct.unpack(f"<{total_samples}h", raw_bytes)

                        # =================================================
                        # 🔉 REDUCE VOLUME HERE
                        # =================================================
                        VOLUME_FACTOR = 2.0  # 0.2 = 20% volume, 0.5 = 50%, 1.0 = 100%
                        
                        # Multiply every sample by the factor and cast back to int
                        pcm_to_send = [int(sample * VOLUME_FACTOR) for sample in raw_pcm]
                if pcm_to_send:
                    encoder = ADPCMEncoder()
                    # Nén PCM -> ADPCM
                    adpcm_bytes = encoder.encode_block(pcm_to_send)

                    total = len(adpcm_bytes)
                    sent = 0
                    print(f"--> Sending {total} bytes ADPCM to ESP32...")

                    while sent < total:
                        end = min(sent + CHUNK_BYTES, total)
                        # Gửi chunk xuống ESP
                        await websocket.send_bytes(adpcm_bytes[sent:end])
                        sent = end
                        # Delay để ESP32 kịp xử lý buffer
                        await asyncio.sleep(REAL_CHUNK_DELAY)
                else:
                    print("⚠ File WAV rỗng hoặc lỗi format.")
            else:
                print(f"⚠ Khong tim thay file: {RESPONSE_FILENAME}")

            # ==============================================================
            # 5️⃣ BÁO HOÀN TẤT
            # ==============================================================
            await websocket.send_text("TTS_END")

    except Exception as e:
        print("Connection error:", e)
        try:
            await websocket.send_text("TTS_END")
        except:
            pass
    finally:
        try:
            await websocket.close()
        except:
            pass
        print("WebSocket closed")
