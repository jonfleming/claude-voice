// Compile the shared display/LVGL stack as a sketch source unit.
// Load board selection before display.cpp includes TFT_eSPI.
// #define DISPLAY_DISABLE_KEYPAD_INPUT
#include "../client_esp32/board_pins.h"
#ifdef BOARD_AIPI_LITE
#define DISPLAY_DEBUG_SERIAL Serial
#else
#define DISPLAY_DEBUG_SERIAL Serial0
#endif
#include "../client_esp32/display.cpp"
