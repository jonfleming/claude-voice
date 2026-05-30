/*
 * Test 01: Display smoke test for the ESP32-S3 Media Kit / AIPI Lite.
 *
 * Goal:
 *   Verify the TFT/LVGL display path before testing any other hardware.
 *
 * Expected result:
 *   The display shows a boot banner plus two updating lines. The serial
 *   monitor prints the same step count once per second.
 *
 * Board selection:
 *   Default build targets Freenove ESP32-S3 Media Kit.
 *   To build for AIPI Lite: define BOARD_AIPI_LITE
 *     - Add -DBOARD_AIPI_LITE in build flags, or
 *     - Uncomment #define BOARD_AIPI_LITE in client_esp32/board_pins.h.
 */

#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 1
#endif

#ifndef ARDUINO_USB_MODE
// ESP32-S3: 1 = USB-Serial/JTAG (same device class you see with ESPHome)
#define ARDUINO_USB_MODE 1
#endif

#ifndef ARDUINO_USB_MSC_ON_BOOT
#define ARDUINO_USB_MSC_ON_BOOT 0
#endif

#ifndef ARDUINO_USB_DFU_ON_BOOT
#define ARDUINO_USB_DFU_ON_BOOT 0
#endif

#include <Arduino.h>

// Reuse the known-good display stack from the full client.
// These are implemented by sketch-local bridge .cpp files so Arduino
// compiles them as normal translation units (more stable than text-including
// source files directly in the .ino).
#include "../client_esp32/board_pins.h"
#include "../client_esp32/driver_button.h"
#include "../client_esp32/display.h"

static uint32_t last_update_ms = 0;
static uint32_t counter = 0;

void setup() {
#ifdef BOARD_AIPI_LITE
  // Keep AIPI Lite powered when running on battery.
  pinMode(AIPI_POWER_KEEPALIVE_PIN, OUTPUT);
  digitalWrite(AIPI_POWER_KEEPALIVE_PIN, HIGH);
#endif

  Serial.begin(115200);
  // Wait up to 3 s for a serial monitor; continue without one so the sketch
  // works on battery (native USB-CDC never becomes ready without a host).
  uint32_t serial_wait_start = millis();
  while (!Serial && (millis() - serial_wait_start < 3000)) {
    delay(10);
  }

  Serial.println();
  Serial.println("Test 01: Display smoke test");
  Serial.println("Test 01: display");
  Serial.println("[stage] setup: before display.init");

  display.init(TFT_DIRECTION);
  Serial.println("[stage] setup: after display.init");

  display.showBootInstructions("Test 01: display");
  Serial.println("[stage] setup: after showBootInstructions");

  display.displayLine1("Display initialized.");
  Serial.println("[stage] setup: after displayLine1");

  display.displayLine2("Counter starts now.");
  Serial.println("[stage] setup: after displayLine2");

}

void loop() {
  static bool logged_first_loop = false;
  if (!logged_first_loop) {
    Serial.println("[stage] loop: entered");
    logged_first_loop = true;
  }

  display.routine();

  if (millis() - last_update_ms >= 1000) {
    last_update_ms = millis();
    counter++;

    char line1[96];
    char line2[96];
    snprintf(line1, sizeof(line1), "Display refresh count: %lu",
      (unsigned long)counter);
    snprintf(line2, sizeof(line2), "Uptime: %lu seconds",
      (unsigned long)(millis() / 1000));

    Serial.println(line1);
    display.displayLine1(line1);
    display.displayLine2(line2);
  }

  delay(5);
}
