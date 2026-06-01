# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See [DESIGN.md](./DESIGN.md) for detailed design decisions and architectural rationale.

## Project Overview

Voice AI Pipeline - a WebSocket-based service that processes audio end-to-end:
1. Receives audio over WebSocket
2. Converts speech to text (STT) using local Whisper
3. Classifies transcribed text as FACT, STATEMENT, QUESTION, or QUERY
4. Sends text to local Ollama LLM
5. Converts LLM response to speech (TTS) using local Piper
6. Sends audio back to client

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
- `ENRICH_QUESTION_WITH_HINDSIGHT` - Optional recall enrichment for QUESTION classification (default: false)

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
  - `classify_prompt_type()`: Classifies into FACT/STATEMENT/QUESTION/QUERY
  - `stream_to_ollama()`: LLM integration with streaming TTS
  - `text_to_speech()`: TTS using Piper
  - Routing behavior:
    - FACT: immediate response + async `Hindsight.retain()`
    - STATEMENT: immediate response only
    - QUESTION: immediate response + optional recall enrichment
    - QUERY: immediate response + required recall, optional second context-aware follow-up
  - All non-empty transcriptions produce an immediate Ollama/TTS audio response path
- **ESP32 client state model**:
  - Button press while listening or playing is a hard stop to boot/idle state.
  - In idle state, stale in-flight conversation audio/messages from backend are ignored.
  - A new conversation starts only on an explicit new button press.

- **requirements.txt**: Python dependencies
- **.env**: Configuration
- **README.md**: Setup documentation