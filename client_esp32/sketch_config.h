#ifndef SKETCH_CONFIG_H
#define SKETCH_CONFIG_H

// Board selection (uncomment ONE):
//   BOARD_AIPI_LITE       - AIPI Lite (ST7735 SPI, ES8311 codec)
//   BOARD_WAVESHARE_AMOLED - Waveshare ESP32-S3-Touch-AMOLED-1.8 (SH8601 QSPI AMOLED)
// Default: Freenove ESP32-S3 Media Kit (ST7796 parallel TFT)

// #define BOARD_AIPI_LITE
#define BOARD_WAVESHARE_AMOLED

#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 1
#endif

#ifndef ARDUINO_USB_MODE
#define ARDUINO_USB_MODE 1
#endif

#ifndef ARDUINO_USB_MSC_ON_BOOT
#define ARDUINO_USB_MSC_ON_BOOT 0
#endif

#ifndef ARDUINO_USB_DFU_ON_BOOT
#define ARDUINO_USB_DFU_ON_BOOT 0
#endif

#endif
