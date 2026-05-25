/*
 * Test 01: Display smoke test for the Freenove ESP32-S3 Media Kit.
 *
 * Goal:
 *   Verify the TFT/LVGL display path before testing any other hardware.
 *
 * Expected result:
 *   The display shows a boot banner plus two updating lines. The serial
 *   monitor prints the same step count once per second.
 */

#include <Arduino.h>

// Reuse the known-good Freenove display stack from the full client.
#include "../client_esp32/driver_button.cpp"
#include "../client_esp32/display.cpp"

static uint32_t last_update_ms = 0;
static uint32_t counter = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println();
  Serial.println("Test 01: Display smoke test");
  display.init(TFT_DIRECTION);
  display.showBootInstructions("Test 01: display");
  display.displayLine1("Freenove display initialized.");
  display.displayLine2("Counter starts now.");
}

void loop() {
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
