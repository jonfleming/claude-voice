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

#else

// --- Freenove ESP32-S3 Media Kit pin profile ---

#define DISPLAY_SPI_BL             2
#define DISPLAY_SPI_RST           20
#define BUTTON_PIN                19

// Audio input (I2S)
#define AUDIO_INPUT_SCK            3
#define AUDIO_INPUT_WS            14
#define AUDIO_INPUT_DIN           46

// Audio output (I2S)
#define AUDIO_OUTPUT_BCLK         42
#define AUDIO_OUTPUT_LRC          41
#define AUDIO_OUTPUT_DOUT          1

#endif  // BOARD_AIPI_LITE

#endif  // BOARD_PINS_H

#define TOUCH_CS                  -1