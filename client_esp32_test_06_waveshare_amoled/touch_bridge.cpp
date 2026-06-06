// Touch driver bridge for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses FT3168 capacitive touch controller via I2C
//
// FT3168 I2C address: 0x5D (default, A0=HIGH)
// Board I2C pins: SDA=GPIO15, SCL=GPIO14
// Touch INT: GPIO21 (active LOW)
//
// The FT3168 is a 10-bit touch controller with I2C interface.
// Touch data register layout (per point, 5 bytes):
//   Byte 0: Touch ID (bits 7-4), X high (bits 3-0)
//   Byte 1: X low
//   Byte 2: Y high (bits 7-4), Touch pressure (bits 3-0)
//   Byte 3: Y low
//   Byte 4: Reserved

#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>

#ifndef TOUCH_DEBUG_SERIAL
#define TOUCH_DEBUG_SERIAL Serial
#endif

// FT3168 I2C address (default, A0 pin = HIGH)
#define FT3168_I2C_ADDR  0x5D

// FT3168 registers
#define FT3168_REG_CHIP_ID    0xD0  // Chip ID register (should return 0x5D or 0x31)
#define FT3168_REG_MODE       0x00  // Mode register
#define FT3168_REG_TOUCH_CNT  0x02  // Touch point count
#define FT3168_REG_TOUCH_DATA 0x03  // Touch data start register

// Touch state
static bool touch_initialized = false;
static int touch_x = 0;
static int touch_y = 0;
static bool touch_pressed = false;

// Read a register from FT3168
static bool ft3168_read_reg(uint8_t reg, uint8_t* value) {
    Wire.beginTransmission(FT3168_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    Wire.requestFrom(FT3168_I2C_ADDR, (uint8_t)1);
    if (Wire.available() < 1) {
        return false;
    }
    *value = Wire.read();
    return true;
}

// Read multiple bytes from FT3168
static bool ft3168_read_regs(uint8_t reg, uint8_t* values, uint8_t count) {
    Wire.beginTransmission(FT3168_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    Wire.requestFrom(FT3168_I2C_ADDR, count);
    if (Wire.available() < count) {
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        values[i] = Wire.read();
    }
    return true;
}

// Write a register on FT3168
static bool ft3168_write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(FT3168_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// Initialize FT3168 touch controller
bool touch_init_ft3168() {
    TOUCH_DEBUG_SERIAL.println("[touch] Initializing FT3168...");

    // Initialize I2C bus
    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);
    TOUCH_DEBUG_SERIAL.printf("[touch] I2C bus started: SDA=GPIO%d, SCL=GPIO%d\r\n",
        TOUCH_I2C_SDA, TOUCH_I2C_SCL);

    // I2C scan to verify FT3168 is on the bus
    bool found = false;
    for (uint8_t addr = 0x01; addr <= 0x7E; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            TOUCH_DEBUG_SERIAL.printf("[touch] I2C device found at 0x%02X\r\n", addr);
            if (addr == FT3168_I2C_ADDR) found = true;
        }
    }

    if (!found) {
        TOUCH_DEBUG_SERIAL.println("[touch] FT3168 NOT found on I2C bus!");
        TOUCH_DEBUG_SERIAL.println("[touch] Check: SDA=GPIO15, SCL=GPIO14, I2C addr=0x5D");
        return false;
    }

    // Read chip ID to verify
    uint8_t chip_id = 0;
    if (!ft3168_read_reg(FT3168_REG_CHIP_ID, &chip_id)) {
        TOUCH_DEBUG_SERIAL.printf("[touch] Failed to read chip ID. Got: 0x%02X\r\n", chip_id);
        return false;
    }
    TOUCH_DEBUG_SERIAL.printf("[touch] FT3168 chip ID: 0x%02X\r\n", chip_id);

    // Configure FT3168 for touch mode
    // Register 0x00 (MODE): bit 0 = 1 to enter command mode
    ft3168_write_reg(FT3168_REG_MODE, 0x01);
    delay(10);

    // Register 0x05 (THRESH): enable touch, set threshold
    ft3168_write_reg(0x05, 0x01);
    delay(10);

    // Exit command mode
    ft3168_write_reg(FT3168_REG_MODE, 0x00);
    delay(10);

    touch_initialized = true;
    TOUCH_DEBUG_SERIAL.println("[touch] FT3168 initialized successfully.");
    return true;
}

// Read touch coordinates from FT3168
bool touch_read_ft3168() {
    if (!touch_initialized) return false;

    uint8_t touch_cnt = 0;
    if (!ft3168_read_reg(FT3168_REG_TOUCH_CNT, &touch_cnt)) {
        return false;
    }

    if (touch_cnt > 0 && touch_cnt <= 2) {
        // Read touch data for first touch point (5 bytes)
        uint8_t data[5];
        if (!ft3168_read_regs(FT3168_REG_TOUCH_DATA, data, 5)) {
            return false;
        }

        // Parse X coordinate (10-bit): bits [3:0] of byte0 + byte1
        touch_x = ((data[0] & 0x0F) << 8) | data[1];

        // Parse Y coordinate (10-bit): bits [7:4] of byte2 + byte3
        touch_y = ((data[2] & 0xF0) << 4) | data[3];

        touch_pressed = true;

        TOUCH_DEBUG_SERIAL.printf("[touch] X=%d, Y=%d, pts=%d\r\n", touch_x, touch_y, touch_cnt);
        return true;
    }

    touch_pressed = false;
    return false;
}

int touch_get_x() { return touch_x; }
int touch_get_y() { return touch_y; }
bool touch_is_pressed() { return touch_pressed; }
