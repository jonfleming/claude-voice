// Audio driver bridge for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses ES8311 I2S audio codec directly via I2SClass

#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>

#ifndef AUDIO_DEBUG_SERIAL
#define AUDIO_DEBUG_SERIAL Serial
#endif

// ES8311 I2C address
#define ES8311_I2C_ADDR_WAVESHARE  0x18

// ES8311 register definitions
#define ES8311_SW_CTRL            0x00
#define ES8311_CHIP_POWER_CTRL    0x01
#define ES8311_CHIP_POWER_CTRL2   0x02
#define ES8311_CLOCK_MANUAL       0x05
#define ES8311_CLK_DIV1           0x06
#define ES8311_CLK_DIV2           0x07
#define ES8311_I2S_CTRL           0x0A
#define ES8311_DAC_VOL            0x2A
#define ES8311_DAC_VOL_CTRL       0x2B
#define ES8311_ADC_DAC_MUTE       0x0E
#define ES8311_INT_POWER_CTRL1    0x03
#define ES8311_INT_POWER_CTRL2    0x04
#define ES8311_ANA_PLAY_CTRL      0x3F
#define ES8311_LDO_CTRL           0x3F
#define ES8311_AMP_CTRL           0x92
#define ES8311_CHIP_CLK_CTRL      0x30

static bool es8311_write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ES8311_I2C_ADDR_WAVESHARE);
    Wire.write(reg);
    Wire.write(value);
    const uint8_t rc = Wire.endTransmission();
    if (rc != 0) {
        AUDIO_DEBUG_SERIAL.printf("[audio] ES8311 write 0x%02x failed: %u\r\n", reg, rc);
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

// Initialize ES8311 codec via I2C
static bool audio_codec_init_es8311() {
    AUDIO_DEBUG_SERIAL.println("[audio] Initializing ES8311 codec...");

    // Check if ES8311 is on the I2C bus
    uint8_t chip_id = 0;
    if (!es8311_read(0xFD, &chip_id)) {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 not found on I2C!");
        return false;
    }
    AUDIO_DEBUG_SERIAL.printf("[audio] ES8311 chip ID: 0x%02X\r\n", chip_id);

    // ES8311 initialization sequence
    bool ok = true;

    // Software reset
    ok &= es8311_write(ES8311_SW_CTRL, 0x01);
    ok &= es8311_write(ES8311_SW_CTRL, 0x00);

    // Power management
    ok &= es8311_write(ES8311_CHIP_POWER_CTRL,  0x00);
    ok &= es8311_write(ES8311_CHIP_POWER_CTRL2, 0x00);
    ok &= es8311_write(ES8311_INT_POWER_CTRL1,  0x00);
    ok &= es8311_write(ES8311_INT_POWER_CTRL2,  0x00);

    // Clock configuration
    ok &= es8311_write(ES8311_CLOCK_MANUAL, 0x01);
    ok &= es8311_write(ES8311_CLK_DIV1, 0x80);
    ok &= es8311_write(ES8311_CLK_DIV2, 0x01);

    // DAC configuration
    ok &= es8311_write(ES8311_DAC_VOL, 0x00);
    ok &= es8311_write(ES8311_DAC_VOL_CTRL, 0x00);
    ok &= es8311_write(ES8311_ADC_DAC_MUTE, 0x00);

    // I2S format: standard I2S, 16-bit
    ok &= es8311_write(ES8311_I2S_CTRL, 0x02);

    // Enable DAC
    ok &= es8311_write(ES8311_ANA_PLAY_CTRL, 0x08);

    // Enable LDO
    ok &= es8311_write(ES8311_LDO_CTRL, 0x0F);

    // Enable amplifier
    ok &= es8311_write(ES8311_AMP_CTRL, 0x00);

    if (ok) {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 codec initialized.");
    } else {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 init had errors.");
    }
    return ok;
}

// Initialize I2S output for the Waveshare board
bool i2s_output_init_waveshare() {
    AUDIO_DEBUG_SERIAL.println("[audio] Initializing I2S output...");

    // Initialize ES8311 codec first
    if (!audio_codec_init_es8311()) {
        AUDIO_DEBUG_SERIAL.println("[audio] ES8311 init failed!");
        return false;
    }

    // Enable power amplifier
    pinMode(AUDIO_PA_PIN, OUTPUT);
    digitalWrite(AUDIO_PA_PIN, HIGH);
    AUDIO_DEBUG_SERIAL.printf("[audio] PA enabled on GPIO%d\r\n", AUDIO_PA_PIN);

    // Initialize I2S output using ESP_I2S library
    // Pins: BCLK=AUDIO_I2S_BCK, WS/LRCLK=AUDIO_I2S_WS, DOUT=AUDIO_I2S_DO, MCK=AUDIO_I2S_MCK
    I2SClass i2s;
    i2s.setPins(AUDIO_I2S_BCK, AUDIO_I2S_WS, AUDIO_I2S_DO, -1, AUDIO_I2S_MCK);

    if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_LEFT)) {
        AUDIO_DEBUG_SERIAL.println("[audio] I2S begin failed!");
        return false;
    }

    AUDIO_DEBUG_SERIAL.println("[audio] I2S output initialized at 16kHz.");
    return true;
}

// Stub for compatibility with the original interface
bool audio_output_codec_init(void) {
    return i2s_output_init_waveshare();
}
