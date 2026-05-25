# AIPI-Lite Voice Assistant Client (ESP32-S3)

A hardware client for the Voice AI Pipeline, designed for the AIPI-Lite ESP32-S3 board with integrated I2S audio codec (ES8311), microphone, speaker, and display.

## Hardware

**AIPI-Lite ESP32-S3**:
- **Microphone**: Integrated, analog input to ES8311 codec
- **Audio Codec**: ES8311 (I2C @ 0x18, I2S audio transfer)
- **Speaker**: Integrated with GPIO9 amplifier enable
- **Display**: ST7735 SPI display (128×128 pixels)
- **Button**: GPIO42 (Right button, active-LOW)
- **Power Management**: GPIO10 keep-alive (CRITICAL on boot), GPIO2 battery monitoring

## Pin Configuration (AIPI-Lite)

| Component | GPIO | Notes |
|-----------|------|-------|
| **Button (Right)** | 42 | Digital INPUT_PULLUP, active-LOW |
| **I2C (ES8311 codec)** | SDA=5, SCL=4 | I2C Address: 0x18 |
| **I2S Audio** | | |
| - Master Clock (MCLK) | 6 | Shared for input and output |
| - Bit Clock (BCLK) | 14 | Shared for input and output |
| - Frame Clock (LRCLK) | 12 | Shared for input and output |
| - Data In (from codec) | 13 | I2S input from ES8311 |
| - Data Out (to codec) | 11 | I2S output to ES8311 |
| **Display (ST7735 SPI)** | | |
| - Clock (SCK) | 16 | SPI clock |
| - Data (MOSI) | 17 | SPI data |
| - Chip Select (CS) | 15 | Display chip select |
| - Data/Command (DC) | 7 | Command/data selector |
| - Reset (RST) | 18 | Display reset |
| - Backlight (BL) | 3 | PWM backlight control |
| **Power Management** | | |
| - Keep-Alive | 10 | HIGH on boot to stay powered |
| - Battery ADC | 2 | Battery voltage monitoring |
| **Speaker Amp Enable** | 9 | HIGH to enable, LOW to disable |

*These pins are defined in `client_aipi.ino` and configured via `User_Setup.h` (TFT_eSPI) and driver files.*

## Setup & Flash

1.  **Libraries**: Install the following libraries in Arduino IDE:
    *   `ArduinoWebsockets` by Gil Maimon
    *   `LVGL` (for display)
    *   `TFT_eSPI` (for ST7735 display)
2.  **Configuration**: Open `client_aipi.ino` and edit:
    *   `WIFI_SSID`: Your Wi-Fi network name.
    *   `WIFI_PASS`: Your Wi-Fi password.
    *   `SERVER_IP`: The IP address of your Python server (e.g., `192.168.0.108`).
3.  **Board Config**: Use the following FQBN:
    ```
    esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=fatflash,PSRAM=opi
    ```
4.  **Flash**: Select your AIPI-Lite board and upload the sketch.

## Features

- **Push-to-Talk**: Press GPIO42 to start listening. Press again to stop and trigger transcription.
- **Streaming Audio**: Records audio via ES8311 codec at 16kHz and streams PCM to the server.
- **Binary Playback**: Receives base64-encoded audio from the server and plays back via ES8311 DAC + speaker amp.
- **Software Volume Scaling**: Integrated volume control (0-21) with software-based sample scaling.
- **Real-time Display**: ST7735 display shows transcriptions and status updates in real-time.
- **Power Management**: GPIO10 keep-alive maintains power on battery; GPIO2 monitors battery voltage.
- **Speaker Amplifier Control**: GPIO9 enables/disables the speaker amp to save power during idle periods.

## Technical Details

- **Multitasking**: Uses FreeRTOS tasks for audio recording, playback, and UI rendering.
- **Thread Safety**: Mutexes protect WebSocket and display resources.
- **ES8311 Codec**: Analog microphone → codec → I2S data stream to/from ESP32.
- **PSRAM**: 4MB external RAM for audio buffering and large operations.
