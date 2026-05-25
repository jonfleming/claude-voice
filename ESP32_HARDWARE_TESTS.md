# ESP32 hardware test sketches

These sketches are incremental Freenove ESP32-S3 Media Kit smoke tests for
porting the voice client to another board, such as a Waveshare 1.8" AMOLED
device.

## Test sequence

1. `client_esp32_test_01_display` verifies TFT/LVGL display writes.
2. `client_esp32_test_02_button` adds the Freenove button input path.
3. `client_esp32_test_03_speaker_wav` adds I2S speaker/DAC WAV playback.
4. `client_esp32_test_04_microphone_record` adds I2S microphone capture and
   local playback of the captured WAV.
5. `client_esp32_test_05_websocket_ping` adds WiFi and WebSocket ping.
6. `client_esp32` is the existing full microphone-to-backend-to-speaker client.

Each test folder contains one uploadable `.ino` sketch. The tests intentionally
reuse the working Freenove drivers from `client_esp32` by relative include so
hardware behavior stays aligned with the full client.

## Freenove pin map used by the tests

| Component | Pin |
| --- | --- |
| Button ADC | GPIO 19 |
| Audio input SCK/BCLK | GPIO 3 |
| Audio input WS/LRCLK | GPIO 14 |
| Audio input DIN | GPIO 46 |
| Audio output BCLK | GPIO 42 |
| Audio output LRC/LRCLK | GPIO 41 |
| Audio output DOUT | GPIO 1 |

The display pin mapping comes from the existing TFT_eSPI/Freenove setup used
by `client_esp32/display.cpp`.

## Build notes

Use the same board and libraries as the existing client:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=fatflash,PSRAM=opi client_esp32_test_01_display
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=fatflash,PSRAM=opi client_esp32_test_01_display
```

Change `COM3` to the port shown by `arduino-cli board list`.

For `client_esp32_test_05_websocket_ping`, edit `WIFI_SSID`, `WIFI_PASS`,
and `SERVER_IP` before uploading.
