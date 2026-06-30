// Compile the shared button driver as a sketch source unit.
#include "sketch_config.h"
#include "../client_esp32/board_pins.h"
#ifdef BOARD_AIPI_LITE
#define AUDIO_OUTPUT_DEBUG_SERIAL Serial
#else
#define AUDIO_OUTPUT_DEBUG_SERIAL Serial0
#endif
#include "../client_esp32/driver_audio_output.cpp"
