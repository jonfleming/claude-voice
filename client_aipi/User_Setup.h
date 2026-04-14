// User_Setup.h - AIPI-Lite ST7735 Display Configuration
// This file configures TFT_eSPI library for the AIPI-Lite's ST7735 128x128 display

#ifndef USER_SETUP_H
#define USER_SETUP_H

// ========== DISPLAY DRIVER ==========
#define ST7735_DRIVER      // ST7735 display controller
#define TFT_WIDTH  128
#define TFT_HEIGHT 128

// ========== SPI INTERFACE ==========
#define TFT_MISO   -1      // Not used (display is write-only)
#define TFT_MOSI   17      // GPIO17 - SPI Data (MOSI)
#define TFT_SCLK   16      // GPIO16 - SPI Clock (SCK)
#define TFT_CS     15      // GPIO15 - Chip Select
#define TFT_DC     7       // GPIO7  - Data/Command pin
#define TFT_RST    18      // GPIO18 - Reset pin
#define TFT_BL     3       // GPIO3  - Backlight PWM

// ========== SPI FREQUENCY ==========
#define SPI_FREQUENCY  27000000  // 27 MHz SPI clock

// ========== ROTATION & DISPLAY ==========
#define TFT_ROTATION  1  // 90-degree rotation for normal viewing

// ========== COLOR INVERSION ==========
#define TFT_INVERT_1  1  // Invert colors (matches AIPI-Lite display)

// ========== TOUCH SUPPORT (NOT USED) ==========
#define TOUCH_CS   -1    // No touch support on AIPI-Lite

#endif // USER_SETUP_H
