# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 voice assistant (Arduino framework) that records audio via I2S microphone, sends it to a backend server for STT (Whisper), generates responses via LLM (Ollama), and plays back TTS audio (Piper). Uses FreeRTOS tasks for concurrent audio recording/playback.

## Build / Upload / Monitor Commands

```sh
# Discover connected device port
arduino-cli board list

# Compile (uses arduino.json config for FQBN and port)
arduino-cli compile

# Upload (uses default port from arduino.json: /dev/ttyACM0)
arduino-cli upload

# Monitor serial output
arduino-cli monitor
```

Board config: `esp32:esp32:esp32s3:FlashSize=4M,PartitionScheme=huge_app,PSRAM=opi,CDCOnBoot=cdc`

## Architecture

- **Main sketch**: `client_esp32.ino` - entry point with VAD state machine and FreeRTOS task loops
- **Drivers**: I2S audio input/output, button handling, display (LVGL)
- **Backend**: WebSocket connection to `claude-voice` server on port 8080
- **Data flow**: Button press/VAD → capture audio to PSRAM → send PCM via WebSocket → receive base64 audio → decode → I2S playback
- **Multitasking**: Separate FreeRTOS tasks for recording (`loop_task_sound_recorder`), playback, and UI

## Key Configuration

- WiFi SSID/Password and server IP: defined in `client_esp32.ino`
- Server port: `CLAUDE_VOICE_PORT = 8080`
- Audio pins: SCK=3, WS=14, DIN=46 (input), BCLK=42, LRC=41, DOUT=1 (output)
- Button pin: 19

## Code Style

- Header guards: `#ifndef DRIVER_NAME_H`
- Include directives: `#include <...>` for libraries, `#include "..."` for local headers
- Explicit types: `uint8_t`, `int16_t`, etc.
- Classes: PascalCase, members: `lower_snake_case` or trailing underscore
- Constants/macros: `ALL_CAPS_SNAKE`
- Global drivers: declare as `extern YourType instance;` in header, define in one .cpp
- Class members: `public:` → `protected:` → `private:`

## Critical Notes

- **Never mix Freenove/Espressif libraries** - incompatible APIs
- Background tasks must NOT call LVGL directly - use thread-safe request buffers (e.g., `display_line1_buf`)
- Display updates require taking `display_mutex` from background tasks
- PSRAM used for audio buffer (4MB)
- Zero-copy JSON parsing to minimize heap fragmentation
- Board config uses PSRAM=opi (OPI mode for external PSRAM)