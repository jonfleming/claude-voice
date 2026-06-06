// Audio driver bridge for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses ES8311 I2S audio codec via I2C configuration + ESP_I2S library
//
// ES8311 I2C address: 0x18 (ADDR pin pulled high on Waveshare board)
// MCLK source: ESP32-S3 I2S MCK pin (GPIO16)
// I2S: BCLK=GPIO9, WS=GPIO45, DOUT=GPIO10, MCK=GPIO16
// PA enable: GPIO46 (active HIGH)
//
// The ES8311 uses an internal PLL to generate MCLK from the I2S bit clock.
// For 16kHz/16-bit stereo: BCLK = 16kHz * 16 * 2 = 512kHz, MCLK = 16kHz * 256 = 4.096MHz
// The ESP32-S3 I2S peripheral generates MCK at 4.096MHz for this configuration.

#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>

#ifndef AUDIO_DEBUG_SERIAL
#define AUDIO_DEBUG_SERIAL Serial
#endif

// =============================================================================
// ES8311 I2C address (ADDR pin = HIGH on Waveshare board)
// =============================================================================
#define ES8311_I2C_ADDR_WAVESHARE  0x18

// =============================================================================
// ES8311 register map (from ES8311 DS Rev 10.0)
// =============================================================================
#define ES8311_SW_CTRL            0x00  // Software control
#define ES8311_CHIP_POWER_CTRL    0x01  // Chip power control
#define ES8311_CHIP_POWER_CTRL2   0x02  // Chip power control 2
#define ES8311_CLOCK_MANUAL       0x05  // Clock manual enable
#define ES8311_CLK_DIV1           0x06  // Clock divider 1
#define ES8311_CLK_DIV2           0x07  // Clock divider 2
#define ES8311_I2S_CTRL           0x0A  // I2S control
#define ES8311_DAC_VOL            0x2A  // DAC volume
#define ES8311_DAC_VOL_CTRL       0x2B  // DAC volume control
#define ES8311_ADC_DAC_MUTE       0x0E  // ADC/DAC mute control
#define ES8311_INT_POWER_CTRL1    0x03  // Internal power control 1
#define ES8311_INT_POWER_CTRL2    0x04  // Internal power control 2
#define ES8311_ANA_PLAY_CTRL      0x3E  // Analog play control
#define ES8311_LDO_CTRL           0x40  // LDO control
#define ES8311_AMP_CTRL           0x92  // AMP control
#define ES8311_CHIP_CLK_CTRL      0x30  // Chip clock control
#define ES8311_CHIP_RESET         0xFD  // Chip reset / ID read

// =============================================================================
// I2C helper functions
// =============================================================================

static bool es8311_write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ES8311_I2C_ADDR_WAVESHARE);
    Wire.write(reg);
    Wire.write(value);
    const uint8_t rc = Wire.endTransmission();
    if (rc != 0) {
        AUDIO_DEBUG_SERIAL.printf("[audio] ES8311 write 0x%02x = 0x%02x failed: %u\r\n", reg, value, rc);
        return false;
    }
    return true;
}

static bool es8311_read(uint8_t reg, uint8_t* value) {
    Wire.beginTransmission(ES8311_I2C_ADDR_WAVESHARE);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(ES8311_I2C_ADDR_WAVESHARE, (uint8_t)1) != 1) {
        return false;
    }
    *value = Wire.read();
    return true;
}

// =============================================================================
// ES8311 initialization sequence
// Based on the working AIPI-Lite implementation + ES8311 DS Rev 10.0
// =============================================================================

static bool audio_codec_init_es8311() {
    AUDIO_DEBUG_SERIAL.println("[audio] Initializing ES8311 codec...");

    // Check if ES8311 is on the I2C bus
    uint8_t chip_id = 0;
    if (!es8311_read(ES8311_CHIP_RESET, &chip_id)) {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 not found on I2C bus!");
        return false;
    }
    AUDIO_DEBUG_SERIAL.printf("[audio] ES8311 chip ID: 0x%02X\r\n", chip_id);

    bool ok = true;

    // --- Step 1: Software reset (per ES8311 DS) ---
    ok &= es8311_write(ES8311_SW_CTRL, 0x1F);  // Reset all registers to default
    delay(10);
    ok &= es8311_write(ES8311_SW_CTRL, 0x00);  // Exit reset
    delay(10);

    // --- Step 2: Power management ---
    // Chip power control: enable all power domains
    ok &= es8311_write(ES8311_CHIP_POWER_CTRL,  0x00);
    ok &= es8311_write(ES8311_CHIP_POWER_CTRL2, 0x00);
    ok &= es8311_write(ES8311_INT_POWER_CTRL1,  0x00);
    ok &= es8311_write(ES8311_INT_POWER_CTRL2,  0x00);

    // --- Step 3: Clock configuration ---
    // MCLK source from I2S MCK pin (GPIO16), not external crystal
    // For 16kHz/16-bit: MCLK = 16000 * 256 = 4.096MHz (provided by ESP32-S3 I2S)
    ok &= es8311_write(ES8311_CLOCK_MANUAL, 0x00);  // Auto clock mode
    ok &= es8311_write(ES8311_CLK_DIV1, 0x00);      // MCLK / 1 = 4.096MHz
    ok &= es8311_write(ES8311_CLK_DIV2, 0x01);      // BCLK / 2 = 2.048MHz

    // --- Step 4: DAC configuration ---
    ok &= es8311_write(ES8311_DAC_VOL, 0x00);       // Max volume (0dB)
    ok &= es8311_write(ES8311_DAC_VOL_CTRL, 0x00);  // DAC volume control
    ok &= es8311_write(ES8311_ADC_DAC_MUTE, 0x00);  // Unmute DAC

    // --- Step 5: I2S format ---
    // 0x02 = I2S format, 16-bit word length, slave mode
    ok &= es8311_write(ES8311_I2S_CTRL, 0x02);

    // --- Step 6: Analog output ---
    // Enable DAC output path
    ok &= es8311_write(ES8311_ANA_PLAY_CTRL, 0x08);  // Enable DAC output

    // --- Step 7: LDO and AMP ---
    ok &= es8311_write(ES8311_LDO_CTRL, 0x0F);       // Enable all LDOs
    ok &= es8311_write(ES8311_AMP_CTRL, 0x00);       // Enable amplifier

    // --- Step 8: Final power-on ---
    // Power up the chip (bit 0 of 0x01)
    ok &= es8311_write(ES8311_CHIP_POWER_CTRL, 0x01);

    if (ok) {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 codec initialized successfully.");
    } else {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 init completed with errors (check I2C above).");
    }
    return ok;
}

// =============================================================================
// I2S output initialization
// =============================================================================

bool i2s_output_init_waveshare() {
    AUDIO_DEBUG_SERIAL.println("[audio] Initializing I2S output...");

    // Initialize ES8311 codec first
    if (!audio_codec_init_es8311()) {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 init failed!");
        return false;
    }

    // Enable power amplifier (GPIO46, active HIGH)
    pinMode(AUDIO_PA_PIN, OUTPUT);
    digitalWrite(AUDIO_PA_PIN, HIGH);
    AUDIO_DEBUG_SERIAL.printf("[audio] PA enabled on GPIO%d\r\n", AUDIO_PA_PIN);

    // Initialize I2S output using ESP_I2S library
    // Pins: BCLK=GPIO9, WS=GPIO45, DOUT=GPIO10, MCK=GPIO16
    I2SClass i2s;
    i2s.setPins(AUDIO_I2S_BCK, AUDIO_I2S_WS, AUDIO_I2S_DO, -1, AUDIO_I2S_MCK);

    // 16kHz, 16-bit, stereo, I2S standard mode
    if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_LEFT)) {
        AUDIO_DEBUG_SERIAL.println("[audio] I2S begin failed!");
        return false;
    }

    AUDIO_DEBUG_SERIAL.println("[audio] I2S output initialized at 16kHz stereo 16-bit.");
    return true;
}

// Stub for compatibility with the original interface
bool audio_output_codec_init(void) {
    return i2s_output_init_waveshare();
}
