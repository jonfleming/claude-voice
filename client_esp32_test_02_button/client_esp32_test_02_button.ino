/*
 * Test 02: Button input test for the Freenove ESP32-S3 Media Kit / AIPI Lite.
 *
 * Goal:
 *   Verify the board button path and confirm the debounced
 *   press/release states from the shared Button driver.
 *
 * Expected result:
 *   Pressing the button increments the press count. The display and serial
 *   monitor show the current raw ADC value, decoded key value, and state.
 */
#include "sketch_config.h"

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>   // Must be configured for ST7735 in User_Setup.h

#include "../client_esp32/board_pins.h"
#include "../client_esp32/driver_button.h"
#include "../client_esp32/display.h"

#ifdef BOARD_AIPI_LITE
#define TEST_SERIAL Serial
#else
#define TEST_SERIAL Serial0
#endif

#ifdef BOARD_AIPI_LITE
static const int BUTTON_INPUT_PIN = BUTTON_PIN_RIGHT;
#else
static const int BUTTON_INPUT_PIN = BUTTON_PIN;
#endif

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
#ifdef BOARD_AIPI_LITE
  pinMode(AIPI_POWER_KEEPALIVE_PIN, OUTPUT);
  digitalWrite(AIPI_POWER_KEEPALIVE_PIN, HIGH);
#endif

  TEST_SERIAL.begin(115200);
  uint32_t serial_wait_start = millis();
  while (!TEST_SERIAL && (millis() - serial_wait_start < 3000)) {
    delay(10);
  }

  TEST_SERIAL.println();
  TEST_SERIAL.println("Test 02: Button input test");
  TEST_SERIAL.println("[stage] setup: before display.init");
  display.init(TFT_DIRECTION);
  TEST_SERIAL.println("[stage] setup: after display.init");
  button.init();
  TEST_SERIAL.println("[stage] setup: after button.init");
  display.showBootInstructions("Test 02: button");
  display.displayLine1("Press the board button.");
  display.displayLine2("Waiting for input...");
}

void loop() {
  button.key_scan();
  const int state = button.get_button_state();
  const int key = button.get_button_key_value();
#ifdef BOARD_AIPI_LITE
  const int raw = digitalRead(BUTTON_INPUT_PIN);
#else
  const int raw = analogRead(BUTTON_INPUT_PIN);
#endif

  if (state == Button::KEY_STATE_PRESSED &&
      last_button_state != Button::KEY_STATE_PRESSED) {
    press_count++;
    TEST_SERIAL.printf("Button press %lu, key=%d, raw=%d\r\n",
      (unsigned long)press_count, key, raw);
  }
  last_button_state = state;

  if (millis() - last_refresh_ms >= 100) {
    last_refresh_ms = millis();

    char line1[96];
    char line2[96];
    snprintf(line1, sizeof(line1), "Presses: %lu  State: %s",
      (unsigned long)press_count, button_state_name(state));
#ifdef BOARD_AIPI_LITE
    snprintf(line2, sizeof(line2), "Key: %d  GPIO: %d", key, raw);
#else
    snprintf(line2, sizeof(line2), "Key: %d  Raw ADC: %d", key, raw);
#endif

    display.displayLine1(line1);
    display.displayLine2(line2);
  }

  display.routine();
  delay(10);
}
