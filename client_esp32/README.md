# ESP32 Voice Assistant Client

A standalone hardware client for the Voice AI Pipeline, designed for ESP32 development boards with I2S microphone and DAC/Speaker support.

## Hardware Requirements

- **ESP32 Core**: ESP32-S3 or standard ESP32.
- **Microphone**: I2S Digital Microphone (e.g., INMP441, MSM261S4030H0).
- **Speaker/DAC**: I2S DAC (e.g., MAX98357A, PCM5102) and a speaker.
- **Display**: SPI or I2C display supported by LVGL (optional, configured via `display.cpp`).
- **Button**: Physical button for push-to-talk (default: GPIO 19).

## Pin Configuration (Default)

| Component | Pin (GPIO) |
|-----------|------------|
| Button | 19 |
| Audio Input SCK | 3 |
| Audio Input WS | 14 |
| Audio Input DIN | 46 |
| Audio Output BCLK | 42 |
| Audio Output LRC | 41 |
| Audio Output DOUT | 1 |

*Note: These are defined in `client_esp32.ino` and should be adjusted to match your specific wiring.*

## Setup & Flash

1.  **Libraries**: Install the following libraries in Arduino IDE:
    *   `ArduinoWebsockets` by Gil Maimon
    *   `LVGL` (if using display)
2.  **Configuration**: Open `client_esp32.ino` and edit:
    *   `WIFI_SSID`: Your Wi-Fi network name.
    *   `WIFI_PASS`: Your Wi-Fi password.
    *   `SERVER_IP`: The IP address of your Python server (e.g., `10.0.0.51`).
3.  **Flash**: Select your ESP32 board and upload the sketch.

## Features

- **Push-to-Talk**: Press the button to start listening. Press again to stop and trigger transcription.
- **Streaming Audio**: Records at 32kHz (downsampled to 16kHz) and streams raw PCM directly to the server.
- **Binary Playback**: Receives raw binary audio frames from the server for high-performance, low-latency playback.
- **Software Volume Scaling**: Integrated volume control (0-21) with software-based sample scaling for DACs that lack hardware volume registers.
- **Real-time Display**: Shows transcriptions as they happen and updates status messages based on server events (`transcribing`, `done`, `error`).

## Technical Implementation

- **Multitasking**: Uses FreeRTOS tasks to separate audio recording (`loop_task_sound_recorder`), audio playback (`i2s_output_wav`), and UI rendering.
- **Optimized JSON**: Employs a zero-copy-adjacent manual JSON parser to minimize heap fragmentation on embedded hardware.
- **Thread Safety**: Mutexes protect access to shared resources like the WebSocket client and display buffers.
