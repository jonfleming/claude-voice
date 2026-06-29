// Compile the shared button driver as a sketch source unit.
#include "sketch_config.h"
#include "../client_esp32/board_pins.h"
#include "../client_esp32/driver_button.cpp"

#ifdef BOARD_AIPI_LITE
Button button(BUTTON_PIN_RIGHT);
#else
Button button(BUTTON_PIN);
#endif
