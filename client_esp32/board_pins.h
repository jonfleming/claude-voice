#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "sketch_config.h"

// =============================================================================
// Board Pin Profile
// =============================================================================
// Default build targets Freenove ESP32-S3 Media Kit.
// To build for AIPI Lite: define BOARD_AIPI_LITE
//   - Add -DBOARD_AIPI_LITE in build flags, or
//   - set it in sketch_config.h for Arduino IDE builds.
// =============================================================================

#ifdef BOARD_AIPI_LITE

// --- AIPI Lite pin profile ---

// Power management
#define POWER_KEEP_ALIVE_PIN      10
#define AIPI_POWER_KEEPALIVE_PIN  POWER_KEEP_ALIVE_PIN
#define BATTERY_ADC_PIN            2
#define SPEAKER_AMP_ENABLE         9
#define AIPI_SPEAKER_AMP_EN_PIN    SPEAKER_AMP_ENABLE

// Display (ST7735 SPI)
#define DISPLAY_SPI_SCK           16
#define DISPLAY_SPI_MOSI          17
#define DISPLAY_SPI_CS            15
#define DISPLAY_SPI_DC             7
#define DISPLAY_SPI_RST           18
#define DISPLAY_SPI_BL             3

// User inputs
#define BUTTON_PIN_LEFT            1
#define BUTTON_PIN_RIGHT          42
#define BUTTON_PIN                 BUTTON_PIN_LEFT

// Audio codec (ES8311)
#define ES8311_I2C_SDA             5
#define ES8311_I2C_SCL             4
#define ES8311_I2C_ADDR         0x18

// Audio I2S
#define AUDIO_INPUT_MCLK           6
#define AUDIO_INPUT_BCLK          14
#define AUDIO_INPUT_SCK           AUDIO_INPUT_BCLK
#define AUDIO_INPUT_LRCLK         12
#define AUDIO_INPUT_WS            AUDIO_INPUT_LRCLK
#define AUDIO_INPUT_DIN           13

#define AUDIO_OUTPUT_MCLK          6
#define AUDIO_OUTPUT_BCLK         14
#define AUDIO_OUTPUT_LRCLK        12
#define AUDIO_OUTPUT_LRC          AUDIO_OUTPUT_LRCLK
#define AUDIO_OUTPUT_DOUT         11

#define TOUCH_CS                  -1

// SD Card (SDMMC) — AIPI Lite has no SDMMC
#define SDMMC_CLK                -1
#define SDMMC_CMD                -1
#define SDMMC_DATA               -1

// Touch I2C — AIPI Lite has no I2C touch
#define TOUCH_I2C_SDA            -1
#define TOUCH_I2C_SCL            -1

#elif defined(BOARD_WAVESHARE_AMOLED)

// --- Waveshare ESP32-S3-Touch-AMOLED-1.8 pin profile ---

// Display (SH8601 QSPI AMOLED, 368x448)
#define LCD_SDIO0          4
#define LCD_SDIO1          5
#define LCD_SDIO2          6
#define LCD_SDIO3          7
#define LCD_SCLK          11
#define LCD_CS            12
#define LCD_WIDTH         368
#define LCD_HEIGHT        448

// Touch (FT3168 via I2C)
#define TOUCH_I2C_SDA     15
#define TOUCH_I2C_SCL     14
#define TOUCH_INT         21

// Audio (ES8311 codec)
#define ES8311_I2C_ADDR         0x18
#define AUDIO_I2S_MCK     16
#define AUDIO_I2S_BCK      9
#define AUDIO_I2S_DI       8  // MIC input
#define AUDIO_I2S_WS      45
#define AUDIO_I2S_DO      10  // SPK output
#define AUDIO_PA_PIN      46

// SD Card (SDMMC)
#define SDMMC_CLK          2
#define SDMMC_CMD          1
#define SDMMC_DATA         3

// Power management (AXP2101 via I2C)
#define AXP2101_I2C_ADDR  0x34

// Audio output aliases (for compatibility with common pipeline code)
#define AUDIO_OUTPUT_BCLK  AUDIO_I2S_BCK
#define AUDIO_OUTPUT_LRC   AUDIO_I2S_WS
#define AUDIO_OUTPUT_DOUT  AUDIO_I2S_DO

// Button (GPIO0 = boot button on Waveshare AMOLED 1.8)
#define BUTTON_PIN  0

#else

// --- Freenove ESP32-S3 Media Kit pin profile ---

// Display (ST7796 320x480 parallel TFT via TFT_eSPI)
#define DISPLAY_SPI_BL             2
#define DISPLAY_SPI_RST           20
#define TFT_WIDTH                 320
#define TFT_HEIGHT                480
#define TFT_BL                    DISPLAY_SPI_BL
#define TFT_BACKLIGHT_ON          HIGH
#define TFT_INVERSION_ON

// User inputs
#define BUTTON_PIN                19

// Touch (I2C) — Freenove uses FT6236 or similar capacitive touch
#define TOUCH_I2C_SDA            8
#define TOUCH_I2C_SCL            9
#define TOUCH_INT                -1
#define TOUCH_CS                 -1

// SD Card (SDMMC 1-bit)
#define SDMMC_CLK                40
#define SDMMC_CMD                39
#define SDMMC_DATA               41

// Audio input (external I2S MEMS microphone)
#define AUDIO_INPUT_SCK            3
#define AUDIO_INPUT_WS            14
#define AUDIO_INPUT_DIN           46

// Audio output (I2S)
#define AUDIO_OUTPUT_BCLK         42
#define AUDIO_OUTPUT_LRC          45
#define AUDIO_OUTPUT_DOUT          1

#endif  // BOARD_WAVESHARE_AMOLED / BOARD_AIPI_LITE

#endif  // BOARD_PINS_H
