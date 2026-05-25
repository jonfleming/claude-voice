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

Board config: `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=fatflash,PSRAM=opi`

## Architecture

- **Main sketch**: `client_aipi.ino` - entry point with VAD state machine and FreeRTOS task loops
- **Drivers**: I2S audio input/output (ES8311 codec via I2C), button handling, display (LVGL with ST7735)
- **Backend**: WebSocket connection to `claude-voice` server on port 8080
- **Data flow**: Button press/VAD → capture audio to PSRAM → send PCM via WebSocket → receive base64 audio → decode → I2S playback
- **Multitasking**: Separate FreeRTOS tasks for recording (`loop_task_sound_recorder`), playback, and UI

## Key Configuration (AIPI-Lite)

- WiFi SSID/Password and server IP: defined in `client_aipi.ino`
- Server port: `CLAUDE_VOICE_PORT = 8080`
- **Audio pins (I2S)**: MCLK=6, BCLK=14, LRCLK=12, DIN=13 (input from ES8311), DOUT=11 (output to ES8311)
- **Audio codec (ES8311)**: I2C SDA=5, SCL=4, Address=0x18
- **Button pin**: GPIO42 (Right button, active-LOW)
- **Display (ST7735)**: SCK=16, MOSI=17, DC=7, CS=15, Reset=18, Backlight=3 (128×128 pixels)
- **Power Management**: Keep-alive=GPIO10 (CRITICAL), Battery ADC=GPIO2
- **Speaker Amp Enable**: GPIO9

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