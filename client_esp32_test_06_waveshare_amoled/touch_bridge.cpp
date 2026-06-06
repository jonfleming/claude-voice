// Touch driver bridge for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses FT3168 capacitive touch controller via I2C

#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>

#ifndef TOUCH_DEBUG_SERIAL
#define TOUCH_DEBUG_SERIAL Serial
#endif

// FT3168 I2C address (default)
#define FT3168_I2C_ADDR  0x5D

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

    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);

    // Check if FT3168 is on the I2C bus
    uint8_t chip_id = 0;
    if (!ft3168_read_reg(0xD0, &chip_id)) {
        TOUCH_DEBUG_SERIAL.println("[touch] FT3168 not found on I2C!");
        return false;
    }
    TOUCH_DEBUG_SERIAL.printf("[touch] FT3168 chip ID: 0x%02X\r\n", chip_id);

    // Configure FT3168 for touch mode
    ft3168_write_reg(0x00, 0x01);  // Enter command mode
    ft3168_write_reg(0x05, 0x01);  // Enable touch
    ft3168_write_reg(0x00, 0x00);  // Exit command mode

    touch_initialized = true;
    TOUCH_DEBUG_SERIAL.println("[touch] FT3168 initialized.");
    return true;
}

// Read touch coordinates from FT3168
bool touch_read_ft3168() {
    if (!touch_initialized) return false;

    uint8_t touch_cnt = 0;
    if (!ft3168_read_reg(0x02, &touch_cnt)) {
        return false;
    }

    if (touch_cnt > 0) {
        // Read touch data (5 bytes per touch point)
        Wire.beginTransmission(FT3168_I2C_ADDR);
        Wire.write(0x03);  // Touch data register
        Wire.endTransmission(false);
        Wire.requestFrom(FT3168_I2C_ADDR, (uint8_t)5);

        if (Wire.available() >= 5) {
            uint8_t b0 = Wire.read();
            uint8_t b1 = Wire.read();
            uint8_t b2 = Wire.read();
            uint8_t b3 = Wire.read();
            uint8_t b4 = Wire.read();

            touch_x = ((b0 & 0x0F) << 8) | b1;
            touch_y = ((b2 & 0x0F) << 8) | b3;
            touch_pressed = true;

            TOUCH_DEBUG_SERIAL.printf("[touch] X=%d, Y=%d\r\n", touch_x, touch_y);
            return true;
        }
    }

    touch_pressed = false;
    return false;
}

int touch_get_x() { return touch_x; }
int touch_get_y() { return touch_y; }
bool touch_is_pressed() { return touch_pressed; }
