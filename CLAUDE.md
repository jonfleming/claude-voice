# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Voice AI Pipeline - a WebSocket-based service that processes audio end-to-end:
1. Receives audio over WebSocket
2. Converts speech to text (STT) using local Whisper
3. Sends text to local Ollama LLM
4. Converts LLM response to speech (TTS) using local Piper
5. Sends audio back to client

## Commands

### Create uv virtual environment
```bash
uv venv
source .venv/bin/activate
```

### Install dependencies
```bash
uv pip install -r requirements.txt

# Alternative without uv:
# pip install -r requirements.txt
```

### Run the server
```bash
python server.py
```

### Download Piper TTS model
```bash
mkdir -p ~/.local/share/piper/models
cd ~/.local/share/piper/models
curl -O https://github.com/rhasspy/piper/releases/download/2024.01.16/en_US-lessac-medium.onnx
curl -O https://github.com/rhasspy/piper/releases/download/2024.01.16/en_US-lessac-medium.onnx.json
```

### Start Ollama
```bash
ollama serve &
# Or: ollama run llama3
```

## Configuration

Edit `.env` file:
- `OLLAMA_HOST` - Ollama API endpoint (default: http://localhost:11434)
- `OLLAMA_MODEL` - Model name (default: llama3)
- `PIPER_MODEL` - TTS model file
- `WS_PORT` - WebSocket port (default: 8080)
- `VAD_THRESHOLD` - Seconds of silence to trigger transcription (default: 1.5)

## WebSocket Protocol

Connect to `ws://localhost:8080/ws`

**Client → Server:**
- Binary: Raw PCM audio (16-bit, 16kHz, mono)
- JSON: `{"type": "transcribe"}` or `{"type": "ping"}`

**Server → Client:**
- `{"type": "text", "content": "..."}` - Transcription result
- `{"type": "response", "content": "..."}` - LLM token
- `{"type": "audio", "data": "base64..."}` - TTS audio
- `{"type": "error", "content": "..."}` - Error message
- `{"type": "done", "content": "..."}` - Full LLM response

## Architecture

- **server.py**: Main WebSocket server with:
  - `AudioBuffer` class: Handles audio buffering and VAD detection
  - `transcribe_audio()`: STT using faster-whisper
  - `stream_to_ollama()`: LLM integration with streaming TTS
  - `text_to_speech()`: TTS using Piper

- **requirements.txt**: Python dependencies
- **.env**: Configuration
- **README.md**: Setup documentation