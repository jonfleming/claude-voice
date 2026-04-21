# Voice AI Pipeline Design Decisions

## Memory Layer (Hindsight)

### Async Recall Pattern
To avoid blocking the voice conversation turn, recall requests are decoupled from the current turn:

1. **Turn N**: User speaks → Server responds immediately (no recall delay)
2. **Background**: After response, queue recall task asynchronously (`asyncio.create_task`)
3. **Turn N+1**: Include any pending memories from previous turn in the LLM context

This prevents the 1-2 second recall delay from adding to the user's wait time.

### Implementation
- `pending_memories`: List that holds memories from the previous turn
- `queue_recall(query)`: Background task that calls Hindsight and stores results in `pending_memories`
- At the start of each turn, check `pending_memories` and prepend to LLM context if present

### Configuration
- `HINDSIGHT_HOST`: Base URL for Hindsight (default: `http://localhost:8888`)
- `HINDSIGHT_BANK`: Memory bank ID (default: `default`)
- `budget`: Recall parameter controlling depth/breadth of query (default: `"low"`)

## Voice Activity Detection (VAD)

### Energy-based Detection
- Uses RMS (root mean square) of audio amplitude to detect speech
- `VAD_ENERGY_THRESHOLD`: Minimum energy to consider as speech (default: `0.005`)
- `VAD_THRESHOLD`: Seconds of silence before triggering transcription (default: `1.0`)
- `VAD_MIN_SPEECH`: Minimum speech duration required (default: `0.3s`)

### Windowed Processing
- Audio is processed in 100ms windows for stable RMS calculation
- Helps prevent spurious triggers from brief noise spikes

### Hallucination Filter
- Whisper can hallucinate common phrases ("thank you", "bye", etc.) on low-energy noise
- Filtered when: phrase detected AND overall RMS < 2x energy threshold

## Text-to-Speech (TTS)

### Streaming TTS
- Piper TTS generates audio incrementally as the LLM responds
- Trigger: When pending text exceeds 20 chars AND contains sentence-ending punctuation, OR exceeds 120 chars
- Splits at the last punctuation mark to maintain sentence integrity

### Dual Output
- Sends both raw binary audio and base64-encoded JSON
- Binary: Efficient for ESP32 clients
- JSON: Fallback for web browsers that may not handle binary well

## WebSocket Protocol

### Client → Server
- Binary: Raw PCM audio (16-bit, 16kHz, mono)
- JSON: `{"type": "transcribe"}`, `{"type": "ping"}`, `{"type": "stream"}`

### Server → Client
- `{"type": "text", "content": "..."}` - Transcription result
- `{"type": "response", "content": "..."}` - LLM token
- `{"type": "audio", "data": "base64..."}` - TTS audio (JSON)
- Binary audio frames - TTS audio (raw)
- `{"type": "done", "content": "..."}` - Full response complete
- `{"type": "error", "content": "..."}` - Error message
- `{"type": "stop_recording"}` - Tell client to stop sending audio

## References

- [CLAUDE.md](./CLAUDE.md) - Project overview and setup instructions
- [Hindsight Documentation](https://hindsight.vectorize.io/) - Memory layer