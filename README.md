# Voice AI Pipeline

A WebSocket-based service that processes audio end-to-end:

1. **STT**: Receives audio, converts speech to text using local Whisper
2. **LLM**: Sends text to local Ollama, receives streamed response
3. **TTS**: Converts response to speech using local Piper TTS
4. **Audio**: Sends synthesized speech back to client

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

4. Download Piper TTS model:
```bash
# For English medium voice
mkdir -p ~/.local/share/piper/models
cd ~/.local/share/piper/models
curl -O https://github.com/rhasspy/piper/releases/download/2024.01.16/en_US-lessac-medium.onnx
curl -O https://github.com/rhasspy/piper/releases/download/2024.01.16/en_US-lessac-medium.onnx.json
```

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
| `OLLAMA_MODEL` | llama3 | Model to use |
| `PIPER_MODEL` | en_US-lessac-medium.onnx | Piper TTS model |
| `AUDIO_SAMPLE_RATE` | 16000 | Audio sample rate |
| `VAD_THRESHOLD` | 1.5 | Seconds of silence to trigger STT |
| `VAD_ENERGY_THRESHOLD` | 0.01 | RMS energy threshold for VAD |
| `WS_PORT` | 8080 | WebSocket server port |

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
{"type": "audio", "data": "<base64 audio>"} // Legacy support
{"type": "done", "content": "Final response"} // Response stream complete
{"type": "error", "content": "Error message"}
{"type": "pong"}
```

## Testing

Use the included test client or a WebSocket testing tool:

```bash
# Example with websocat (https://github.com/bufbuild/websocat)
websocat ws://localhost:8080/ws
```

For audio testing, you'll need to send proper PCM audio data from a client.