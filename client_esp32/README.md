# ESP32 Voice Assistant Client

A standalone hardware client for the Voice AI Pipeline, designed for ESP32 development boards with I2S microphone and DAC/Speaker support.

## Supported Hardware Boards

| Board | Board Select | Display | Audio | Touch | Power | Price (est.) |
|-------|-------------|---------|-------|-------|-------|-------------|
| **Freenove ESP32-S3 Media Kit** (default) | *(none)* | ST7796 320x480 parallel TFT | External I2S mic + I2S DAC | None | USB only | ~$25 |
| **AIPI-Lite** | `BOARD_AIPI_LITE` | ST7735 128x128 SPI | ES8311 I2S codec | None | Battery (GPIO10 keep-alive) | ~$18 |
| **Waveshare ESP32-S3-Touch-AMOLED-1.8** | `BOARD_WAVESHARE_AMOLED` | SH8601 368x448 QSPI AMOLED | ES8311 I2S codec | FT3168 I2C touch | AXP2101 PMU + battery | ~$35 |

### Board-specific notes

#### Freenove (default)
- Button: GPIO 19
- Audio Input: SCK=3, WS=14, DIN=46
- Audio Output: BCLK=42, LRC=41, DOUT=1
- No special boot requirements
- No battery — always powered via USB

#### AIPI-Lite
- Button: GPIO 1 (also hardware power button)
- Shared I2S peripheral: mic and speaker use the same GPIO pins (6, 14, 12)
- **CRITICAL:** GPIO 10 must be set HIGH on boot to keep power on
- Speaker amp enable: GPIO 9 (assert before playback)
- After recording, call `audio_input_deinit()` then `delay(10)` before playback
- After playback, call `audio_input_init_mclk(...)` to re-arm the mic

#### Waveshare AMOLED 1.8
- **Display:** SH8601 QSPI AMOLED (368x448) via Arduino_GFX (Arduino_SH8601 driver)
- **Touch:** FT3168 capacitive touch at I2C 0x5D (SDA=GPIO15, SCL=GPIO14)
- **Power:** AXP2101 PMU at I2C 0x34 for battery monitoring and power management
- **SD Card:** SDMMC 1-bit mode (CLK=GPIO2, CMD=GPIO1, DATA=GPIO3)
- **Audio:** ES8311 I2S codec (MCK=GPIO16, BCK=GPIO9, DIN=GPIO8, WS=GPIO45, DO=GPIO10)
- **Speaker Amp:** GPIO 46
- **Button:** GPIO 0 (boot button)
- **Partition scheme:** Requires `app3M_fat9M_16MB` (3MB app space) — sketch exceeds default 1.2MB limit
- **Flash size:** 16MB (16M)

## Hardware Requirements

- **ESP32 Core**: ESP32-S3 or standard ESP32.
- **Microphone**: I2S Digital Microphone (e.g., INMP441, MSM261S4030H0).
- **Speaker/DAC**: I2S DAC (e.g., MAX98357A, PCM5102) and a speaker.
- **Display**: SPI or I2C display supported by LVGL (optional, configured via `display.cpp`).
- **Button**: Physical button for push-to-talk (default: GPIO 19).

## Pin Configuration (Default — Freenove)

| Component | Pin (GPIO) |
|-----------|------------|
| Button | 19 |
| Audio Input SCK | 3 |
| Audio Input WS | 14 |
| Audio Input DIN | 46 |
| Audio Output BCLK | 42 |
| Audio Output LRC | 41 |
| Audio Output DOUT | 1 |

*Note: Pins are board-specific. See [DEPLOYMENT_GUIDE.md](../DEPLOYMENT_GUIDE.md#4-supported-hardware-boards) for full GPIO pinout tables for all boards.*

## Setup & Flash

### 1. Libraries

Install the following libraries in Arduino IDE:

| Library | Author | Purpose |
|---------|--------|---------|
| **ArduinoWebsockets** | Gil Maimon | WebSocket client for ESP32 |
| **LVGL** | LVGL Organization | Display rendering |
| **RemoteDebug** | JoaoLopesF | Wireless serial debugging |
| **TFT_eSPI** | Bodmer | Display driver (board-specific) |

Some libraries (Freenove, Espressif) may require manual `.zip` install from their GitHub releases.

### 2. Board Configuration

Edit `client_esp32.ino` and set the board define in `sketch_config.h`:

```cpp
// Uncomment ONE board:
// #define BOARD_AIPI_LITE
// #define BOARD_WAVESHARE_AMOLED
```

### 3. Arduino IDE Setup

#### Freenove (default)
- Board: **ESP32-S3 Dev Module**
- CDC On Boot: `cdc`
- CPU Frequency: `240MHz`
- Partition Scheme: `fatflash`
- PSRAM: `opi`
- USB Mode: `hwcdc`
- Upload Speed: `921600`

#### AIPI-Lite
- Same board settings as Freenove
- **CRITICAL:** GPIO 10 must be HIGH on boot (handled by sketch)

#### Waveshare AMOLED 1.8
- Board: **ESP32-S3 Dev Module**
- CDC On Boot: `cdc`
- CPU Frequency: `240MHz`
- Flash Size: `16M`
- Partition Scheme: `app3M_fat9M_16MB` (required — 3MB app space)
- PSRAM: `opi`
- USB Mode: `hwcdc`
- Upload Speed: `921600`

### 4. Flash

Connect the ESP32 via USB and click **Upload**.

### arduino-cli build commands

**Freenove (default):**
```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3 client_esp32.ino
```

**AIPI-Lite:**
```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3 \
  --build-property "compiler.c.extra_flags=-DBOARD_AIPI_LITE" \
  --build-property "compiler.cpp.extra_flags=-DBOARD_AIPI_LITE" \
  client_esp32.ino
```

**Waveshare AMOLED:**
```sh
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=115200" \
  --build-property "compiler.c.extra_flags=-DBOARD_WAVESHARE_AMOLED" \
  --build-property "compiler.cpp.extra_flags=-DBOARD_WAVESHARE_AMOLED" \
  client_esp32.ino
```

**Upload (all boards):**
```sh
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 client_esp32.ino
```

Replace `/dev/ttyACM0` with the correct port. Find it with `arduino-cli board list`.

## Features

- **Push-to-Talk / Hard Stop**: Press the button to start listening. If pressed again while listening or while response audio is playing, the client immediately stops all activity, returns to boot state (`Press button to start a conversation.`), and waits for a new start press.
- **Streaming Audio**: Records at 32kHz (downsampled to 16kHz) and streams raw PCM directly to the server.
- **Binary Playback**: Receives raw binary audio frames from the server for high-performance, low-latency playback.
- **Software Volume Scaling**: Integrated volume control (0-21) with software-based sample scaling for DACs that lack hardware volume registers.
- **Real-time Display**: Shows transcriptions as they happen and updates status messages based on server events (`transcribing`, `done`, `error`).
- **Idle-State Guarding**: After a hard stop, stale in-flight backend conversation messages and audio payloads are ignored until the next explicit start.
- **Touch Input (Waveshare)**: FT3168 capacitive touch panel provides multi-touch input on the Waveshare AMOLED board.
- **SD Card (Waveshare)**: SDMMC 1-bit support for config storage, logs, or TTS model caching.
- **Battery Monitoring (Waveshare/AIPI)**: AXP2101 PMU (Waveshare) or GPIO ADC (AIPI-Lite) for battery level reading.

## Technical Implementation

- **Multitasking**: Uses FreeRTOS tasks to separate audio recording (`loop_task_sound_recorder`), audio playback (`i2s_output_wav`), and UI rendering.
- **Optimized JSON**: Employs a zero-copy-adjacent manual JSON parser to minimize heap fragmentation on embedded hardware.
- **Thread Safety**: Mutexes protect access to shared resources like the WebSocket client and display buffers.

## Configuration Reference

### ESP32 `sketch_config.h`

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

**Freenove / AIPI-Lite:**
```json
{
    "sketch": "client_esp32.ino",
    "board": "esp32:esp32:esp32s3",
    "configuration": "FlashSize=16M,PartitionScheme=fatflash,PSRAM=opi,CDCOnBoot=cdc,USBMode=tinyusb,UploadSpeed=115200",
    "output": "build",
    "port": "/dev/ttyACM0"
}
```

**Waveshare AMOLED:**
```json
{
    "sketch": "client_esp32.ino",
    "board": "esp32:esp32:esp32s3",
    "configuration": "FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc,USBMode=tinyusb,UploadSpeed=115200",
    "output": "build",
    "port": "/dev/ttyACM0"
}
```
