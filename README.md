# Voice AI Pipeline

A WebSocket-based service that processes audio end-to-end:

1. **STT**: Receives audio, converts speech to text using local Whisper
2. **Classifier**: Routes text as `FACT`, `STATEMENT`, `QUESTION`, or `QUERY`
3. **LLM**: Sends text to local Ollama, receives streamed response
4. **TTS**: Converts response to speech using local Piper TTS
5. **Audio**: Sends synthesized speech back to client

## Requirements

- Python 3.10+
- [uv](https://docs.astral.sh/uv/) (recommended for fast virtual env + package installs)
- [Ollama](https://github.com/ollama/ollama) installed and running
- [Piper](https://github.com/rhasspy/piper) TTS binary in PATH
- [faster-whisper](https://github.com/SYSTRAN/faster-whisper) model (downloaded automatically)

## Setup

1. Create and activate a virtual environment (recommended with `uv`):
```bash
uv venv

# Linux/macOS
source .venv/bin/activate

# Windows PowerShell
# .venv\Scripts\Activate.ps1
```

2. Install dependencies:
```bash
uv pip install -r requirements.txt
```

Alternative (without `uv`):
```bash
pip install -r requirements.txt
```

3. Download Whisper model (auto-downloaded on first run):
```bash
# Optional: Download a specific model
# faster-whisper downloads "small" by default
```

4. Piper TTS models (voices)

Piper uses compiled voice models (.onnx) paired with a small JSON metadata file. Place models under `~/.local/share/piper/models` (or set `PIPER_MODEL_DIR` in `.env`).

Example: download the English Lessac medium voice
```bash
mkdir -p ~/.local/share/piper/models
cd ~/.local/share/piper/models
curl -L -O https://github.com/rhasspy/piper/releases/download/2024.01.16/en_US-lessac-medium.onnx
curl -L -O https://github.com/rhasspy/piper/releases/download/2024.01.16/en_US-lessac-medium.onnx.json
```

Find additional voices and releases on the Piper GitHub releases page:

- https://github.com/rhasspy/piper/releases

After downloading, set the model name in `.env` (the filename of the `.onnx` file):

```
PIPER_MODEL=en_US-lessac-medium.onnx
# Or point to a directory containing multiple models
PIPER_MODEL_DIR=$HOME/.local/share/piper/models
```

Optional: use the Python package `piper-tts` instead of the CLI. Install with pip:

```bash
pip install piper-tts
```

`server.py` will prefer the Python package if available and fall back to the `piper` CLI. If you rely on the CLI, ensure the `piper` binary is installed and on your `PATH`.

5. Start Ollama:
```bash
ollama serve &
# Or: ollama run llama3
```

6. Start the server:
```bash
python server.py
```

## Clients

For the Tailscale bridge deployment that connects the ESP32 through a GL travel router and Raspberry Pi relay, see [TAILSCALE_BRIDGE.md](./TAILSCALE_BRIDGE.md).

### 1. Web Client (Node.js)
A modern, responsive web interface located in `client-node/`.
- **Chat UI**: Traditional message feed for transcripts and AI responses.
- **Waveform Visualizer**: Real-time visualization of microphone input.
- **Low Latency**: Uses `AudioWorkletNode` for high-performance audio capture.
- **Setup**: 
  ```bash
  cd client-node
  npm install
  node server.js
  ```
  Visit `http://localhost:3000` to use.

### 2. ESP32 Client (Arduino)
A standalone hardware client located in `client_esp32/`.
- **Streaming**: Streams I2S microphone data (16-bit, 16kHz mono) directly to the server.
- **Efficient Playback**: Supports raw binary audio frames for low-latency playback via I2S.
- **Display**: Real-time status ("Transcribing...", "Speaking...") and transcript display.
- **Button Behavior**: If the button is pressed while listening or playing audio, the client performs a hard stop, returns to boot state (`Press button to start a conversation.`), and ignores stale in-flight backend audio/messages until the next explicit start press.
- **Volume Control**: Software-based volume scaling for DACs without hardware controls.
- **Setup**: 
  - Open `client_esp32/client_esp32.ino` in Arduino IDE.
  - Install `ArduinoWebsockets` library.
  - Update `WIFI_SSID`, `WIFI_PASS`, and `SERVER_IP` in the sketch.
  - Flash to your ESP32 board.

### 3. Minimal Ping Tester
A simple CLI tool to verify connectivity:
```bash
python test_ping.py [SERVER_IP] 8080
```

## Configuration

Edit `.env` to configure:

| Variable | Default | Description |
|----------|---------|-------------|
| `OLLAMA_HOST` | http://localhost:11434 | Ollama API endpoint |
| `OLLAMA_MODEL` | llama3.2 | Model to use |
| `PIPER_MODEL` | en_US-amy-medium.onnx | Piper TTS model |
| `PIPER_MODEL_DIR` | (none) | Directory containing Piper models |
| `AUDIO_SAMPLE_RATE` | 16000 | Audio sample rate |
| `VAD_THRESHOLD` | 1.0 | Seconds of silence to trigger STT |
| `VAD_MIN_SPEECH` | 0.3 | Minimum seconds of speech to trigger |
| `VAD_ENERGY_THRESHOLD` | 0.161 | RMS energy threshold for VAD |
| `ENRICH_QUESTION_WITH_HINDSIGHT` | false | Enable optional memory recall enrichment for `QUESTION` |
| `WS_PORT` | 8080 | WebSocket server port |

## Prompt Routing And Memory

After transcription, the server classifies each utterance and routes work asynchronously:

- `FACT`: Immediate Ollama + TTS response, plus background `Hindsight.retain()`.
- `STATEMENT`: Immediate Ollama + TTS response, no memory operation.
- `QUESTION`: Immediate first-pass Ollama + TTS response, optional background `Hindsight.recall()` when `ENRICH_QUESTION_WITH_HINDSIGHT=true`.
- `QUERY`: Immediate first-pass Ollama + TTS response, required background `Hindsight.recall()`, and optional second context-aware Ollama/TTS follow-up when memory hits.

All non-empty transcriptions now produce an immediate audio response path.

## WebSocket Protocol

Connect to `ws://localhost:8080/ws`

### Client → Server

**Audio data**: Send raw PCM audio bytes (16-bit, 16kHz mono) as binary frames.

**JSON messages**:
```json
{"type": "transcribe"}  // Force transcribe current buffer
{"type": "ping"}
```

### Server → Client

**Binary frames**: Raw PCM audio data (16-bit, 16kHz mono) or WAV data for immediate playback.

**JSON messages**:
```json
{"type": "transcribing", "content": ""} // Server started Whisper processing
{"type": "text", "content": "Transcribed text..."}
{"type": "response", "content": "LLM response token..."}
{"type": "audio", "data": "<base64 audio>"} // Base64 TTS audio (legacy)
{"type": "done", "content": "Final response"} // Response stream complete
{"type": "error", "content": "Error message"}
{"type": "pong"}
{"type": "stop_recording"} // Server is processing, client should stop sending audio
```

**Binary frames**: Raw PCM audio (16-bit, 16kHz mono) for low-latency playback

## Testing

Use the included test client or a WebSocket testing tool:

```bash
# Example with websocat (https://github.com/bufbuild/websocat)
websocat ws://localhost:8080/ws
```

For audio testing, you'll need to send proper PCM audio data from a client.
