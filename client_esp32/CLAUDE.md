# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-S3 voice assistant project (Arduino framework) that records audio via I2S microphone, sends it to a backend server for transcription (Whisper), generates responses via LLM (Ollama), and plays back TTS audio (Piper). Target board is `esp32:esp32:esp32s3` or `freenove_esp32s3_mediakit`.

## Build / Upload / Monitor Commands

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3 voice_assistant_esp32.ino
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3 voice_assistant_esp32.ino
arduino-cli monitor -p COM3
```
*Note: Change COM3 to your actual port (`arduino-cli board list` to discover).*

## Architecture

- **Main sketch**: `voice_assistant_esp32.ino` - entry point with VAD state machine and task loops
- **Drivers**: `driver_audio_input.cpp/h` (I2S mic), `driver_audio_output.cpp/h` (I2S speaker), `driver_button.cpp/h`, `display.cpp/h`
- **Backend**: `claude-voice` server on port 8080 (WebSocket) - handles STT, LLM, and TTS
- **VAD task**: Runs continuously, monitors audio energy to detect speech
- **Recording flow**: Button press or VAD triggers audio capture → WAV stored in PSRAM → send via WebSocket → receive base64 audio → decode → play

## Key Configuration

- WiFi SSID/Password and server IP defined in `voice_assistant_esp32.ino` (lines 42-44)
- Server port: `CLAUDE_VOICE_PORT = 8080`
- Audio pins: SCK=3, WS=14, DIN=46 (input), BCLK=42, LRC=41, DOUT=1 (output)
- Button pin: 19

## Code Style

- Header guards: `#ifndef DRIVER_NAME_H`
- Use `#include <...>` for libraries, `#include "..."` for local headers
- Explicit types (`uint8_t`, `int16_t`)
- Classes: PascalCase, members: `lower_snake_case` or trailing underscore
- Constants/macros: `ALL_CAPS_SNAKE`

## Critical Notes

- **Never mix Freenove/Espressif libraries** - they have incompatible APIs
- Background tasks must NOT call LVGL directly - use thread-safe request buffers (see `display_line1_buf`, etc.)
- PSRAM used for audio buffer (4MB allocated)
- Display updates require taking `display_mutex` from background tasks