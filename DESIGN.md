# Voice AI Pipeline Design Decisions

## Memory Layer (Hindsight)

### Prompt Classification And Routing
Each non-empty transcription is classified into one of four categories:

- `FACT`: Declarative personal fact worth retaining.
- `STATEMENT`: Declarative content that does not require memory storage.
- `QUESTION`: General question that can be answered without prior context.
- `QUERY`: Context-dependent question that should use recalled memory.

### Async Two-Pass Response Pattern
To keep voice turns responsive, the server always sends an immediate first-pass LLM+TTS response, while memory operations run asynchronously:

1. **Immediate pass**: Classify text and start Ollama streaming right away; TTS audio is enqueued as tokens arrive.
2. **Background memory work**:
	- `FACT`: run `retain()` asynchronously while first-pass audio is playing.
	- `QUESTION`: optional `recall()` in background when enrichment is enabled.
	- `QUERY`: required `recall()` in background.
3. **Conditional follow-up**: if recall returns relevant context and turn is still active, run a second context-aware Ollama pass and enqueue follow-up TTS.

Late/duplicate background completions are ignored for canceled or superseded turns.

### Implementation
- `classify_prompt_type(text)`: Returns `FACT`, `STATEMENT`, `QUESTION`, or `QUERY`.
- `retain_memory_async(...)`: Non-blocking memory write used for `FACT`.
- `recall_memories_async(...)`: Non-blocking memory lookup used for `QUESTION` (optional) and `QUERY` (required).
- Turn guards (`turn_id`, active-turn checks): Prevent stale memory results from affecting newer turns.
- `tts_queue` worker: Ensures FIFO audio playback for first and follow-up responses.

### Configuration
- `HINDSIGHT_HOST`: Base URL for Hindsight (default: `http://localhost:8888`)
- `HINDSIGHT_BANK`: Memory bank ID (default: `default`)
- `budget`: Recall parameter controlling depth/breadth of query (default: `"low"`)
- `ENRICH_QUESTION_WITH_HINDSIGHT`: Enables optional recall enrichment for `QUESTION` prompts (default: `false`)

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