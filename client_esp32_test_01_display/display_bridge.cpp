// Compile the shared display/LVGL stack as a sketch source unit.
// Force TFT_eSPI to use this sketch's local User_Setup.h first.
#define DISPLAY_DISABLE_KEYPAD_INPUT
#include "User_Setup.h"
#include "../client_esp32/display.cpp"
