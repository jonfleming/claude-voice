// driver_touch.cpp — FT3168 capacitive touch driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
//
// Hardware:
//   FT3168 touch controller at I2C 0x5D
//   SDA=GPIO15, SCL=GPIO14, INT=GPIO21
//   SH8601 QSPI AMOLED display: 368x448
//
// Coordinate mapping:
//   FT3168 returns 10-bit coordinates (0-1023)
//   Mapped to display pixels: X→368, Y→448
//   Touch sensor is mounted with Y=0 at top, X=0 at left (portrait)
//
// Debouncing:
//   Minimum 80ms between reported events
//   Minimum 5-pixel shift to report a new event (avoids jitter)

#include "driver_touch.h"
#include "board_pins.h"

#include <Arduino.h>
#include <Wire.h>

// ---------- Internal state ----------

static bool s_initialized = false;
static int s_raw_x = 0;
static int s_raw_y = 0;
static int s_mapped_x = -1;
static int s_mapped_y = -1;
static bool s_pressed = false;

// Debounce state
static unsigned long s_last_event_ms = 0;
static int s_last_mapped_x = -1;
static int s_last_mapped_y = -1;

// ---------- FT3168 I2C helpers ----------

static bool ft3168_read_reg(uint8_t reg, uint8_t* value) {
    Wire.beginTransmission(TOUCH_FT3168_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    Wire.requestFrom(TOUCH_FT3168_I2C_ADDR, (uint8_t)1);
    if (Wire.available() < 1) {
        return false;
    }
    *value = Wire.read();
    return true;
}

static bool ft3168_read_regs(uint8_t reg, uint8_t* values, uint8_t count) {
    Wire.beginTransmission(TOUCH_FT3168_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    Wire.requestFrom(TOUCH_FT3168_I2C_ADDR, count);
    if (Wire.available() < count) {
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        values[i] = Wire.read();
    }
    return true;
}

static bool ft3168_write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TOUCH_FT3168_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// ---------- Public API ----------

bool touch_init(void) {
    Serial.printf("[touch] Initializing FT3168 on I2C (SDA=GPIO%d, SCL=GPIO%d)...\r\n",
        TOUCH_I2C_SDA, TOUCH_I2C_SCL);

    // Initialize I2C bus
    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);
    Serial.printf("[touch] I2C bus started: SDA=GPIO%d, SCL=GPIO%d\r\n",
        TOUCH_I2C_SDA, TOUCH_I2C_SCL);

    // I2C scan to verify FT3168 is on the bus
    bool found = false;
    for (uint8_t addr = 0x01; addr <= 0x7E; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[touch] I2C device found at 0x%02X\r\n", addr);
            if (addr == TOUCH_FT3168_I2C_ADDR) {
                found = true;
            }
        }
    }

    if (!found) {
        Serial.println("[touch] FT3168 NOT found on I2C bus!");
        Serial.println("[touch] Check: SDA=GPIO15, SCL=GPIO14, I2C addr=0x5D");
        return false;
    }

    // Read chip ID to verify
    uint8_t chip_id = 0;
    if (!ft3168_read_reg(TOUCH_FT3168_REG_CHIP_ID, &chip_id)) {
        Serial.printf("[touch] Failed to read chip ID. Got: 0x%02X\r\n", chip_id);
        return false;
    }
    Serial.printf("[touch] FT3168 chip ID: 0x%02X\r\n", chip_id);

    // Configure FT3168 for touch mode
    // Register 0x00 (MODE): bit 0 = 1 to enter command mode
    if (!ft3168_write_reg(TOUCH_FT3168_REG_MODE, 0x01)) {
        Serial.println("[touch] Failed to set command mode");
        return false;
    }
    delay(10);

    // Register 0x05 (THRESH): enable touch, set threshold
    if (!ft3168_write_reg(0x05, 0x01)) {
        Serial.println("[touch] Failed to set touch threshold");
        return false;
    }
    delay(10);

    // Exit command mode
    if (!ft3168_write_reg(TOUCH_FT3168_REG_MODE, 0x00)) {
        Serial.println("[touch] Failed to exit command mode");
        return false;
    }
    delay(10);

    s_initialized = true;
    Serial.println("[touch] FT3168 initialized successfully.");
    return true;
}

bool touch_read(void) {
    if (!s_initialized) {
        return false;
    }

    uint8_t touch_cnt = 0;
    if (!ft3168_read_reg(TOUCH_FT3168_REG_TOUCH_CNT, &touch_cnt)) {
        return false;
    }

    bool new_press = (touch_cnt > 0 && touch_cnt <= 2);

    if (new_press) {
        // Read touch data for first touch point (5 bytes)
        uint8_t data[5];
        if (!ft3168_read_regs(TOUCH_FT3168_REG_TOUCH_DATA, data, 5)) {
            return false;
        }

        // Parse X coordinate (10-bit): bits [3:0] of byte0 + byte1
        s_raw_x = ((data[0] & 0x0F) << 8) | data[1];

        // Parse Y coordinate (10-bit): bits [7:4] of byte2 + byte3
        s_raw_y = ((data[2] & 0xF0) << 4) | data[3];

        // Clamp to valid range
        if (s_raw_x > TOUCH_RAW_MAX) s_raw_x = TOUCH_RAW_MAX;
        if (s_raw_y > TOUCH_RAW_MAX) s_raw_y = TOUCH_RAW_MAX;

        // Map to display coordinates
        // Touch sensor: X=0 left, Y=0 top (portrait orientation)
        // Display: 368 wide x 448 tall
        s_mapped_x = (s_raw_x * TOUCH_DISPLAY_WIDTH) / (TOUCH_RAW_MAX + 1);
        s_mapped_y = (s_raw_y * TOUCH_DISPLAY_HEIGHT) / (TOUCH_RAW_MAX + 1);

        // Clamp mapped coordinates
        if (s_mapped_x >= TOUCH_DISPLAY_WIDTH) s_mapped_x = TOUCH_DISPLAY_WIDTH - 1;
        if (s_mapped_y >= TOUCH_DISPLAY_HEIGHT) s_mapped_y = TOUCH_DISPLAY_HEIGHT - 1;

        s_pressed = true;
    } else {
        s_pressed = false;
    }

    // Apply debounce filtering
    unsigned long now = millis();
    bool event_reported = false;

    if (s_pressed && (now - s_last_event_ms >= TOUCH_DEBOUNCE_MS)) {
        // Check if coordinates have shifted enough to warrant a new report
        int dx = abs(s_mapped_x - s_last_mapped_x);
        int dy = abs(s_mapped_y - s_last_mapped_y);

        if (dx >= TOUCH_DEBOUNCE_THRESH || dy >= TOUCH_DEBOUNCE_THRESH ||
            s_last_mapped_x < 0) {
            s_last_event_ms = now;
            s_last_mapped_x = s_mapped_x;
            s_last_mapped_y = s_mapped_y;
            event_reported = true;
        }
    }

    return event_reported;
}

int touch_get_x(void) {
    return s_mapped_x;
}

int touch_get_y(void) {
    return s_mapped_y;
}

int touch_get_raw_x(void) {
    return s_raw_x;
}

int touch_get_raw_y(void) {
    return s_raw_y;
}

bool touch_is_pressed(void) {
    return s_pressed;
}

bool touch_is_initialized(void) {
    return s_initialized;
}
