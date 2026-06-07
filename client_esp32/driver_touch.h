// driver_touch.h — FT3168 capacitive touch driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
//
// The FT3168 is a 10-bit touch controller at I2C address 0x5D.
// Board pins: SDA=GPIO15, SCL=GPIO14, INT=GPIO21
//
// Display: SH8601 QSPI AMOLED, 368x448 pixels
// Touch sensor native range: X 0-1023, Y 0-1023
//
// Usage:
//   touch_init()        — I2C scan, chip verify, FT3168 init
//   touch_read()        — read touch state (returns true if any touch event)
//   touch_get_x(), y()  — get mapped display coordinates
//   touch_is_pressed()  — true while any touch point is active
//   touch_get_raw_x(), y() — get raw 10-bit coordinates

#ifndef DRIVER_TOUCH_H
#define DRIVER_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

// ---------- FT3168 I2C address (A0 = HIGH) ----------
#define TOUCH_FT3168_I2C_ADDR  0x5D

// ---------- FT3168 registers ----------
#define TOUCH_FT3168_REG_CHIP_ID    0xD0
#define TOUCH_FT3168_REG_MODE       0x00
#define TOUCH_FT3168_REG_TOUCH_CNT  0x02
#define TOUCH_FT3168_REG_TOUCH_DATA 0x03

// ---------- Display / touch coordinate limits ----------
// Waveshare AMOLED: 368 (width) x 448 (height)
#define TOUCH_DISPLAY_WIDTH   368
#define TOUCH_DISPLAY_HEIGHT  448
#define TOUCH_RAW_MAX         1023   // 10-bit

// ---------- Debounce ----------
// Minimum time (ms) between reported touch events
#define TOUCH_DEBOUNCE_MS     80
// Minimum pixel shift to report a new event (avoids jitter)
#define TOUCH_DEBOUNCE_THRESH 5

// ---------- Public API ----------

/**
 * Initialize the FT3168 touch controller.
 *
 * Scans I2C for the device at 0x5D, reads chip ID,
 * configures touch mode, and sets up the Wire bus.
 *
 * @return true if FT3168 detected and initialized successfully
 */
bool touch_init(void);

/**
 * Read current touch state from the FT3168.
 *
 * Reads touch point count and coordinates, applies debounce filtering,
 * and updates the mapped display coordinates.
 *
 * @return true if a touch event was detected this call
 */
bool touch_read(void);

/** Get the last mapped X coordinate (0..TOUCH_DISPLAY_WIDTH-1). */
int touch_get_x(void);

/** Get the last mapped Y coordinate (0..TOUCH_DISPLAY_HEIGHT-1). */
int touch_get_y(void);

/** Get the raw X coordinate (0..1023). */
int touch_get_raw_x(void);

/** Get the raw Y coordinate (0..1023). */
int touch_get_raw_y(void);

/** True while at least one touch point is active. */
bool touch_is_pressed(void);

/** True if touch_init() succeeded. */
bool touch_is_initialized(void);

#endif // DRIVER_TOUCH_H
