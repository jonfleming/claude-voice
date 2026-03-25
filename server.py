"""
Voice AI Pipeline Server

WebSocket server that:
1. Receives audio over WebSocket
2. Converts speech to text (STT) using local Whisper
3. Sends text to local LLM (Ollama)
4. Converts LLM response to speech (TTS) using local Piper
5. Sends audio back over WebSocket
"""

import asyncio
import base64
import json
import os
from pathlib import Path
from typing import Optional

import aiohttp
import numpy as np
import uvicorn
from dotenv import load_dotenv
from faster_whisper import WhisperModel
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse

load_dotenv()


# Configuration
OLLAMA_HOST = os.getenv("OLLAMA_HOST", "http://localhost:11434")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "llama3")
PIPER_MODEL = os.getenv("PIPER_MODEL", "en_US-lessac-medium.onnx")
PIPER_MODEL_DIR = os.getenv("PIPER_MODEL_DIR", "")
AUDIO_SAMPLE_RATE = int(os.getenv("AUDIO_SAMPLE_RATE", "16000"))
VAD_THRESHOLD = float(os.getenv("VAD_THRESHOLD", "1.5"))
VAD_MIN_SPEECH = float(os.getenv("VAD_MIN_SPEECH", "0.3"))
WS_HOST = os.getenv("WS_HOST", "0.0.0.0")
WS_PORT = int(os.getenv("WS_PORT", "8000"))

# Global models (loaded once)
whisper_model: Optional[WhisperModel] = None
piper_process: Optional[asyncio.subprocess.Process] = None


class ConnectionManager:
    """Manages WebSocket connections."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)


manager = ConnectionManager()

app = FastAPI()


async def load_whisper_model():
    """Load Whisper model for STT."""
    global whisper_model
    if whisper_model is None:
        # Use small model for speed, can change to medium/large for quality
        print("Loading Whisper model...")
        whisper_model = WhisperModel("small", device="cpu", compute_type="int8")
        print("Whisper model loaded")
    return whisper_model


def detect_silence(audio_data: bytes, sample_rate: int = 16000, threshold: float = 0.01) -> float:
    """Detect silence in audio. Returns duration of silence in seconds."""
    audio_np = np.frombuffer(audio_data, dtype=np.int16).astype(np.float32) / 32768.0

    # Calculate RMS energy
    window_size = sample_rate // 10  # 100ms windows
    energy = []

    for i in range(0, len(audio_np) - window_size, window_size):
        window = audio_np[i:i + window_size]
        rms = np.sqrt(np.mean(window ** 2))
        energy.append(rms)

    if not energy:
        return 0.0

    # Count consecutive silent windows
    silent_count = 0
    for e in energy:
        if e < threshold:
            silent_count += 1
        else:
            silent_count = 0

    # Return silent duration
    return (silent_count * window_size) / sample_rate


async def transcribe_audio(audio_data: bytes) -> str:
    """Convert audio bytes to text using Whisper."""
    if len(audio_data) < 1600:  # Less than 100ms of audio
        return ""

    model = await load_whisper_model()

    # Convert bytes to numpy array
    audio_np = np.frombuffer(audio_data, dtype=np.int16).astype(np.float32) / 32768.0

    # Transcribe
    segments, info = model.transcribe(audio_np, beam_size=5)
    text = " ".join([segment.text for segment in segments])

    return text.strip()


async def stream_to_ollama(prompt: str, websocket: WebSocket) -> str:
    """Send prompt to Ollama and stream the response with TTS."""
    global piper_process

    url = f"{OLLAMA_HOST}/api/generate"
    payload = {
        "model": OLLAMA_MODEL,
        "prompt": prompt,
        "stream": True
    }

    response_text = ""
    pending_text = ""

    try:
        async with aiohttp.ClientSession() as session:
            async with session.post(url, json=payload) as resp:
                if resp.status != 200:
                    error_text = await resp.text()
                    raise Exception(f"Ollama error: {error_text}")

                async for line in resp.content:
                    if line:
                        try:
                            data = json.loads(line)
                            if "response" in data:
                                token = data["response"]
                                response_text += token
                                pending_text += token

                                # Send token to client
                                await websocket.send_json({
                                    "type": "response",
                                    "content": token
                                })

                                # Generate TTS for every ~20 chars (word boundary approximation)
                                if len(pending_text) >= 20:
                                    audio = await text_to_speech(pending_text)
                                    if audio:
                                        audio_b64 = base64.b64encode(audio).decode()
                                        await websocket.send_json({
                                            "type": "audio",
                                            "data": audio_b64
                                        })
                                    pending_text = ""

                        except json.JSONDecodeError:
                            continue

                # Generate final TTS for remaining text
                if pending_text.strip():
                    audio = await text_to_speech(pending_text)
                    if audio:
                        audio_b64 = base64.b64encode(audio).decode()
                        await websocket.send_json({
                            "type": "audio",
                            "data": audio_b64
                        })

    except aiohttp.ClientError as e:
        raise Exception(f"Failed to connect to Ollama: {e}")

    return response_text


async def text_to_speech(text: str) -> Optional[bytes]:
    """Convert text to speech using Piper."""
    if not text.strip():
        return None

    global piper_process
    model_path = PIPER_MODEL
    if PIPER_MODEL_DIR:
        model_path = str(Path(PIPER_MODEL_DIR) / PIPER_MODEL)

    try:
        # Start Piper process if needed
        if piper_process is None or piper_process.returncode is not None:
            if piper_process and piper_process.returncode is not None:
                await piper_process.wait()
            piper_process = await asyncio.create_subprocess_exec(
                "piper",
                "--model", model_path,
                "--output_file", "-",
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )

        # Send text to Piper
        piper_process.stdin.write(text.encode() + b"\n")
        await piper_process.stdin.drain()

        # Read audio output
        audio_chunks = []
        try:
            while True:
                chunk = await asyncio.wait_for(piper_process.stdout.read(4096), timeout=5.0)
                if not chunk:
                    break
                audio_chunks.append(chunk)
        except asyncio.TimeoutError:
            pass

        return b"".join(audio_chunks)

    except FileNotFoundError:
        print("Piper not found. Make sure piper is in PATH.")
        return None
    except Exception as e:
        print(f"TTS error: {e}")
        return None


class AudioBuffer:
    """Audio buffer with VAD detection."""

    def __init__(self, vad_threshold: float = 1.5, min_speech: float = 0.3):
        self.buffer: list[bytes] = []
        self.vad_threshold = vad_threshold
        self.min_speech = min_speech
        self.last_audio_time: Optional[float] = None
        self.speech_start_time: Optional[float] = None

    def add(self, chunk: bytes, current_time: float):
        """Add audio chunk to buffer."""
        self.buffer.append(chunk)
        self.last_audio_time = current_time

    def get_audio(self) -> bytes:
        """Get all buffered audio."""
        return b"".join(self.buffer)

    def clear(self):
        """Clear the buffer."""
        self.buffer = []
        self.speech_start_time = None

    async def check_vad(self, current_time: float, loop: asyncio.AbstractEventLoop) -> bool:
        """Check if we should trigger transcription (VAD)."""
        if not self.buffer or not self.last_audio_time:
            return False

        # Time since last audio
        silence_duration = current_time - self.last_audio_time

        # Need at least threshold seconds of silence
        if silence_duration >= self.vad_threshold:
            # Check we have enough speech
            if self.speech_start_time:
                speech_duration = self.last_audio_time - self.speech_start_time
                if speech_duration >= self.min_speech:
                    return True
            else:
                # No speech start recorded, just transcribe what's there
                return True

        return False


async def handle_websocket(websocket: WebSocket):
    """Handle a WebSocket connection for the voice pipeline."""
    await manager.connect(websocket)

    audio_buffer = AudioBuffer(VAD_THRESHOLD, VAD_MIN_SPEECH)
    vad_task: Optional[asyncio.Task] = None

    try:
        while True:
            # Receive message with timeout for VAD checking
            try:
                data = await asyncio.wait_for(websocket.receive(), timeout=0.5)
            except asyncio.TimeoutError:
                # Check VAD periodically
                current_time = asyncio.get_event_loop().time()
                if await audio_buffer.check_vad(current_time, asyncio.get_event_loop()):
                    # Trigger transcription
                    audio_data = audio_buffer.get_audio()
                    if audio_data:
                        # Transcribe
                        await websocket.send_json({"type": "transcribing", "content": ""})
                        text = await transcribe_audio(audio_data)

                        if text:
                            await websocket.send_json({
                                "type": "text",
                                "content": text
                            })

                            # Send to LLM and get TTS response
                            response = await stream_to_ollama(text, websocket)
                            await websocket.send_json({
                                "type": "done",
                                "content": response
                            })

                        audio_buffer.clear()
                continue

            if "text" in data:
                # Text message (control message)
                try:
                    message = json.loads(data["text"])
                except json.JSONDecodeError:
                    continue

                msg_type = message.get("type")

                if msg_type == "ping":
                    await websocket.send_json({"type": "pong"})
                elif msg_type == "transcribe":
                    # Force transcribe current buffer
                    audio_data = audio_buffer.get_audio()
                    if audio_data:
                        text = await transcribe_audio(audio_data)
                        await websocket.send_json({
                            "type": "text",
                            "content": text
                        })
                        audio_buffer.clear()
                elif msg_type == "stream":
                    # Stream mode: process immediately without waiting for silence
                    audio_data = audio_buffer.get_audio()
                    if audio_data:
                        text = await transcribe_audio(audio_data)
                        if text:
                            await websocket.send_json({
                                "type": "text",
                                "content": text
                            })
                            response = await stream_to_ollama(text, websocket)
                        audio_buffer.clear()

            elif "bytes" in data:
                # Audio data
                audio_chunk = data["bytes"]
                current_time = asyncio.get_event_loop().time()

                # Record speech start if first audio after silence
                if audio_buffer.speech_start_time is None:
                    audio_buffer.speech_start_time = current_time

                audio_buffer.add(audio_chunk, current_time)

    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception as e:
        await websocket.send_json({
            "type": "error",
            "content": str(e)
        })
        manager.disconnect(websocket)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint for voice AI pipeline."""
    await handle_websocket(websocket)


@app.get("/")
async def get_index():
    """Serve a simple test page."""
    return HTMLResponse("""
    <html>
        <head>
            <title>Voice AI Pipeline</title>
        </head>
        <body>
            <h1>Voice AI Pipeline</h1>
            <p>WebSocket server running. Connect to <code>/ws</code></p>
            <p>Protocol:</p>
            <ul>
                <li>Send raw PCM audio (16-bit, 16kHz, mono) as binary</li>
                <li>JSON messages: <code>{"type": "transcribe"}</code>, <code>{"type": "ping"}</code></li>
            </ul>
        </body>
    </html>
    """)


def main():
    """Run the server."""
    print(f"Starting Voice AI Pipeline server on {WS_HOST}:{WS_PORT}")
    uvicorn.run(
        "server:app",
        host=WS_HOST,
        port=WS_PORT,
        reload=False,
    )


if __name__ == "__main__":
    main()