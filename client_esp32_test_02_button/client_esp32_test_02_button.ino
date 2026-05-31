/*
 * Test 02: Button input test for the Freenove ESP32-S3 Media Kit.
 *
 * Goal:
 *   Verify the analog button path on GPIO 19 and confirm the debounced
 *   press/release states from the shared Button driver.
 *
 * Expected result:
 *   Pressing the button increments the press count. The display and serial
 *   monitor show the current raw ADC value, decoded key value, and state.
 */

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>   // Must be configured for ST7735 in User_Setup.h

#include "../client_esp32/board_pins.h"
#include "../client_esp32/driver_button.h"
#include "../client_esp32/display.h"

static const int BUTTON_ADC_PIN = 19;

static int last_button_state = Button::KEY_STATE_IDLE;
static uint32_t press_count = 0;
static uint32_t last_refresh_ms = 0;

const char *button_state_name(int state) {
  switch (state) {
    case Button::KEY_STATE_IDLE: return "IDLE";
    case Button::KEY_STATE_PRESSED_BOUNCE_TIME: return "PRESS_BOUNCE";
    case Button::KEY_STATE_PRESSED: return "PRESSED";
    case Button::KEY_STATE_RELEASE_BOUNCE_TIME: return "RELEASE_BOUNCE";
    case Button::KEY_STATE_RELEASED: return "RELEASED";
    default: return "UNKNOWN";
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println();
  Serial.println("Test 02: Button input test");
  display.init(TFT_DIRECTION);
  display.showBootInstructions("Test 02: button");
  display.displayLine1("Press the Freenove button.");
  display.displayLine2("Waiting for input...");
}

void loop() {
  button.key_scan();
  const int state = button.get_button_state();
  const int key = button.get_button_key_value();
  const int raw = analogRead(BUTTON_ADC_PIN);

  if (state == Button::KEY_STATE_PRESSED &&
      last_button_state != Button::KEY_STATE_PRESSED) {
    press_count++;
    Serial.printf("Button press %lu, key=%d, raw=%d\r\n",
      (unsigned long)press_count, key, raw);
  }
  last_button_state = state;

  if (millis() - last_refresh_ms >= 100) {
    last_refresh_ms = millis();

    char line1[96];
    char line2[96];
    snprintf(line1, sizeof(line1), "Presses: %lu  State: %s",
      (unsigned long)press_count, button_state_name(state));
    snprintf(line2, sizeof(line2), "Key: %d  Raw ADC: %d", key, raw);

    display.displayLine1(line1);
    display.displayLine2(line2);
  }

  display.routine();
  delay(10);
}
