# Claude-Voice Deployment Guide

Complete setup and deployment documentation for the Voice AI Pipeline — from server installation to ESP32 hardware flashing.

---

## Table of Contents

1. [Server Setup](#1-server-setup)
2. [ESP32 Client — Arduino IDE](#2-esp32-client---arduino-ide)
3. [ESP32 Client — arduino-cli](#3-esp32-client---arduino-cli)
4. [Supported Hardware Boards](#4-supported-hardware-boards)
5. [Configuration Reference](#5-configuration-reference)
6. [Tailscale Bridge Deployment](#6-tailscale-bridge-deployment)
7. [Remote Debugging](#7-remote-debugging)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Server Setup

### Requirements

- Python 3.10+
- [uv](https://docs.astral.sh/uv/) (recommended for fast virtual env + package installs)
- [Ollama](https://github.com/ollama/ollama) installed and running
- [Piper](https://github.com/rhasspy/piper) TTS binary in PATH (or `piper-tts` Python package)
- [faster-whisper](https://github.com/SYSTRAN/faster-whisper) model (auto-downloaded on first run)

### Step 1 — Create virtual environment

```bash
cd /path/to/claude-voice
uv venv
source .venv/bin/activate   # Linux/macOS
# .venv\Scripts\Activate.ps1   # Windows PowerShell
```

### Step 2 — Install dependencies

```bash
uv pip install -r requirements.txt
```

### Step 3 — Download a Piper TTS voice model

Piper uses compiled `.onnx` voice models paired with a `.json` metadata file.

```bash
mkdir -p ~/.local/share/piper/models
cd ~/.local/share/piper/models

# Download English Lessac medium voice
curl -L -o en_US-lessac-medium.onnx \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx
curl -L -o en_US-lessac-medium.onnx.json \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json
```

Find additional voices at: https://huggingface.co/rhasspy/piper-voices

After downloading, set the model name in `.env`:

```
PIPER_MODEL=en_US-lessac-medium.onnx
# Optional: point to a directory containing multiple models
PIPER_MODEL_DIR=$HOME/.local/share/piper/models
```

### Step 4 — Start Ollama

```bash
ollama serve &
# Or pull a model first:
ollama pull llama3.2
```

### Step 5 — Configure `.env`

Edit `/path/to/claude-voice/.env` with the following variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `OLLAMA_HOST` | `http://localhost:11434` | Ollama API endpoint |
| `OLLAMA_MODEL` | `llama3.2` | Model to use |
| `PIPER_MODEL` | `en_US-lessac-medium.onnx` | Piper TTS model filename |
| `PIPER_MODEL_DIR` | *(none)* | Directory containing Piper models |
| `AUDIO_SAMPLE_RATE` | `16000` | Audio sample rate |
| `VAD_THRESHOLD` | `1.0` | Seconds of silence to trigger transcription |
| `VAD_MIN_SPEECH` | `0.3` | Minimum seconds of speech to trigger |
| `VAD_ENERGY_THRESHOLD` | `0.161` | RMS energy threshold for VAD |
| `ENRICH_QUESTION_WITH_HINDSIGHT` | `false` | Enable optional memory recall for `QUESTION` classification |
| `WS_PORT` | `8080` | WebSocket server port |

### Step 6 — Start the server

```bash
python server.py
```

The server listens on `ws://0.0.0.0:8080/ws` by default.

---

## 2. ESP32 Client — Arduino IDE

### Step 1 — Install required libraries

In Arduino IDE (Sketch → Include Library → Manage Libraries):

| Library | Author | Purpose |
|---------|--------|---------|
| **ArduinoWebsockets** | Gil Maimon | WebSocket client for ESP32 |
| **LVGL** | LVGL Organization | Display rendering |
| **RemoteDebug** | JoaoLopesF | Wireless serial debugging |
| **TFT_eSPI** | Bodmer | Display driver (board-specific) |

Some libraries (Freenove, Espressif) may require manual `.zip` install from their GitHub releases.

### Step 2 — Open the sketch

Open `client_esp32/client_esp32.ino` in Arduino IDE.

### Step 3 — Edit configuration

In `client_esp32.ino`, find the configuration section (around line 99) and edit:

```cpp
#define WIFI_SSID "your_wifi_network"
#define WIFI_PASS "your_wifi_password"

#define SERVER_IP "192.168.8.50"   // IP of your Python server
#define CLAUDE_VOICE_WS_PORT 8080
#define CLAUDE_VOICE_WS_PATH "/ws"

#define OLLAMA_MODEL "llama3.2"
```

### Step 4 — Select board

In Arduino IDE: **Tools → Board → ESP32 Arduino → ESP32-S3 Dev Module**

Then set the following under **Tools**:

| Setting | Value |
|---------|-------|
| CDC On Boot | `cdc` |
| CPU Frequency | `240MHz` |
| Debug Level | `none` |
| Erase Flash | `all` |
| Events Core | `1` |
| Flash Mode | `qio` |
| Flash Size | `16M` |
| MSC On Boot | `default` |
| Partition Scheme | `fatflash` |
| PSRAM | `opi` |
| Upload Mode | `cdc` |
| Upload Speed | `921600` |
| USB Mode | `hwcdc` |

### Step 5 — Flash

Connect the ESP32 via USB and click **Upload**.

---

## 3. ESP32 Client — arduino-cli

### Prerequisites

- `arduino-cli` installed
- ESP32 board package: `arduino-cli core install esp32:esp32`
- Required libraries installed (same as Arduino IDE section above)

### Build

```bash
cd /path/to/claude-voice/client_esp32
arduino-cli compile --fqbn esp32:esp32:esp32s3 --build-property "compiler.c.extra_flags=-DARDUINO_USB_CDC_ON_BOOT=1" --build-property "compiler.cpp.extra_flags=-DARDUINO_USB_CDC_ON_BOOT=1" client_esp32.ino
```

### Upload

```bash
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 client_esp32.ino
```

Replace `/dev/ttyACM0` with the correct port. Find it with:

```bash
arduino-cli board list
```

### Monitor serial output

```bash
arduino-cli monitor -p /dev/ttyACM0
```

---

## 4. Supported Hardware Boards

The sketch supports **three** hardware boards, selected via compile-time define in `sketch_config.h`:

| Board | Define | Display | Audio | Notes |
|-------|--------|---------|-------|-------|
| **Freenove ESP32-S3 Media Kit** (default) | *(none)* | ST7796 320x480 parallel TFT | External I2S mic + I2S DAC | No battery, always powered via USB |
| **AIPI Lite** | `BOARD_AIPI_LITE` | ST7735 128x128 SPI | ES8311 I2S codec (shared peripheral) | Battery powered, GPIO10 must be HIGH on boot |
| **Waveshare AMOLED 1.8** | `BOARD_WAVESHARE_AMOLED` | SH8601 368x448 QSPI AMOLED | ES8311 I2S codec + FT3168 touch + AXP2101 PMU | SD card slot, touch input |

### Board-specific notes

#### Freenove (default)

- Button: GPIO 19
- Audio Input: SCK=3, WS=14, DIN=46
- Audio Output: BCLK=42, LRC=41, DOUT=1
- No special boot requirements

#### AIPI Lite

- Button: GPIO 1 (also hardware power button)
- Shared I2S peripheral: mic and speaker use the same GPIO pins (6, 14, 12)
- **CRITICAL:** GPIO 10 must be set HIGH on boot to keep power on
- Speaker amp enable: GPIO 9 (assert before playback)
- After recording, call `audio_input_deinit()` then `delay(10)` before playback
- After playback, call `audio_input_init_mclk(...)` to re-arm the mic

#### Waveshare AMOLED 1.8

- Button: GPIO 0 (boot button)
- Display: SH8601 via QSPI (SDIO0-3 + SCLK + CS)
- Touch: FT3168 via I2C (SDA=15, SCL=14)
- Power: AXP2101 PMU via I2C (addr 0x34)
- SD Card: SDMMC (CLK=2, CMD=1, DATA=3)

### GPIO reference: AIPI Lite

| Function | GPIO | Notes |
|----------|------|-------|
| Power Keep-Alive | 10 | **CRITICAL:** Must be HIGH on boot |
| Battery ADC | 2 | 12dB attenuation, multiply by 2.0 |
| Display SCK | 16 | SPI clock |
| Display MOSI | 17 | SPI data out |
| Display CS | 15 | |
| Display DC | 7 | Data/command select |
| Display RST | 18 | |
| Display BL | 3 | Strapping pin — works with warning |
| ES8311 I2C SDA | 5 | |
| ES8311 I2C SCL | 4 | |
| I2S MCLK | 6 | Shared with mic |
| I2S BCLK | 14 | Shared with mic |
| I2S LRCLK | 12 | Shared with mic |
| I2S DOUT | 11 | To speaker |
| Speaker Amp EN | 9 | Assert before playback |
| Left Button | 1 | Also hardware power button |
| Right Button | 42 | Standard GPIO button |

---

## 5. Configuration Reference

### Server `.env` (Python backend)

See [Server Setup](#5--configure-env) above for full table.

### ESP32 `sketch_config.h`

Controls board selection and USB boot behavior:

```cpp
// Uncomment ONE board:
// #define BOARD_AIPI_LITE
// #define BOARD_WAVESHARE_AMOLED

// USB boot settings (default values):
#define ARDUINO_USB_CDC_ON_BOOT 1
#define ARDUINO_USB_MODE 1
#define ARDUINO_USB_MSC_ON_BOOT 0
#define ARDUINO_USB_DFU_ON_BOOT 0
```

### ESP32 `settings.json` (VS Code / Arduino extension)

```json
{
    "sketch": "client_esp32.ino",
    "board": "esp32:esp32:esp32s3",
    "configuration": "FlashSize=16M,PartitionScheme=fatflash,PSRAM=opi,CDCOnBoot=cdc,USBMode=tinyusb,UploadSpeed=115200",
    "output": "build",
    "port": "/dev/ttyACM0"
}
```

### WebSocket Protocol

**Connect to:** `ws://<SERVER_IP>:8080/ws`

**Client → Server:**
- **Binary:** Raw PCM audio (16-bit, 16kHz, mono)
- **JSON:** `{"type": "transcribe"}` or `{"type": "ping"}`

**Server → Client:**
| Message | Description |
|---------|-------------|
| `{"type": "transcribing"}` | Server started Whisper processing |
| `{"type": "text", "content": "..."}` | Transcribed text |
| `{"type": "response", "content": "..."}` | LLM streaming token |
| `{"type": "audio", "data": "<base64>"}` | Legacy TTS audio (base64) |
| **Binary** | Raw PCM audio for low-latency playback |
| `{"type": "done", "content": "..."}` | Response stream complete |
| `{"type": "error", "content": "..."}` | Error message |
| `{"type": "stop_recording"}` | Server processing, stop sending audio |
| `{"type": "pong"}` | Ping response |

---

## 6. Tailscale Bridge Deployment

For deployments where the ESP32 cannot reach the home server directly over Wi-Fi.

### Architecture

```
ESP32-S3 → GL Travel Router (Wi-Fi) → Raspberry Pi Zero 2W (socat relay) → Tailscale → Home Server
```

### ESP32 configuration for bridge

Set `SERVER_IP` to the **Raspberry Pi's static LAN IP** (not the home server):

```cpp
#define SERVER_IP "192.168.8.145"   // Pi Zero 2W LAN IP
```

### Raspberry Pi setup

1. Install Tailscale on the Pi
2. Set up `socat` to forward the WebSocket:

```bash
# Listen on port 8080 and forward to home server over Tailscale
socat TCP-LISTEN:8080,fork,reuseaddr TCP:<home-server-tailscale-ip>:8080
```

### Home server

Run `server.py` as normal. The WebSocket endpoint is the same — the Pi bridge is transparent to the protocol.

---

## 7. Remote Debugging

The ESP32 uses [RemoteDebug](https://github.com/JoaoLopesF/RemoteDebug) to expose all `[Recorder]`, `[Button]`, `[Loop]`, and `[WS]` log output over telnet.

### Connect via telnet

```bash
# Using mDNS hostname (recommended)
telnet claude-voice-esp32

# Or by IP address (printed in serial output during WiFi setup)
telnet 192.168.x.x
```

> **Windows:** Telnet is disabled by default. Enable via *Control Panel → Programs → Turn Windows features on or off → Telnet Client*, or use PuTTY in raw/telnet mode.

### Connect via USB

When the USB cable is connected, log output mirrors to the Arduino Serial Monitor simultaneously.

---

## 8. Troubleshooting

### ESP32 won't connect to Wi-Fi

- Verify `WIFI_SSID` and `WIFI_PASS` are correct in `client_esp32.ino`
- Check the serial output for the error message
- Try a 2.4 GHz network (ESP32 does not support 5 GHz Wi-Fi)
- Ensure the AP allows connections from unknown devices

### ESP32 connects to Wi-Fi but WebSocket fails

- Verify `SERVER_IP` is the correct IP address of the machine running `server.py`
- Check that `server.py` is running and listening on port 8080:
  ```bash
  ss -tlnp | grep 8080
  ```
- Ensure no firewall is blocking the port:
  ```bash
  # Linux
  sudo ufw allow 8080/tcp
  # Windows
  netsh advfirewall firewall add rule name="ClaudeVoice" dir=in action=allow protocol=TCP localport=8080
  ```
- Use the ping tester to verify connectivity:
  ```bash
  python test_ping.py <SERVER_IP> 8080
  ```

### AIPI Lite won't stay powered on

- GPIO 10 must be set HIGH on boot — this is the power keep-alive pin
- Check that the battery is connected and the power switch is ON
- If the device boots then immediately shuts down, the battery may be depleted

### AIPI Lite speaker outputs bus noise

The mic and speaker share the same I2S peripheral. Before playback:

1. Call `audio_input_deinit()` after recording finishes
2. `delay(10)` to flush RX FIFO
3. Call the speaker path (which reinitialises the peripheral in TX mode)
4. After playback, call `audio_input_init_mclk(...)` to re-arm the mic

Skipping step 1 leaves the mic driver owning the port — the speaker outputs bus noise instead of audio.

### Display shows garbage or stays blank

- Verify the correct board is selected (`sketch_config.h`)
- Check that the TFT_eSPI library is installed with the correct user setup
- For AIPI Lite: ensure GPIO 16 (SCK) and GPIO 17 (MOSI) are wired correctly
- For Waveshare: verify QSPI wiring (SDIO0-3, SCLK, CS)

### Server returns TTS errors

- Verify the Piper model file exists:
  ```bash
  ls ~/.local/share/piper/models/en_US-lessac-medium.onnx
  ```
- Check that `PIPER_MODEL` in `.env` matches the filename (without `.json`)
- If using the Python `piper-tts` package, install it:
  ```bash
  pip install piper-tts
  ```
- The server prefers the Python package if available, falls back to the `piper` CLI

### Server returns STT errors

- The first run auto-downloads the Whisper model — check disk space
- Verify the model file is not corrupted
- Check that `faster-whisper` is installed:
  ```bash
  pip show faster-whisper
  ```

### Ollama model not found

- Pull the model first:
  ```bash
  ollama pull llama3.2
  ```
- Verify `OLLAMA_MODEL` in `.env` matches an installed model:
  ```bash
  ollama list
  ```
- Check that Ollama is running:
  ```bash
  curl http://localhost:11434/api/tags
  ```

### WebSocket server not running

- The server may not have started — check for Python errors in the terminal
- The integration tests confirmed the WS server was not running in test runs
- Verify port 8080 is not in use by another process:
  ```bash
  lsof -i :8080
  ```

### Build fails with arduino-cli

- Ensure the ESP32 core is installed:
  ```bash
  arduino-cli core install esp32:esp32
  ```
- Verify the board configuration matches your hardware
- For AIPI Lite, add the board define:
  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32s3 \
    --build-property "compiler.c.extra_flags=-DBOARD_AIPI_LITE" \
    --build-property "compiler.cpp.extra_flags=-DBOARD_AIPI_LITE" \
    client_esp32.ino
  ```

### Library conflicts (Freenove vs Espressif)

- Do not blindly upgrade or mix Freenove and Espressif core/driver libraries
- They can have API conflicts and different hardware assumptions
- Document the version and source of each non-Arduino-registry library
- If adding a new driver, test with both board profiles

### Flash size / partition errors

- The partition scheme `fatflash` reserves space for SPIFFS/filesystem
- If you need more app space, change to `min_spiffs` or `default_8MB`
- Verify PSRAM is set to `opi` for the Freenove board (it has external PSRAM)

---

## Quick Reference

### Server start

```bash
cd /path/to/claude-voice
source .venv/bin/activate
python server.py
```

### ESP32 flash (Arduino IDE)

1. Open `client_esp32.ino`
2. Edit `WIFI_SSID`, `WIFI_PASS`, `SERVER_IP`
3. Tools → Board → ESP32-S3 Dev Module
4. Tools → Upload Speed → 921600
5. Click Upload

### ESP32 flash (arduino-cli)

```bash
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 client_esp32.ino
```

### Server health check

```bash
python test_ping.py <SERVER_IP> 8080
```

### Remote debug (ESP32)

```bash
telnet claude-voice-esp32
```
