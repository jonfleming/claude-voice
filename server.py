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
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

try:
    import aiohttp
except ImportError:
    aiohttp = None
import numpy as np
try:
    import uvicorn
except ImportError:
    uvicorn = None
try:
    from dotenv import load_dotenv
except ImportError:
    # dotenv is optional; define no-op if unavailable
    def load_dotenv():
        pass
try:
    from faster_whisper import WhisperModel
except ImportError:
    # Whisper STT model is optional
    WhisperModel = None
try:
    from fastapi import FastAPI, WebSocket, WebSocketDisconnect
    from fastapi.responses import HTMLResponse
except ImportError:
    # FastAPI is optional; define dummy app and placeholders for import-time definitions
    class DummyApp:
        def __init__(self, *args, **kwargs):
            pass
        def websocket(self, path):
            def decorator(func):
                return func
            return decorator
        def get(self, path):
            def decorator(func):
                return func
            return decorator
    FastAPI = DummyApp
    WebSocket = None
    WebSocketDisconnect = Exception
    # Dummy HTMLResponse that returns content directly
    HTMLResponse = lambda content: content
try:
    from hindsight_client import Hindsight
except ImportError:
    Hindsight = None
from prompt_classifier import classify_prompt_type

# Optional Python piper package (preferred over CLI if available)
try:
    import piper_tts as piper_pkg
except Exception:
    piper_pkg = None

load_dotenv()


# Configuration
OLLAMA_HOST = os.getenv("OLLAMA_HOST", "http://100.111.132.40:11434")
if not OLLAMA_HOST.startswith("http"):
    OLLAMA_HOST = f"http://{OLLAMA_HOST}"
OLLAMA_HOST = OLLAMA_HOST.rstrip("/")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "llama3.2")
PIPER_MODEL = os.getenv("PIPER_MODEL", "en_US-libritts_r-medium.onnx")
PIPER_MODEL_DIR = os.getenv("PIPER_MODEL_DIR", "")
AUDIO_SAMPLE_RATE = int(os.getenv("AUDIO_SAMPLE_RATE", "16000"))
VAD_THRESHOLD = float(os.getenv("VAD_THRESHOLD", "1.0"))
VAD_MIN_SPEECH = float(os.getenv("VAD_MIN_SPEECH", "0.3"))
VAD_ENERGY_THRESHOLD = float(os.getenv("VAD_ENERGY_THRESHOLD", "0.005"))
WS_HOST = os.getenv("WS_HOST", "0.0.0.0")
WS_PORT = int(os.getenv("WS_PORT", "8080"))
HINDSIGHT_HOST = os.getenv("HINDSIGHT_HOST", "http://100.111.132.40:8888")
HINDSIGHT_BANK = os.getenv("HINDSIGHT_BANK", "amicus-2026")

# Global models (loaded once)
whisper_model: Optional[WhisperModel] = None
piper_process: Optional[asyncio.subprocess.Process] = None
hindsight_client: Optional[Hindsight] = None


def log(message: str):
    """Print message with timestamp."""
    timestamp = time.strftime("%H:%M:%S", time.localtime())
    millis = int((time.time() % 1) * 10)
    print(f"[{timestamp}.{millis}] {message}")


def get_hindsight_client() -> Optional[Hindsight]:
    """Get or create Hindsight client."""
    global hindsight_client
    if hindsight_client is None:
        try:
            hindsight_client = Hindsight(base_url=HINDSIGHT_HOST)
            log("[Hindsight] Client initialized")
        except Exception as e:
            log(f"[Hindsight] Client creation failed: {e}")
            return None
    return hindsight_client


def retain_memory(content: str, context: str = "", tags: list = None) -> bool:
    """Store a memory in Hindsight."""
    if tags is None:
        tags = ["conversation"]
    client = get_hindsight_client()
    if client is None:
        return False
    try:
        client.retain(bank_id=HINDSIGHT_BANK, content=content, context=context, tags=tags)
        log(f"[Hindsight] Memory retained")
        return True
    except Exception as e:
        log(f"[Hindsight] Retain failed: {e}")
        return False


def recall_memories(query: str, budget: str = "low") -> list:
    """Recall relevant memories from Hindsight."""
    client = get_hindsight_client()
    if client is None:
        return []
    try:
        result = client.recall(bank_id=HINDSIGHT_BANK, query=query, budget=budget)
        if result and isinstance(result, list):
            memories = []
            for item in result:
                if isinstance(item, dict) and "text" in item:
                    memories.append(item["text"])
                elif isinstance(item, str):
                    memories.append(item)
            return memories
        return []
    except Exception as e:
        log(f"[Hindsight] Recall failed: {e}")
        return []


async def retain_memory_async(content: str, context: str = "", tags: list = None) -> bool:
    """Store a memory in Hindsight (async wrapper)."""
    loop = asyncio.get_running_loop()
    return await loop.run_in_executor(None, retain_memory, content, context, tags)


async def recall_memories_async(query: str, budget: str = "low") -> list:
    """Recall relevant memories from Hindsight (async wrapper)."""
    loop = asyncio.get_running_loop()
    return await loop.run_in_executor(None, recall_memories, query, budget)


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


async def safe_send_json(websocket: WebSocket, data: dict) -> bool:
    """Safely send JSON, returns False if connection is closed."""
    try:
        await websocket.send_json(data)
        return True
    except Exception:
        return False


async def safe_send_bytes(websocket: WebSocket, data: bytes) -> bool:
    """Safely send bytes, returns False if connection is closed."""
    try:
        await websocket.send_bytes(data)
        return True
    except Exception:
        return False


manager = ConnectionManager()


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Pre-load models on startup."""
    # Load Whisper in the background
    asyncio.create_task(load_whisper_model())
    yield


app = FastAPI(lifespan=lifespan)


async def load_whisper_model():
    """Load Whisper model for STT."""
    global whisper_model
    if whisper_model is None:
        # Use small model for speed, can change to medium/large for quality
        print("Loading Whisper model...")
        whisper_model = WhisperModel("medium.en", device="cpu", compute_type="int8")
        print("Whisper model loaded")
    return whisper_model


def get_rms(audio_data: bytes) -> float:
    """Calculate RMS energy of audio data."""
    if not audio_data:
        return 0.0
    audio_np = np.frombuffer(audio_data, dtype=np.int16).astype(np.float32) / 32768.0
    return np.sqrt(np.mean(audio_np ** 2)) if len(audio_np) > 0 else 0.0


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


async def stream_to_ollama(messages: list[dict], websocket: WebSocket, tts_queue: Optional[asyncio.Queue] = None) -> str:
    """Send message history to Ollama and stream the response with TTS."""
    global piper_process

    log(f"[Ollama] Sending {len(messages)} messages in history")
    log(f"[Ollama] Last user message: {messages[-1]['content'] if messages else 'N/A'}")
    url = f"{OLLAMA_HOST}/api/chat"
    payload = {
        "model": OLLAMA_MODEL,
        "messages": messages,
        "stream": True
    }

    response_text = ""
    pending_text = ""
    session = None

    try:
        session = aiohttp.ClientSession()
        await session.__aenter__()
        log(f"[Ollama] Posting {payload} \nto {url}...")
        async with session.post(url, json=payload) as resp:
                if resp.status != 200:
                    error_text = await resp.text()
                    log(f"[Ollama] Error: {resp.status} - {error_text}")
                    raise Exception(f"Ollama error: {error_text}")

                log(f"[Ollama] Connection successful, streaming chat response...")

                async for line in resp.content:
                    if line:
                        try:
                            data = json.loads(line)
                            if "message" in data and "content" in data["message"]:
                                token = data["message"]["content"]
                                response_text += token
                                pending_text += token

                                # Send token to client
                                if not await safe_send_json(websocket, {"type": "response", "content": token}):
                                    log("[WS] Connection closed during streaming")
                                    return response_text

                                # Generate TTS on sentence boundaries or if too long
                                if (len(pending_text) >= 20 and any(c in pending_text for c in ".!?\n")) or len(pending_text) >= 120:
                                    # Try to split at the last punctuation to keep sentence integrity
                                    last_punct = -1
                                    for i, char in enumerate(reversed(pending_text)):
                                        if char in ".!?\n":
                                            last_punct = len(pending_text) - i
                                            break

                                    if last_punct != -1:
                                        text_segment = pending_text[:last_punct]
                                        pending_text = pending_text[last_punct:]
                                    else:
                                        text_segment = pending_text
                                        pending_text = ""

                                    if text_segment.strip():
                                        log(f"[TTS] Enqueuing audio generation for: {text_segment.strip()}")
                                        # Enqueue TTS so a single worker synthesizes/sends in FIFO order
                                        if tts_queue is not None:
                                            await tts_queue.put(text_segment)
                                        else:
                                            # Fallback to previous behavior (background task) if no queue provided
                                            asyncio.create_task(_generate_and_send_tts(text_segment, websocket))


                        except json.JSONDecodeError:
                            log(f"[Ollama] Non-JSON line: {line}")
                            continue

                # Generate final TTS for remaining text
                log(f"[Ollama] Stream ended, generating final TTS for remaining text...")
                if pending_text.strip():
                    log(f"[TTS] Enqueuing background final audio for: {pending_text.strip()}")
                    if tts_queue is not None:
                        await tts_queue.put(pending_text)
                    else:
                        asyncio.create_task(_generate_and_send_tts(pending_text, websocket))

    except Exception as e:
        log(f"[Ollama] Exception: {e}")
        await safe_send_json(websocket, {
            "type": "error",
            "content": f"Ollama connection error: {str(e)}"
        })
        raise
    finally:
        if session:
            await session.close()

    log(f"[Ollama] Stream complete ({len(response_text)} chars)")
    return response_text


async def text_to_speech(text: str) -> Optional[bytes]:
    """Convert text to speech using Piper."""
    if not text.strip():
        return None

    model_path = PIPER_MODEL
    if PIPER_MODEL_DIR:
        model_path = str(Path(PIPER_MODEL_DIR) / PIPER_MODEL)
    # First attempt: use the `piper-tts` Python package if available.
    if piper_pkg is not None:
        try:
            # Try several common function names that piper packages might expose.
            candidate_names = ("synthesize", "synthesize_text", "generate", "generate_tts", "tts", "speak")
            for name in candidate_names:
                if hasattr(piper_pkg, name):
                    func = getattr(piper_pkg, name)
                    try:
                        # Prefer keyword args where supported
                        result = func(text, model=str(model_path), sample_rate=AUDIO_SAMPLE_RATE, length_scale=0.75)
                    except TypeError:
                        # Fallback to positional args
                        try:
                            result = func(text, str(model_path))
                        except Exception:
                            result = func(text)

                    # If we get raw bytes, return as-is
                    if isinstance(result, (bytes, bytearray)):
                        log(f"[TTS] Generated {len(result)} bytes via piper_pkg.{name}")
                        return bytes(result)

                    # If we get a numpy array or list, convert to WAV bytes
                    if isinstance(result, np.ndarray) or isinstance(result, list):
                        arr = np.asarray(result)
                        # If float32/64, assume -1..1 range
                        if np.issubdtype(arr.dtype, np.floating):
                            pcm = (arr * 32767.0).astype(np.int16)
                        elif np.issubdtype(arr.dtype, np.integer):
                            pcm = arr.astype(np.int16)
                        else:
                            pcm = arr.astype(np.int16)

                        import io, wave
                        buf = io.BytesIO()
                        with wave.open(buf, "wb") as wf:
                            wf.setnchannels(1)
                            wf.setsampwidth(2)
                            wf.setframerate(AUDIO_SAMPLE_RATE)
                            wf.writeframes(pcm.tobytes())
                        data = buf.getvalue()
                        log(f"[TTS] Generated {len(data)} WAV bytes via piper_pkg.{name}")
                        return data

            # If no known function produced usable output, log and fall back
            log("[TTS] piper_pkg available but produced no usable audio, falling back to CLI")
        except Exception as e:
            log(f"[TTS] piper_pkg invocation failed: {e}; falling back to CLI")

    # Fallback: call the `piper` CLI as before
    try:
        process = await asyncio.create_subprocess_exec(
            "piper",
            "--model", model_path,
            "--length_scale", "0.75",
            "--output_file", "-",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        stdout, stderr = await process.communicate(input=text.encode() + b"\n")

        if process.returncode != 0:
            print(f"TTS error (code {process.returncode}): {stderr.decode()}")
            return None

        if stdout:
            log(f"[TTS] Generated {len(stdout)} bytes of audio (CLI)")
            return stdout

        log("[TTS] No audio data generated (CLI)")
        return None

    except FileNotFoundError:
        print("Piper not found. Make sure piper is in PATH or install the `piper-tts` package.")
        return None
    except Exception as e:
        print(f"TTS error: {e}")
        return None


async def _generate_and_send_tts(text: str, websocket: WebSocket):
    """Background helper: synthesize TTS and send to websocket without blocking the LLM stream."""
    try:
        audio = await text_to_speech(text)
        if not audio:
            return

        # Send raw binary first (preferred by embedded clients)
        if not await safe_send_bytes(websocket, audio):
            log("[WS] Connection closed during background TTS send")
            return

        # Also send base64 JSON for web clients
        audio_b64 = base64.b64encode(audio).decode()
        await safe_send_json(websocket, {"type": "audio", "data": audio_b64})
        log(f"[TTS] Background send complete ({len(audio)} bytes)")
    except Exception as e:
        log(f"[TTS] Background exception: {e}")


async def _tts_worker(tts_queue: asyncio.Queue, websocket: WebSocket):
    """Worker that processes TTS requests from a FIFO queue, synthesizes audio,
    and sends them to the websocket in order. Use `None` as sentinel to stop."""
    try:
        while True:
            text = await tts_queue.get()
            if text is None:
                tts_queue.task_done()
                break
            try:
                await _generate_and_send_tts(text, websocket)
            except Exception as e:
                log(f"[TTS-Worker] Exception for segment: {e}")
            finally:
                tts_queue.task_done()
    except asyncio.CancelledError:
        pass


class AudioBuffer:
    """Audio buffer with content-aware VAD detection."""

    def __init__(self, vad_threshold: float = 1.5, min_speech: float = 0.3, energy_threshold: float = 0.005, sample_rate: int = 16000):
        self.buffer: list[bytes] = []
        self.vad_threshold = vad_threshold
        self.min_speech = min_speech
        self.energy_threshold = energy_threshold
        self.sample_rate = sample_rate
        self.last_audio_time: Optional[float] = None
        self.speech_start_time: Optional[float] = None
        self.silent_duration = 0.0
        # Internal buffer for VAD windowing (ensures stable RMS on small chunks)
        self.vad_window_buffer = b""
        self.min_vad_window_bytes = int(self.sample_rate * 0.1 * 2) # 100ms

    def add(self, chunk: bytes, current_time: float):
        """Add audio chunk to buffer and update VAD state."""
        self.buffer.append(chunk)
        self.vad_window_buffer += chunk
        self.last_audio_time = current_time

        # Only process VAD when we have a full window
        while len(self.vad_window_buffer) >= self.min_vad_window_bytes:
            window = self.vad_window_buffer[:self.min_vad_window_bytes]
            self.vad_window_buffer = self.vad_window_buffer[self.min_vad_window_bytes:]
            
            rms = get_rms(window)
            duration = len(window) / (2 * self.sample_rate)
            self._update_vad(rms, duration, current_time)

    def _update_vad(self, rms: float, duration: float, current_time: float):
        """Internal VAD state update."""
        if rms < self.energy_threshold:
            # accumulate silence
            self.silent_duration += duration
            # log(f"[VAD-Window] RMS: {rms:.5f} (silence), +{duration:.3f}s -> silent_duration={self.silent_duration:.3f}s")
        else:
            # Speech detected
            if self.speech_start_time is None:
                log(f"[VAD] Speech detected (RMS: {rms:.4f})")
                self.speech_start_time = current_time
            else:
                # Update speech presence (useful for long speech segments)
                # log(f"[VAD-Window] RMS: {rms:.5f} (speech), silent_duration reset")
                pass
            self.silent_duration = 0.0

    def add_silence(self, duration: float):
        """Manually add silence duration (used on connection timeouts)."""
        if self.buffer:
            self.silent_duration += duration
            log(f"[VAD] add_silence: +{duration:.3f}s -> silent_duration={self.silent_duration:.3f}s")

    def get_audio(self) -> bytes:
        """Get all buffered audio."""
        return b"".join(self.buffer)

    def clear(self):
        """Clear the buffer."""
        self.buffer = []
        self.vad_window_buffer = b""
        self.speech_start_time = None
        self.silent_duration = 0.0

    def check_vad(self) -> bool:
        """Check if we should trigger transcription (VAD)."""
        if not self.buffer:
            return False

        # If silence has lasted longer than threshold
        if self.silent_duration >= self.vad_threshold:
            # log(f"[VAD] check_vad: silent_duration={self.silent_duration:.3f}s >= vad_threshold={self.vad_threshold:.3f}s; speech_start_time={self.speech_start_time}")
            # Check we have enough speech duration to care
            if self.speech_start_time is not None and self.last_audio_time is not None:
                speech_duration = self.last_audio_time - self.speech_start_time
                if speech_duration >= self.min_speech:
                    log(f"[VAD] Silent threshold reached, speech duration: {speech_duration:.2f}s -> trigger transcription")
                    return True
            
            # If we have reached the silence threshold but never detected speech,
            # or speech was too short, clear the buffer to avoid it growing indefinitely.
            if self.silent_duration > self.vad_threshold * 2:
                if self.speech_start_time is not None:
                    log("[VAD] Silence threshold reached but speech too short, clearing.")
                self.clear()

        return False


async def handle_websocket(websocket: WebSocket):
    """Handle a WebSocket connection for the voice pipeline."""
    log("[WS] Client connected")
    await manager.connect(websocket)
    # Per-connection TTS queue and worker to preserve audio ordering
    tts_queue: asyncio.Queue = asyncio.Queue()
    tts_worker_task = asyncio.create_task(_tts_worker(tts_queue, websocket))

    audio_buffer = AudioBuffer(VAD_THRESHOLD, VAD_MIN_SPEECH, VAD_ENERGY_THRESHOLD, AUDIO_SAMPLE_RATE)
    chat_history: list[dict] = []
    pending_memories: list[str] = []  # Memories from previous turn to include
    is_processing = False
    last_rms_log_time = 0

    async def queue_recall(query: str):
        """Background task to recall memories for next turn."""
        memories = await recall_memories_async(query, budget="low")
        if memories:
            pending_memories.clear()
            pending_memories.extend(memories)
            log(f"[Hindsight] Queued {len(memories)} memories for next turn")

    async def trigger_transcription(force=False):
        """Helper to trigger transcription and LLM response."""
        nonlocal is_processing
        
        if is_processing and not force:
            return

        audio_data = audio_buffer.get_audio()
        if not audio_data:
            return

        # Transcription Gate: Check if the overall energy is high enough to be real speech
        total_rms = get_rms(audio_data)
        if not force and total_rms < audio_buffer.energy_threshold * 1.2:
            log(f"[VAD] Ignoring low-energy segment (RMS: {total_rms:.4f})")
            audio_buffer.clear()
            return

        is_processing = True
        if not force:
            await safe_send_json(websocket, {"type": "stop_recording"})

        audio_buffer.clear()
        log(f"[VAD] Triggering transcription ({len(audio_data)} bytes, RMS: {total_rms:.4f})")
        await safe_send_json(websocket, {"type": "transcribing", "content": ""})

        try:
            text = await transcribe_audio(audio_data)

            # Hallucination Filter: Whisper often hallucinations common phrases on noise
            hallucinations = ["thank", "thanks for watching", "bye", "subscrib"]
            one_word_hallucinations = ["thanks", "bye", "subscribe", "you"]
            if text in one_word_hallucinations:
                log(f"[STT] Filtered one-word hallucination: {text}")
                text = ""
                
            if text and any(h in text.lower() for h in hallucinations) and total_rms < audio_buffer.energy_threshold * 2.0:
                log(f"[STT] Filtered hallucination: {text}")
                text = ""

            if text:
                log(f"[STT] {text}")
                await safe_send_json(websocket, {"type": "text", "content": text})

                # Classification: STATEMENT, QUESTION, or QUERY
                classification = classify_prompt_type(text)
                log(f"[Classifier] Classified as {classification}")

                # If statement, store in memory but still respond
                if classification == "STATEMENT":
                    retained = await retain_memory_async(
                        text, context="voice conversation", tags=["conversation"]
                    )
                    if retained:
                        log("[Hindsight] Statement memory retained")
                    else:
                        log("[Hindsight] Statement memory failed to retain")

                # Prepare messages for LLM
                if classification == "QUERY":
                    # Retrieve relevant memories
                    memories = await recall_memories_async(text, budget="low")
                    if memories:
                        log(f"[Hindsight] Retrieved {len(memories)} memories for query")
                        context_prompt = "\n".join([f"- {m}" for m in memories])
                        system_msg = {
                            "role": "system",
                            "content": (
                                f"Relevant past conversations:\n{context_prompt}\n\n"
                                "You are a helpful voice assistant."
                            )
                        }
                        messages = [system_msg, {"role": "user", "content": text}]
                        log(f"[Hindsight] Included memories in context for LLM:\n{context_prompt}")
                    else:
                        log("[Hindsight] No memories found for query; proceeding without context")
                        messages = [
                            {"role": "system", "content": "You are a helpful voice assistant."},
                            {"role": "user", "content": text}
                        ]
                else:
                    # General question or statement
                    messages = [
                        {"role": "system", "content": "You are a helpful voice assistant."},
                        {"role": "user", "content": text}
                    ]

                # Send to Ollama and stream response
                response = await stream_to_ollama(messages, websocket, tts_queue)
                await safe_send_json(websocket, {"type": "done", "content": response})
                # Ensure all TTS segments are sent
                await tts_queue.join()
                await safe_send_json(websocket, {"type": "audio_done"})
            else:
                log("[STT] No meaningful speech detected")
                await safe_send_json(websocket, {"type": "done", "content": ""})
                await safe_send_json(websocket, {"type": "audio_done"})
        finally:
            is_processing = False

    try:
        while True:
            # Receive message with timeout for VAD checking
            try:
                data = await asyncio.wait_for(websocket.receive(), timeout=0.5)
            except asyncio.TimeoutError:
                # Check VAD on timeout (handles case where client stops sending)
                if not is_processing:
                    audio_buffer.add_silence(0.5)
                    if audio_buffer.check_vad():
                        await trigger_transcription()
                continue

            if data.get("type") == "websocket.disconnect":
                raise WebSocketDisconnect(code=data.get("code", 1000))

            if "text" in data:
                # Text message (control message)
                try:
                    message = json.loads(data["text"])
                except json.JSONDecodeError:
                    continue

                msg_type = message.get("type")

                if msg_type == "ping":
                    await safe_send_json(websocket, {"type": "pong"})
                elif msg_type == "transcribe":
                    await trigger_transcription(force=True)
                elif msg_type == "stream":
                    # Force process current buffer
                    await trigger_transcription(force=True)
                elif msg_type == "config":
                    # Dynamically update VAD energy threshold for this connection
                    if "energy_threshold" in message:
                        new_threshold = float(message["energy_threshold"])
                        if 0 < new_threshold < 1.0:
                            audio_buffer.energy_threshold = new_threshold
                            log(f"[Config] Updated VAD energy threshold to {new_threshold:.5f}")
                            await safe_send_json(websocket, {
                                "type": "config_ack",
                                "energy_threshold": new_threshold
                            })
                        else:
                            log(f"[Config] Invalid energy_threshold: {new_threshold}")
                    else:
                        log("[Config] Received config message without energy_threshold")

            elif "bytes" in data:
                # Audio data
                if is_processing:
                    continue # Ignore audio while processing

                audio_chunk = data["bytes"]
                current_time = asyncio.get_event_loop().time()
                audio_buffer.add(audio_chunk, current_time)

                # Periodic RMS logging for calibration (every 2 seconds)
                if current_time - last_rms_log_time > 2.0:
                    rms = get_rms(audio_chunk)
                    log(f"[Audio] Current RMS: {rms:.5f} (Threshold: {audio_buffer.energy_threshold:.5f})")
                    last_rms_log_time = current_time

                # Check VAD after each chunk (handles continuous streaming)
                if audio_buffer.check_vad():
                    await trigger_transcription()

    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception as e:
        log(f"[WS] Error: {e}")
        await safe_send_json(websocket, {
            "type": "error",
            "content": str(e)
        })
        manager.disconnect(websocket)
    finally:
        # Shutdown TTS worker gracefully
        try:
            # enqueue sentinel and wait for worker to finish
            await tts_queue.put(None)
            await asyncio.wait_for(tts_worker_task, timeout=5.0)
        except Exception:
            try:
                tts_worker_task.cancel()
            except Exception:
                pass


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint for voice AI pipeline."""
    try:
        await handle_websocket(websocket)
    except Exception as e:
        log(f"[WS] Error in handler: {e}")
        try:
            await websocket.close(1011, str(e))
        except:
            pass


@app.get("/")
async def get_index():
    """Serve a test page for uploading WAV files."""
    return HTMLResponse("""
    <!DOCTYPE html>
    <html>
        <head>
            <title>Voice AI Pipeline - Test</title>
            <style>
                body { font-family: system-ui, sans-serif; max-width: 800px; margin: 40px auto; padding: 20px; }
                button { padding: 10px 20px; font-size: 16px; cursor: pointer; }
                input[type="file"] { padding: 10px; }
                #log { background: #1a1a2e; color: #0f0; padding: 15px; border-radius: 8px; height: 300px; overflow-y: auto; font-family: monospace; white-space: pre-wrap; }
                .status { padding: 5px 10px; border-radius: 4px; display: inline-block; margin: 5px 0; }
                .connected { background: #22c55e; color: white; }
                .disconnected { background: #ef4444; color: white; }
                .error { background: #f97316; color: white; }
                .debug-only { display: none; }
                #visualizer { background: #000; border-radius: 8px; width: 100%; height: 100px; margin: 10px 0; display: none; }
            </style>
        </head>
        <body>
            <h1>Voice AI Pipeline - Test</h1>
            <p><span id="status" class="status disconnected">Disconnected</span></p>

            <div style="margin-bottom: 20px;">
                <label><input type="checkbox" id="debugToggle"> Show Debug Options (File Upload)</label>
                <div class="debug-only" style="margin-top: 10px;">
                    <label for="vadEnergyThreshold">VAD Energy Threshold:</label>
                    <input type="number" id="vadEnergyThreshold" min="0.0001" max="1.0" step="0.0001" value="0.005" style="width: 100px; margin-left: 8px;">
                    <button id="applyVadBtn" type="button">Apply</button>
                </div>
            </div>

            <h2>1. Input Source</h2>
            <div class="debug-only">
                <label><input type="radio" name="input" value="file"> WAV File</label>
            </div>
            <label><input type="radio" name="input" value="mic" checked> Microphone</label>

            <div id="fileInput" class="debug-only" style="display:none">
                <input type="file" id="wavFile" accept=".wav">
            </div>

            <div id="micInput">
                <canvas id="visualizer"></canvas>
                <button id="micStartBtn">Start Microphone</button>
                <button id="micStopBtn" disabled>Stop Microphone</button>
                <span id="micStatus"></span>
            </div>

            <h2>2. Connect & Stream</h2>
            <button id="connectBtn">Connect</button>
            <button id="sendBtn" class="debug-only" disabled>Send to Server</button>
            <button id="transcribeBtn" disabled>Force Transcribe</button>

            <h2>3. Response Audio</h2>
            <audio id="audioPlayer" controls></audio>

            <h2>Log</h2>
            <div id="log"></div>

            <script>
                let ws = null;
                let audioContext = null;
                let micStream = null;
                let processor = null;
                let analyser = null;
                let animationId = null;
                
                let isServerProcessing = false;
                let isPlaying = false;
                let awaitingAudioDone = false;
                let audioQueue = [];

                const debugToggle = document.getElementById('debugToggle');
                const debugElements = document.querySelectorAll('.debug-only');

                debugToggle.onchange = () => {
                    debugElements.forEach(el => {
                        el.style.display = debugToggle.checked ? (el.tagName === 'DIV' ? 'block' : 'inline-block') : 'none';
                    });
                };

                // Input source toggle
                document.querySelectorAll('input[name="input"]').forEach(r => {
                    r.onchange = () => {
                        document.getElementById('fileInput').style.display = (r.value === 'file' && debugToggle.checked) ? 'block' : 'none';
                        document.getElementById('micInput').style.display = r.value === 'mic' ? 'block' : 'none';
                    };
                });

                const log = document.getElementById('log');
                const status = document.getElementById('status');
                const connectBtn = document.getElementById('connectBtn');
                const sendBtn = document.getElementById('sendBtn');
                const transcribeBtn = document.getElementById('transcribeBtn');
                const micStartBtn = document.getElementById('micStartBtn');
                const micStopBtn = document.getElementById('micStopBtn');
                const micStatus = document.getElementById('micStatus');
                const vadEnergyThresholdInput = document.getElementById('vadEnergyThreshold');
                const applyVadBtn = document.getElementById('applyVadBtn');
                const audioPlayer = document.getElementById('audioPlayer');
                const visualizer = document.getElementById('visualizer');
                const canvasCtx = visualizer.getContext('2d');

                function sendVadConfig() {
                    const value = parseFloat(vadEnergyThresholdInput.value);
                    if (!Number.isFinite(value) || value <= 0 || value >= 1.0) {
                        addLog('[Config] Invalid VAD energy threshold. Use a value between 0 and 1.0');
                        return;
                    }

                    if (!ws || ws.readyState !== WebSocket.OPEN) {
                        addLog('[Config] Connect first to apply VAD energy threshold');
                        return;
                    }

                    ws.send(JSON.stringify({ type: 'config', energy_threshold: value }));
                    addLog('[Config] Applied VAD energy threshold: ' + value.toFixed(4));
                }

                function addLog(msg) {
                    log.textContent += msg + '\\n';
                    log.scrollTop = log.scrollHeight;
                }

                function playNextAudio() {
                    console.log('Checking audio queue, length:', audioQueue.length);
                    if (audioQueue.length === 0) {
                        isPlaying = false;
                        return;
                    }

                    log.textContent += '[Audio] Playing response, queue length: ' + audioQueue.length + '\\n';
                    isPlaying = true;
                    const blobUrl = audioQueue.shift();
                    audioPlayer.src = blobUrl;
                    audioPlayer.play().catch(e => {
                        console.error('Playback error:', e);
                        playNextAudio();
                    });
                }

                audioPlayer.onended = () => {
                    console.log('Audio ended, playing next if available');
                    playNextAudio();
                };

                connectBtn.onclick = () => {
                    if (ws) {
                        ws.close();
                        return;
                    }

                    ws = new WebSocket('ws://' + location.host + '/ws');
                    ws.binaryType = 'arraybuffer';

                    ws.onopen = () => {
                        status.textContent = 'Connected';
                        status.className = 'status connected';
                        connectBtn.textContent = 'Disconnect';
                        sendBtn.disabled = false;
                        transcribeBtn.disabled = false;
                        addLog('[WS] Connected');
                        sendVadConfig();
                    };

                    ws.onclose = () => {
                        status.textContent = 'Disconnected';
                        status.className = 'status disconnected';
                        connectBtn.textContent = 'Connect';
                        sendBtn.disabled = true;
                        transcribeBtn.disabled = true;
                        ws = null;
                        addLog('[WS] Disconnected');
                    };

                    ws.onerror = (e) => {
                        addLog('[WS] Error: ' + e);
                        status.textContent = 'Error';
                        status.className = 'status error';
                    };

                    ws.onmessage = (event) => {
                        if (event.data instanceof ArrayBuffer) {
                            console.log('Received binary audio data, size:', event.data.byteLength);
                            const blob = new Blob([event.data], { type: 'audio/wav' });
                            const url = URL.createObjectURL(blob);
                            audioQueue.push(url);
                            if (!isPlaying) {
                                playNextAudio();
                            }
                        } else {
                            try {
                                const msg = JSON.parse(event.data);
                                console.log('Received message:', msg.type);
                                
                                if (msg.type === 'transcribing') {
                                    isServerProcessing = true;
                                    awaitingAudioDone = false;
                                    addLog('[WS] Transcribing...');
                                } else if (msg.type === 'done') {
                                    isServerProcessing = false;
                                    awaitingAudioDone = true;
                                    addLog('[WS] Response text complete');
                                } else if (msg.type === 'audio_done') {
                                    awaitingAudioDone = false;
                                    addLog('[WS] Response audio complete');
                                } else if (msg.type === 'text') {
                                    addLog('[STT] ' + msg.content);
                                } else if (msg.type === 'error') {
                                    addLog('[Error] ' + msg.content);
                                    isServerProcessing = false;
                                    awaitingAudioDone = false;
                                } else if (msg.type === 'audio' && msg.data) {
                                    // Skip if we already got binary for this segment
                                    // (Simplification: if we send both, we'll play twice if not careful.
                                    // However, the ESP32 prefers binary, web can handle both. 
                                    // To avoid double playback, we only use binary if available.)
                                    // For now, let's just log and skip JSON audio if it's there
                                    console.log('Received JSON audio (skipped in favor of binary)');
                                }
                            } catch (e) {
                                addLog('[WS] ' + event.data);
                            }
                        }
                    };
                };

                sendBtn.onclick = () => {
                    const fileInput = document.getElementById('wavFile');
                    const file = fileInput.files[0];
                    if (!file) {
                        addLog('Please select a WAV file first');
                        return;
                    }
                    if (!ws || ws.readyState !== WebSocket.OPEN) {
                        addLog('Please connect first');
                        return;
                    }

                    addLog('[File] Reading: ' + file.name);

                    const reader = new FileReader();
                    reader.onload = (e) => {
                        const arrayBuffer = e.target.result;
                        const uint8Array = new Uint8Array(arrayBuffer);

                        // Skip WAV header (44 bytes) to get raw PCM data
                        const pcmData = uint8Array.slice(44);

                        addLog('[WS] Sending ' + pcmData.length + ' bytes of audio');
                        ws.send(pcmData);
                    };
                    reader.readAsArrayBuffer(file);
                };

                transcribeBtn.onclick = () => {
                    if (!ws || ws.readyState !== WebSocket.OPEN) {
                        addLog('Please connect first');
                        return;
                    }
                    addLog('[WS] Sending force transcribe request');
                    ws.send(JSON.stringify({type: "transcribe"}));
                };

                applyVadBtn.onclick = () => {
                    sendVadConfig();
                };

                vadEnergyThresholdInput.onkeydown = (event) => {
                    if (event.key === 'Enter') {
                        sendVadConfig();
                    }
                };

                // Microphone handling
                micStartBtn.onclick = async () => {
                    if (!ws || ws.readyState !== WebSocket.OPEN) {
                        addLog('Please connect first');
                        return;
                    }

                    try {
                        micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
                        audioContext = new AudioContext({ sampleRate: 16000 });
                        const source = audioContext.createMediaStreamSource(micStream);
                        
                        // Setup Analyser for visualization
                        analyser = audioContext.createAnalyser();
                        analyser.fftSize = 2048;
                        const bufferLength = analyser.frequencyBinCount;
                        const dataArray = new Uint8Array(bufferLength);
                        
                        visualizer.style.display = 'block';
                        
                        function draw() {
                            animationId = requestAnimationFrame(draw);
                            analyser.getByteTimeDomainData(dataArray);

                            canvasCtx.fillStyle = 'rgb(0, 0, 0)';
                            canvasCtx.fillRect(0, 0, visualizer.width, visualizer.height);
                            canvasCtx.lineWidth = 2;
                            canvasCtx.strokeStyle = isServerProcessing || isPlaying || awaitingAudioDone ? 'rgb(100, 100, 100)' : 'rgb(0, 255, 0)';
                            canvasCtx.beginPath();

                            let sliceWidth = visualizer.width * 1.0 / bufferLength;
                            let x = 0;

                            for (let i = 0; i < bufferLength; i++) {
                                let v = dataArray[i] / 128.0;
                                let y = v * visualizer.height / 2;

                                if (i === 0) {
                                    canvasCtx.moveTo(x, y);
                                } else {
                                    canvasCtx.lineTo(x, y);
                                }
                                x += sliceWidth;
                            }

                            canvasCtx.lineTo(visualizer.width, visualizer.height / 2);
                            canvasCtx.stroke();
                        }
                        draw();

                        processor = audioContext.createScriptProcessor(4096, 1, 1);

                        processor.onaudioprocess = (e) => {
                            // Block sending if server is processing or AI is speaking
                            if (isServerProcessing || isPlaying || awaitingAudioDone) {
                                return;
                            }

                            const inputData = e.inputBuffer.getChannelData(0);
                            // Convert to 16-bit PCM
                            const pcmData = new Int16Array(inputData.length);
                            for (let i = 0; i < inputData.length; i++) {
                                pcmData[i] = Math.max(-1, Math.min(1, inputData[i])) * 32767;
                            }

                            // Stream to server
                            if (ws && ws.readyState === WebSocket.OPEN) {
                                ws.send(pcmData.buffer);
                            }
                        };

                        source.connect(analyser);
                        analyser.connect(processor);
                        processor.connect(audioContext.destination);

                        micStartBtn.disabled = true;
                        micStopBtn.disabled = false;
                        micStatus.textContent = ' Recording...';
                        addLog('[Mic] Started streaming');

                    } catch (err) {
                        addLog('[Mic] Error: ' + err);
                    }
                };

                micStopBtn.onclick = () => {
                    if (processor) {
                        processor.disconnect();
                        processor = null;
                    }
                    if (micStream) {
                        micStream.getTracks().forEach(t => t.stop());
                        micStream = null;
                    }
                    if (audioContext) {
                        audioContext.close();
                        audioContext = null;
                    }
                    micStartBtn.disabled = false;
                    micStopBtn.disabled = true;
                    micStatus.textContent = '';
                    addLog('[Mic] Stopped');
                };
            </script>
        </body>
    </html>
    """)


def main():
    """Run the server."""
    print(f"Starting Voice AI Pipeline server on {WS_HOST}:{WS_PORT}")
    # Disable automatic WebSocket keepalive pings to avoid ping/pong assertion errors
    uvicorn.run(
        "server:app",
        host=WS_HOST,
        port=WS_PORT,
        reload=False,
        ws_ping_interval=None,
        ws_ping_timeout=None,
    )


if __name__ == "__main__":
    main()