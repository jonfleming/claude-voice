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
PIPER_MODEL = os.getenv("PIPER_MODEL", "en_US-amy-medium.onnx")
PIPER_MODEL_DIR = os.getenv("PIPER_MODEL_DIR", "")
AUDIO_SAMPLE_RATE = int(os.getenv("AUDIO_SAMPLE_RATE", "16000"))
VAD_THRESHOLD = float(os.getenv("VAD_THRESHOLD", "1.5"))
VAD_MIN_SPEECH = float(os.getenv("VAD_MIN_SPEECH", "0.3"))
VAD_ENERGY_THRESHOLD = float(os.getenv("VAD_ENERGY_THRESHOLD", "0.01"))
WS_HOST = os.getenv("WS_HOST", "0.0.0.0")
WS_PORT = int(os.getenv("WS_PORT", "8080"))

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


async def stream_to_ollama(messages: list[dict], websocket: WebSocket) -> str:
    """Send message history to Ollama and stream the response with TTS."""
    global piper_process

    print(f"[Ollama] Sending {len(messages)} messages in history")
    url = f"{OLLAMA_HOST}/api/chat"
    payload = {
        "model": OLLAMA_MODEL,
        "messages": messages,
        "stream": True
    }

    response_text = ""
    pending_text = ""

    try:
        async with aiohttp.ClientSession() as session:
            async with session.post(url, json=payload) as resp:
                if resp.status != 200:
                    error_text = await resp.text()
                    print(f"[Ollama] Error: {resp.status} - {error_text}")
                    raise Exception(f"Ollama error: {error_text}")

                print(f"[Ollama] Connection successful, streaming chat response...")

                async for line in resp.content:
                    if line:
                        try:
                            data = json.loads(line)
                            if "message" in data and "content" in data["message"]:
                                token = data["message"]["content"]
                                response_text += token
                                pending_text += token

                                # Send token to client
                                await websocket.send_json({
                                    "type": "response",
                                    "content": token
                                })

                                # Generate TTS on sentence boundaries or if too long
                                if (len(pending_text) >= 30 and any(c in pending_text for c in ".!?\n")) or len(pending_text) >= 120:
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
                                        print(f"[TTS] Generating audio for: {text_segment.strip()}")
                                        audio = await text_to_speech(text_segment)
                                        if audio:
                                            audio_b64 = base64.b64encode(audio).decode()
                                            await websocket.send_json({
                                                "type": "audio",
                                                "data": audio_b64
                                            })


                        except json.JSONDecodeError:
                            continue

                # Generate final TTS for remaining text
                if pending_text.strip():
                    print(f"[TTS] Generating final audio for: {pending_text.strip()}")
                    audio = await text_to_speech(pending_text)
                    if audio:
                        audio_b64 = base64.b64encode(audio).decode()
                        await websocket.send_json({
                            "type": "audio",
                            "data": audio_b64
                        })

    except Exception as e:
        print(f"[Ollama] Exception: {e}")
        await websocket.send_json({
            "type": "error",
            "content": f"Ollama connection error: {str(e)}"
        })
        raise

    print(f"[Ollama] Stream complete ({len(response_text)} chars)")
    return response_text


async def text_to_speech(text: str) -> Optional[bytes]:
    """Convert text to speech using Piper."""
    if not text.strip():
        return None

    model_path = PIPER_MODEL
    if PIPER_MODEL_DIR:
        model_path = str(Path(PIPER_MODEL_DIR) / PIPER_MODEL)

    try:
        # Create a fresh process for each segment to ensure stdout is flushed and EOF is reached
        process = await asyncio.create_subprocess_exec(
            "piper",
            "--model", model_path,
            "--output_file", "-",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        # Send text and close stdin to signal EOF to Piper
        stdout, stderr = await process.communicate(input=text.encode() + b"\n")

        if process.returncode != 0:
            print(f"TTS error (code {process.returncode}): {stderr.decode()}")
            return None

        if stdout:
            print(f"[TTS] Generated {len(stdout)} bytes of audio")
            return stdout
        
        print("[TTS] No audio data generated")
        return None

    except FileNotFoundError:
        print("Piper not found. Make sure piper is in PATH.")
        return None
    except Exception as e:
        print(f"TTS error: {e}")
        return None


class AudioBuffer:
    """Audio buffer with content-aware VAD detection."""

    def __init__(self, vad_threshold: float = 1.5, min_speech: float = 0.3, energy_threshold: float = 0.01, sample_rate: int = 16000):
        self.buffer: list[bytes] = []
        self.vad_threshold = vad_threshold
        self.min_speech = min_speech
        self.energy_threshold = energy_threshold
        self.sample_rate = sample_rate
        self.last_audio_time: Optional[float] = None
        self.speech_start_time: Optional[float] = None
        self.silent_duration: float = 0.0

    def add(self, chunk: bytes, current_time: float):
        """Add audio chunk to buffer and update VAD state."""
        self.buffer.append(chunk)
        self.last_audio_time = current_time

        # Process in windows of 100ms for more accurate VAD
        window_size_bytes = int(self.sample_rate * 0.1 * 2) # 100ms window (16-bit mono)
        
        if len(chunk) <= window_size_bytes:
            rms = get_rms(chunk)
            duration = len(chunk) / (2 * self.sample_rate)
            self._update_vad(rms, duration, current_time)
        else:
            # Split into windows
            for i in range(0, len(chunk), window_size_bytes):
                window = chunk[i:i+window_size_bytes]
                if not window: continue
                rms = get_rms(window)
                duration = len(window) / (2 * self.sample_rate)
                self._update_vad(rms, duration, current_time)

    def _update_vad(self, rms: float, duration: float, current_time: float):
        """Internal VAD state update."""
        if rms < self.energy_threshold:
            self.silent_duration += duration
        else:
            # Speech detected
            if self.speech_start_time is None:
                self.speech_start_time = current_time
            self.silent_duration = 0.0

    def add_silence(self, duration: float):
        """Manually add silence duration (used on connection timeouts)."""
        if self.buffer:
            self.silent_duration += duration

    def get_audio(self) -> bytes:
        """Get all buffered audio."""
        return b"".join(self.buffer)

    def clear(self):
        """Clear the buffer."""
        self.buffer = []
        self.speech_start_time = None
        self.silent_duration = 0.0

    def check_vad(self) -> bool:
        """Check if we should trigger transcription (VAD)."""
        if not self.buffer:
            return False

        # If silence has lasted longer than threshold
        if self.silent_duration >= self.vad_threshold:
            # Check we have enough speech duration to care
            if self.speech_start_time:
                speech_duration = self.last_audio_time - self.speech_start_time
                if speech_duration >= self.min_speech:
                    return True
            
            # If we have reached the silence threshold but never detected speech,
            # or speech was too short, clear the buffer to avoid it growing indefinitely.
            if self.silent_duration > self.vad_threshold * 2:
                self.clear()

        return False


async def handle_websocket(websocket: WebSocket):
    """Handle a WebSocket connection for the voice pipeline."""
    print("[WS] Client connected")
    await manager.connect(websocket)

    audio_buffer = AudioBuffer(VAD_THRESHOLD, VAD_MIN_SPEECH, VAD_ENERGY_THRESHOLD, AUDIO_SAMPLE_RATE)
    chat_history: list[dict] = []


    async def trigger_transcription():
        """Helper to trigger transcription and LLM response."""
        if not audio_buffer.speech_start_time:
            audio_buffer.clear()
            return

        audio_data = audio_buffer.get_audio()
        audio_buffer.clear() # Clear immediately to avoid double triggers
        
        if not audio_data:
            return

        print(f"[VAD] Triggering transcription ({len(audio_data)} bytes)")
        await websocket.send_json({"type": "transcribing", "content": ""})
        text = await transcribe_audio(audio_data)

        if text:
            print(f"[STT] {text}")
            await websocket.send_json({
                "type": "text",
                "content": text
            })

            # Add to history
            chat_history.append({"role": "user", "content": text})

            # Send to LLM and get TTS response
            response = await stream_to_ollama(chat_history, websocket)
            
            # Save assistant response to history
            chat_history.append({"role": "assistant", "content": response})

            await websocket.send_json({
                "type": "done",
                "content": response
            })
        else:
            print("[STT] No speech detected")
            await websocket.send_json({"type": "done", "content": ""})

    try:
        while True:
            # Receive message with timeout for VAD checking
            try:
                data = await asyncio.wait_for(websocket.receive(), timeout=0.5)
            except asyncio.TimeoutError:
                # Check VAD on timeout (handles case where client stops sending)
                audio_buffer.add_silence(0.5)
                if audio_buffer.check_vad():
                    await trigger_transcription()
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
                    await trigger_transcription()
                elif msg_type == "stream":
                    # Force process current buffer
                    await trigger_transcription()

            elif "bytes" in data:
                # Audio data
                audio_chunk = data["bytes"]
                current_time = asyncio.get_event_loop().time()
                audio_buffer.add(audio_chunk, current_time)

                # Check VAD after each chunk (handles continuous streaming)
                if audio_buffer.check_vad():
                    await trigger_transcription()

    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception as e:
        print(f"[WS] Error: {e}")
        try:
            await websocket.send_json({
                "type": "error",
                "content": str(e)
            })
        except:
            pass
        manager.disconnect(websocket)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint for voice AI pipeline."""
    try:
        await handle_websocket(websocket)
    except Exception as e:
        print(f"[WS] Error in handler: {e}")
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
            </style>
        </head>
        <body>
            <h1>Voice AI Pipeline - Test</h1>
            <p><span id="status" class="status disconnected">Disconnected</span></p>

            <div style="margin-bottom: 20px;">
                <label><input type="checkbox" id="debugToggle"> Show Debug Options (File Upload)</label>
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
                
                let isServerProcessing = false;
                let isPlaying = false;
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
                const audioPlayer = document.getElementById('audioPlayer');

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

                    ws.onopen = () => {
                        status.textContent = 'Connected';
                        status.className = 'status connected';
                        connectBtn.textContent = 'Disconnect';
                        sendBtn.disabled = false;
                        transcribeBtn.disabled = false;
                        addLog('[WS] Connected');
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
                        if (event.data instanceof Blob) {
                            // Raw blob handling (if any)
                        } else {
                            try {
                                const msg = JSON.parse(event.data);
                                console.log('Received message:', msg.type);
                                
                                if (msg.type === 'transcribing') {
                                    isServerProcessing = true;
                                    addLog('[WS] Transcribing...');
                                } else if (msg.type === 'done') {
                                    isServerProcessing = false;
                                    addLog('[WS] Response complete');
                                } else if (msg.type === 'text') {
                                    addLog('[STT] ' + msg.content);
                                } else if (msg.type === 'error') {
                                    addLog('[Error] ' + msg.content);
                                    isServerProcessing = false;
                                } else if (msg.type === 'audio' && msg.data) {
                                    console.log('Received audio data, length:', msg.data.length);
                                    const bytes = atob(msg.data);
                                    const arr = new Uint8Array(bytes.length);
                                    for (let i = 0; i < bytes.length; i++) arr[i] = bytes.charCodeAt(i);
                                    const blob = new Blob([arr], { type: 'audio/wav' });
                                    const url = URL.createObjectURL(blob);
                                    audioQueue.push(url);
                                    if (!isPlaying) {
                                        playNextAudio();
                                    }
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
                        processor = audioContext.createScriptProcessor(4096, 1, 1);

                        processor.onaudioprocess = (e) => {
                            // Block sending if server is processing or AI is speaking
                            if (isServerProcessing || isPlaying) {
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

                        source.connect(processor);
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
    uvicorn.run(
        "server:app",
        host=WS_HOST,
        port=WS_PORT,
        reload=False,
    )


if __name__ == "__main__":
    main()