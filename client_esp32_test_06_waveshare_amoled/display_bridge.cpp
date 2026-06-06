// Display bridge for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses Arduino_GFX with SH8601 QSPI AMOLED driver (368x448, RGB565)
//
// SH8601 QSPI pinout:
//   SDIO0=GPIO4, SDIO1=GPIO5, SDIO2=GPIO6, SDIO3=GPIO7
//   SCLK=GPIO11, CS=GPIO12
//
// Note: SH8601 uses QSPI (quad SPI) data bus, not standard SPI.
// The Arduino_ESP32QSPI class handles the ESP32-S3 QSPI peripheral.

#include "board_pins.h"
#include <Arduino.h>
#include <Wire.h>

// QSPI display bus (SH8601 AMOLED, 368x448)
#include <Arduino_DataBus.h>
#include <Arduino_GFX_Library.h>

#ifndef DISPLAY_DEBUG_SERIAL
#define DISPLAY_DEBUG_SERIAL Serial
#endif

// SH8601 reset pin (if available on board)
// The Waveshare AMOLED-1.8 may not have a separate reset pin;
// the SH8601 can be reset via QSPI command sequence.
#ifndef SH8601_RST_PIN
#define SH8601_RST_PIN GFX_NOT_DEFINED
#endif

// QSPI data bus for SH8601
static Arduino_ESP32QSPI* gfx_bus = nullptr;
static Arduino_SH8601* gfx = nullptr;

bool display_init_sh8601() {
    DISPLAY_DEBUG_SERIAL.println("[display] Initializing SH8601 QSPI AMOLED...");

    // Create QSPI data bus: CS, SCLK, SDIO0-3
    gfx_bus = new Arduino_ESP32QSPI(
        LCD_CS, LCD_SCLK,
        LCD_SDIO0, LCD_SDIO1,
        LCD_SDIO2, LCD_SDIO3
    );

    // Create SH8601 display driver
    // Parameters: bus, CS, SPI clock, width, height
    gfx = new Arduino_SH8601(gfx_bus, SH8601_RST_PIN, 0, LCD_WIDTH, LCD_HEIGHT);

    if (!gfx->begin()) {
        DISPLAY_DEBUG_SERIAL.println("[display] SH8601 begin() FAILED!");
        return false;
    }

    // Landscape orientation (rotation 1 = 90 degrees clockwise)
    gfx->setRotation(1);

    // Fill black
    gfx->fillScreen(RGB565_BLACK);

    // Set brightness to maximum
    gfx->setBrightness(255);

    DISPLAY_DEBUG_SERIAL.printf("[display] SH8601 initialized: %dx%d, rotation=%d\r\n",
        LCD_WIDTH, LCD_HEIGHT, gfx->getRotation());
    return true;
}

void display_clear() {
    if (gfx) gfx->fillScreen(RGB565_BLACK);
}

void display_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (gfx) gfx->drawPixel(x, y, color);
}

void display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (gfx) gfx->drawFastHLine(x, y, w, color);
}

void display_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (gfx) gfx->drawFastVLine(x, y, h, color);
}

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (gfx) gfx->fillRect(x, y, w, h, color);
}

void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (gfx) gfx->drawRect(x, y, w, h, color);
}

void display_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (gfx) gfx->drawCircle(x0, y0, r, color);
}

void display_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (gfx) gfx->fillCircle(x0, y0, r, color);
}

void display_draw_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (gfx) gfx->drawTriangle(x0, y0, x1, y1, x2, y2, color);
}

void display_fill_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    if (gfx) gfx->fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (gfx) gfx->drawLine(x0, y0, x1, y1, color);
}

void display_set_cursor(int16_t x, int16_t y) {
    if (gfx) gfx->setCursor(x, y);
}

void display_set_text_color(uint16_t color) {
    if (gfx) gfx->setTextColor(color);
}

void display_set_text_size(uint8_t size) {
    if (gfx) gfx->setTextSize(size);
}

void display_set_text_size(uint8_t sx, uint8_t sy) {
    if (gfx) gfx->setTextSize(sx, sy);
}

void display_println(const char* text) {
    if (gfx) gfx->println(text);
}

void display_print(const char* text) {
    if (gfx) gfx->print(text);
}

int display_width() {
    return gfx ? gfx->width() : 0;
}

int display_height() {
    return gfx ? gfx->height() : 0;
}
