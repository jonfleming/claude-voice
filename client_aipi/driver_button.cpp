#include "driver_button.h"

Button::Button(int pin): pin(pin) {
  // Initialize variables
  thresholdRange = 0;
  pinState = KEY_STATE_IDLE;
  lastPinState = KEY_STATE_IDLE;
  thisTimeButtonKeyValue = Volt_330;
  btnVolt = Volt_330;
}

void Button::init() {
  // AIPI-Lite: GPIO42 is a standard digital GPIO button (active-LOW)
  pinMode(pin, INPUT_PULLUP);  // GPIO42 will read LOW when pressed
}

void Button::set_voltage_thresholds(const int thresholds[6]) {
  for (int i = 0; i < 6; i++) {
    voltageThresholds[i] = thresholds[i];
  }
}

void Button::set_threshold_range(int range) {
  thresholdRange = range;
}

void Button::key_scan() {
  // AIPI-Lite: GPIO42 is active-LOW (pressed when read returns LOW)
  int buttonPressed = (digitalRead(pin) == LOW);  // 1 if pressed, 0 if not

  // Determine button state based on GPIO level
  btnVolt = buttonPressed ? Volt_000 : Volt_330;  // Volt_000 = button pressed, Volt_330 = idle

  if (lastPinState != pinState && pinState != KEY_STATE_IDLE) {
    lastPinState = pinState;
  }

  switch (pinState) {
    case KEY_STATE_IDLE:
      if (buttonPressed) {  // Button just pressed
        buttonTriggerTiming = millis();
        pinState = KEY_STATE_PRESSED_BOUNCE_TIME;
        thisTimeButtonKeyValue = Volt_000;
      }
      break;
    case KEY_STATE_PRESSED_BOUNCE_TIME:
      if (buttonPressed) {  // Still pressed
        if (millis() - buttonTriggerTiming > DEBOUNCE_TIME) {
          pinState = KEY_STATE_PRESSED;
        }
      } else {  // Already released (noise)
        pinState = KEY_STATE_IDLE;
      }
      break;
    case KEY_STATE_PRESSED:
      if (!buttonPressed) {  // Button released
        buttonFirstRelesseTiming = millis();
        pinState = KEY_STATE_RELEASE_BOUNCE_TIME;
      }
      break;
    case KEY_STATE_RELEASE_BOUNCE_TIME:
      if (!buttonPressed) {  // Still released
        if (millis() - buttonFirstRelesseTiming > DEBOUNCE_TIME) {
          pinState = KEY_STATE_RELEASED;
        }
      } else {  // Pressed again (noise)
        pinState = KEY_STATE_PRESSED;
      }
      break;
    case KEY_STATE_RELEASED:
      pinState = KEY_STATE_IDLE;
      break;
  }
}

int Button::get_button_key_value() {
  return static_cast<int>(thisTimeButtonKeyValue);
}

int Button::get_button_state() {
  return static_cast<int>(pinState);
}