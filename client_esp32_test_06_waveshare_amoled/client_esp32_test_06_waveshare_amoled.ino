/*
 * Test 06: Waveshare ESP32-S3-Touch-AMOLED-1.8 hardware smoke test.
 *
 * Goal:
 *   Verify all major hardware subsystems on the Waveshare board:
 *   1. SH8601 QSPI AMOLED display (368x448)
 *   2. FT3168 capacitive touch controller (I2C)
 *   3. ES8311 audio codec + I2S output
 *
 * Expected results:
 *   - Display renders color test patterns with text overlay
 *   - Touch coordinates print to serial when touched
 *   - Serial prints stage markers for each initialization step
 *
 * Board selection:
 *   Define BOARD_WAVESHARE_AMOLED in sketch_config.h (already set).
 *
 * Pinout reference:
 *   Display (SH8601 QSPI): SDIO0=4, SDIO1=5, SDIO2=6, SDIO3=7, SCLK=11, CS=12
 *   Touch (FT3168 I2C): SDA=15, SCL=14, INT=21
 *   Audio (ES8311): MCK=16, BCK=9, DI(MIC)=8, WS=45, DO(SPK)=10, PA=46
 *   Power (AXP2101 I2C): via shared I2C bus
 *   SD Card (SDMMC): CLK=2, CMD=1, DATA=3
 */

#include <Arduino.h>

// Board pin definitions
#include "board_pins.h"

// Display bridge (Arduino_GFX + SH8601)
extern bool display_init_sh8601();
extern void display_clear();
extern void display_draw_pixel(int16_t x, int16_t y, uint16_t color);
extern void display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
extern void display_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
extern void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
extern void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
extern void display_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
extern void display_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
extern void display_draw_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
extern void display_fill_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
extern void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
extern void display_set_cursor(int16_t x, int16_t y);
extern void display_set_text_color(uint16_t color);
extern void display_set_text_size(uint8_t size);
extern void display_set_text_size(uint8_t sx, uint8_t sy);
extern void display_println(const char* text);
extern void display_print(const char* text);
extern int display_width();
extern int display_height();

// Touch bridge (FT3168)
extern bool touch_init_ft3168();
extern bool touch_read_ft3168();
extern int touch_get_x();
extern int touch_get_y();
extern bool touch_is_pressed();

// Audio bridge (ES8311)
extern bool i2s_output_init_waveshare();
extern bool audio_output_codec_init(void);

// Color definitions for 16-bit RGB565
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x0400
#define COLOR_BLUE        0x001F
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_YELLOW      0xFFE0
#define COLOR_DARKGRAY    0x7BEF
#define COLOR_LIGHTGRAY   0xC618

static uint32_t last_update_ms = 0;
static uint32_t counter = 0;
static int test_pattern = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000) delay(10);

    Serial.println();
    Serial.println("========================================");
    Serial.println("Test 06: Waveshare ESP32-S3-Touch-AMOLED-1.8");
    Serial.println("========================================");
    Serial.println();

    // --- Stage 1: Display ---
    Serial.println("[stage] 1: Display init (SH8601 QSPI AMOLED)");
    if (!display_init_sh8601()) {
        Serial.println("[FAIL] Display init failed!");
        while (1) { delay(1000); }
    }
    Serial.println("[OK]   Display initialized.");
    Serial.printf("[info] Screen: %dx%d\r\n", display_width(), display_height());
    Serial.println();

    // Render initial pattern
    display_fill_rect(0, 0, display_width(), display_height(), COLOR_BLACK);
    display_set_text_size(2);
    display_set_text_color(COLOR_GREEN);
    display_set_cursor(20, 40);
    display_println("Waveshare AMOLED");
    display_set_text_color(COLOR_CYAN);
    display_set_cursor(20, 80);
    display_println("Test 06: Hardware");
    display_set_text_color(COLOR_YELLOW);
    display_set_cursor(20, 120);
    display_println("Smoke Test");
    display_set_text_color(COLOR_WHITE);
    display_set_cursor(20, 200);
    display_println("Initializing...");

    delay(2000);

    // --- Stage 2: Touch ---
    Serial.println("[stage] 2: Touch init (FT3168 I2C)");
    if (!touch_init_ft3168()) {
        Serial.println("[WARN] Touch init failed (continuing without touch).");
    } else {
        Serial.println("[OK]   Touch initialized.");
    }
    Serial.println();

    // --- Stage 3: Audio ---
    Serial.println("[stage] 3: Audio init (ES8311 + I2S)");
    if (!i2s_output_init_waveshare()) {
        Serial.println("[WARN] Audio init failed (continuing without audio).");
    } else {
        Serial.println("[OK]   Audio initialized.");
    }
    Serial.println();

    Serial.println("========================================");
    Serial.println("All subsystems initialized. Starting loop.");
    Serial.println("========================================");
    Serial.println();
}

// Render a color bar test pattern
void render_color_bars() {
    int w = display_width();
    int h = display_height();
    int bar_w = w / 8;

    uint16_t colors[] = { COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE, COLOR_BLACK };

    for (int i = 0; i < 8; i++) {
        display_fill_rect(i * bar_w, 0, bar_w, h, colors[i]);
    }

    // Border
    display_draw_rect(0, 0, w - 1, h - 1, COLOR_WHITE);
}

// Render a gradient test pattern
void render_gradient() {
    int w = display_width();
    int h = display_height();

    for (int y = 0; y < h; y++) {
        uint16_t r = (uint16_t)(255 * y / h);
        uint16_t g = (uint16_t)(255 * (h - y) / h);
        uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | ((r >> 3) >> 1);
        display_draw_hline(0, y, w, color);
    }
}

// Render geometric shapes
void render_shapes() {
    int w = display_width();
    int h = display_height();
    int cx = w / 2;
    int cy = h / 2;

    display_fill_rect(0, 0, w, h, COLOR_BLACK);

    // Outer rectangle
    display_draw_rect(10, 10, w - 20, h - 20, COLOR_WHITE);

    // Circle
    display_draw_circle(cx, cy, 80, COLOR_RED);
    display_fill_circle(cx, cy, 40, COLOR_GREEN);

    // Triangle
    display_draw_triangle(cx, cy - 120, cx - 100, cy + 60, cx + 100, cy + 60, COLOR_BLUE);
    display_fill_triangle(cx, cy - 100, cx - 80, cy + 40, cx + 80, cy + 40, COLOR_CYAN);

    // Cross lines
    display_draw_line(10, cy, w - 10, cy, COLOR_YELLOW);
    display_draw_line(cx, 10, cx, h - 10, COLOR_YELLOW);
}

void loop() {
    // Update every 2 seconds
    if (millis() - last_update_ms >= 2000) {
        last_update_ms = millis();
        counter++;

        // Cycle through test patterns
        test_pattern = (test_pattern + 1) % 4;

        switch (test_pattern) {
            case 0:
                render_color_bars();
                break;
            case 1:
                render_gradient();
                break;
            case 2:
                render_shapes();
                break;
            case 3:
                display_fill_rect(0, 0, display_width(), display_height(), COLOR_BLACK);
                display_set_text_size(2);
                display_set_text_color(COLOR_GREEN);
                display_set_cursor(20, 40);
                display_println("Test Pattern");
                display_set_text_color(COLOR_CYAN);
                display_set_cursor(20, 80);
                display_println("Iteration:");
                display_set_text_color(COLOR_YELLOW);
                char buf[32];
                snprintf(buf, sizeof(buf), "%lu", (unsigned long)counter);
                display_set_cursor(20, 120);
                display_print(buf);
                break;
        }

        // Display touch info
        if (touch_is_pressed()) {
            char touch_buf[64];
            snprintf(touch_buf, sizeof(touch_buf), "Touch: %d, %d", touch_get_x(), touch_get_y());
            display_set_text_size(1);
            display_set_text_color(COLOR_WHITE);
            display_set_cursor(10, display_height() - 30);
            display_print(touch_buf);
        } else {
            display_set_text_size(1);
            display_set_text_color(COLOR_DARKGRAY);
            display_set_cursor(10, display_height() - 30);
            display_print("No touch");
        }

        // Display uptime
        char uptime_buf[64];
        uint32_t secs = millis() / 1000;
        snprintf(uptime_buf, sizeof(uptime_buf), "Uptime: %lus", (unsigned long)secs);
        display_set_text_size(1);
        display_set_text_color(COLOR_LIGHTGRAY);
        display_set_cursor(10, display_height() - 10);
        display_print(uptime_buf);

        Serial.printf("[update] Pattern %d, Counter: %lu, Touch: %s\r\n",
            test_pattern, (unsigned long)counter,
            touch_is_pressed() ? "pressed" : "released");
    }

    // Poll touch continuously
    touch_read_ft3168();

    delay(50);
}
