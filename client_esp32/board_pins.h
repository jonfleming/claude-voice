<<<<<<< HEAD
#ifndef __BOARD_PINS_H
#define __BOARD_PINS_H

// ============================================================
// Board Pin Profile Selector
// ============================================================
// Default build targets **Freenove** (FNK0102A/B).
// To build for **AIPI Lite**: define `BOARD_AIPI_LITE`
// (add -DBOARD_AIPI_LITE in build flags, or uncomment below).
// ============================================================

// #define BOARD_AIPI_LITE  // <-- Uncomment to target AIPI Lite

#ifdef BOARD_AIPI_LITE

// ----------------------------------------------------------
// AIPI-Lite board pin definitions (from AIPI-Lite-GPIO-Pins.md)
// ----------------------------------------------------------

// POWER MANAGEMENT
#define POWER_KEEP_ALIVE_PIN    10  // CRITICAL: Must be HIGH on boot to stay powered on battery
#define AIPI_POWER_KEEPALIVE_PIN POWER_KEEP_ALIVE_PIN
#define BATTERY_ADC_PIN          2  // ADC input, use 11db attenuation, multiply by 2.0

// DISPLAY (ST7735 - SPI)
#define DISPLAY_SPI_SCK         16  // SPI Clock
#define DISPLAY_SPI_MOSI        17  // SPI Data (MOSI)
#define DISPLAY_SPI_CS          15  // Chip Select
#define DISPLAY_SPI_DC           7  // Data/Command
#define DISPLAY_SPI_RST         18  // Reset
#define DISPLAY_SPI_BL           3  // Backlight PWM (strapping pin - works but shows warning)
#define TFT_BL                   DISPLAY_SPI_BL

// AUDIO CODEC (ES8311 - I2C)
#define ES8311_I2C_SDA          5  // I2C Data
#define ES8311_I2C_SCL          4  // I2C Clock
#define ES8311_I2C_ADDR       0x18 // ES8311 I2C Address

// AUDIO (I2S)
#define AUDIO_INPUT_MCLK         6  // I2S Master Clock
#define AUDIO_INPUT_BCLK        14  // I2S Bit Clock
#define AUDIO_INPUT_LRCLK       12  // I2S Word Select (LRCLK)
#define AUDIO_INPUT_DIN         13  // I2S Data In from ES8311 codec

#define AUDIO_OUTPUT_MCLK        6  // I2S Master Clock
#define AUDIO_OUTPUT_BCLK       14  // I2S Bit Clock
#define AUDIO_OUTPUT_LRCLK      12  // I2S Word Select (LRCLK)
#define AUDIO_OUTPUT_DOUT       11  // I2S Data Out to speaker

#define SPEAKER_AMP_ENABLE       9  // Enable speaker amp before playback

// USER INPUTS
#define BUTTON_PIN_LEFT          1  // Left button (also hardware power button)
#define BUTTON_PIN_RIGHT        42  // Right button (standard GPIO)
#define BUTTON_PIN               BUTTON_PIN_LEFT

#else

// ----------------------------------------------------------
// Freenove (FNK0102A / FNK0102B) board pin definitions
// ----------------------------------------------------------

#define TFT_BL                  20  // TFT backlight
#define BUTTON_PIN             19  // Button (analog ADC pin)

// Audio input (I2S to external mic)
#define AUDIO_INPUT_SCK          3  // Bit Clock
#define AUDIO_INPUT_WS          14  // Word Select (LRCLK)
#define AUDIO_INPUT_DIN         46  // Data In

// Audio output (I2S to external speaker amp)
#define AUDIO_OUTPUT_BCLK       42  // Bit Clock
#define AUDIO_OUTPUT_LRC        41  // Frame Clock (LRCLK)
#define AUDIO_OUTPUT_DOUT        1  // Data Out

#endif // BOARD_AIPI_LITE

#endif // __BOARD_PINS_H
=======
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

// =============================================================================
// Board Pin Profile
// =============================================================================
// Default build targets Freenove ESP32-S3 Media Kit.
// To build for AIPI Lite: define BOARD_AIPI_LITE
//   - Add -DBOARD_AIPI_LITE in build flags, or
//   - Uncomment the line below for quick toggling in the IDE:
#define BOARD_AIPI_LITE
// =============================================================================

#ifdef BOARD_AIPI_LITE

// --- AIPI Lite pin profile ---

// Power management
#define AIPI_POWER_KEEPALIVE_PIN  10  // CRITICAL: set HIGH on boot to stay powered on battery
#define AIPI_SPEAKER_AMP_EN_PIN    9  // Set HIGH before playing audio

// Display (ST7735 SPI)
#define BUTTON_PIN                 1  // Left button (also hardware power button)
#define TFT_BL                     3  // Backlight PWM (strapping pin - works with warning)

// Audio output (I2S via ES8311 codec)
#define AUDIO_OUTPUT_BCLK         14
#define AUDIO_OUTPUT_LRC          12
#define AUDIO_OUTPUT_DOUT         11

// Audio input (I2S via ES8311 codec - shared I2S bus)
#define AUDIO_INPUT_SCK           14
#define AUDIO_INPUT_WS            12
#define AUDIO_INPUT_DIN           -1  // Codec provides data; update if direct I2S DIN pin found

#else

// --- Freenove ESP32-S3 Media Kit pin profile (default) ---

// Button
#define BUTTON_PIN                19

// Display
#define TFT_BL                    20

// Audio output (I2S)
#define AUDIO_OUTPUT_BCLK         42
#define AUDIO_OUTPUT_LRC          41
#define AUDIO_OUTPUT_DOUT          1

// Audio input (I2S)
#define AUDIO_INPUT_SCK            3
#define AUDIO_INPUT_WS            14
#define AUDIO_INPUT_DIN           46

#endif  // BOARD_AIPI_LITE

#endif  // BOARD_PINS_H
>>>>>>> 05405f9 (testing)
