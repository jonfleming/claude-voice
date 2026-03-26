# GEMINI.md - Voice AI Pipeline

This file provides instructional context and development guidelines for the Voice AI Pipeline project.

## Project Overview

The **Voice AI Pipeline** is a Python-based WebSocket service designed for real-time, end-to-end audio processing. It integrates local AI models to minimize latency and ensure privacy.

### Key Technologies:
- **Python 3.11+**: Core programming language.
- **FastAPI**: Used for the WebSocket server and basic HTTP endpoints.
- **faster-whisper**: High-performance local implementation of OpenAI's Whisper for Speech-to-Text (STT).
- **Ollama**: Local Large Language Model (LLM) runner (e.g., Llama 3).
- **Piper**: Fast, local neural text-to-speech (TTS) engine.
- **uv**: Recommended for fast virtual environment and dependency management.

### Architecture:
1.  **WebSocket Client**: Sends raw PCM audio (16-bit, 16kHz, mono).
2.  **Server (FastAPI/Uvicorn)**: Receives audio, buffers it, and performs Voice Activity Detection (VAD).
3.  **STT (faster-whisper)**: Transcribes buffered audio to text once silence is detected.
4.  **LLM (Ollama)**: Processes transcribed text and streams a response.
5.  **TTS (Piper)**: Converts LLM response tokens into audio chunks on-the-fly.
6.  **WebSocket Client**: Receives JSON metadata and raw audio chunks for playback.

---

## Key Files

- `server.py`: The heart of the application. Contains:
    - `ConnectionManager`: Handles WebSocket connections.
    - `AudioBuffer`: Manages incoming audio bytes and implements VAD logic.
    - `transcribe_audio()`: Interfaces with `faster-whisper`.
    - `stream_to_ollama()`: Streams prompts to Ollama and triggers TTS for responses.
    - `text_to_speech()`: Executes the Piper TTS binary as a subprocess.
    - `app`: FastAPI application instance with `/ws` and index routes.
- `requirements.txt`: Lists necessary Python packages (FastAPI, uvicorn, faster-whisper, numpy, aiohttp, etc.).
- `.env`: Configuration for `OLLAMA_HOST`, `OLLAMA_MODEL`, `PIPER_MODEL`, and WebSocket settings.
- `README.md` & `CLAUDE.md`: Comprehensive setup and operational documentation.

---

## Building and Running

### Environment Setup
```bash
# Create and activate virtual environment
uv venv
source .venv/bin/activate

# Install dependencies
uv pip install -r requirements.txt
```

### External Requirements
- **Ollama**: Must be installed and running (`ollama serve`).
- **Piper**: The `piper` binary must be in your system `PATH`.
- **Piper Models**: Download `.onnx` and `.json` model files (default: `en_US-lessac-medium.onnx`).

### Starting the Server
```bash
python server.py
```
By default, the server runs on `0.0.0.0:8000`. You can access a simple web-based test client at `http://localhost:8000/`.

---

## Development Conventions

- **Asynchronous Execution**: The project heavily uses `asyncio` for WebSocket handling, HTTP requests to Ollama, and managing the Piper subprocess to ensure the server remains responsive.
- **Audio Processing**:
    - Standard Format: 16-bit PCM, 16kHz sample rate, Mono.
    - VAD: Simple energy-based silence detection is used to trigger transcription.
- **Error Handling**: WebSocket connections should be gracefully closed with informative error messages sent as JSON to the client.
- **Dependency Management**: Prefer `uv` for all package operations to ensure consistency and speed.
- **Environment Variables**: Always use `os.getenv` with sensible defaults; sensitive or environment-specific config belongs in `.env`.
