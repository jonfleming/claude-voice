// User_Setup.h - TFT_eSPI configuration for the AIPI Lite display test.
// Arduino adds the sketch folder to the include path first, so this keeps the
// smoke test independent from the globally installed TFT_eSPI setup.

#ifndef USER_SETUP_H
#define USER_SETUP_H

#define USER_SETUP_LOADED
#define BOARD_AIPI_LITE

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

#define USE_HSPI_PORT
#define SPI_FREQUENCY  27000000

#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERT_1  1

#define TOUCH_CS   -1

#endif  // USER_SETUP_H
