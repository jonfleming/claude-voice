// User_Setup.h - TFT_eSPI configuration for the display smoke test.
// Arduino adds the sketch folder to the include path first, so this keeps the
// smoke test independent from the globally installed TFT_eSPI setup.
// #define BOARD_AIPI_LITE

#ifndef USER_SETUP_H
#define USER_SETUP_H

#include "sketch_config.h"

#define USER_SETUP_LOADED

#ifdef BOARD_AIPI_LITE

// --- AIPI Lite: ST7735 128x128 ---
#define AIPI_LITE_128x128_ST7735
#define ST7735_DRIVER
#define ST7735_GREENTAB128

#define TFT_WIDTH  128
#define TFT_HEIGHT 128

#define TFT_MISO   -1
#define TFT_MOSI   17
#define TFT_SCLK   16
#define TFT_CS     15
#define TFT_DC      7
#define TFT_RST    18
#define TFT_BL      3
#define TOUCH_CS   -1

#define USE_HSPI_PORT
#define SPI_FREQUENCY  27000000

#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERT_1  1

#define TOUCH_CS   -1

#else

// --- Freenove ESP32-S3 Media Kit: ST7796 320x480 ---
// Include the Freenove-specific TFT_eSPI setup file for pin definitions,
// driver selection, and display configuration.
#include <User_Setups/Setup_Freenove_FNK0102B.h>

#endif  // BOARD_AIPI_LITE

#endif  // USER_SETUP_H
