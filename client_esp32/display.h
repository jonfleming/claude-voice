#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "board_pins.h"
#include "lvgl.h"
#include "driver_button.h"

// Board-specific display driver selection
#ifdef BOARD_WAVESHARE_AMOLED
#include <Arduino_GFX_Library.h>
#else
#include "TFT_eSPI.h"
#endif

// BUTTON_PIN is defined in board_pins.h. TFT pin macros come from TFT_eSPI.
#ifdef BOARD_AIPI_LITE
#define TFT_DIRECTION 3           // AIPI Lite landscape orientation
#elif defined(BOARD_WAVESHARE_AMOLED)
#define TFT_DIRECTION 1           // Waveshare landscape orientation
#else
#define TFT_DIRECTION 1           // Freenove landscape orientation
#endif

extern lv_indev_t* indev_keypad;  // External declaration of the keypad input device

#if defined(BOARD_WAVESHARE_AMOLED)
extern lv_indev_t *touch_indev;     // LVGL touch input device
extern bool touch_input_registered; // Whether touch input was registered
#endif

// Display class handles LVGL integration for all board types
class Display {
private:
    int tft_show_dirction;  // Non-static member variable for display direction

public:
    // Function to initialize the display
    // Pointers to LVGL objects we create so we can update/remove them later
    lv_obj_t* boot_label = nullptr;
    lv_obj_t* line1_label = nullptr;
    lv_obj_t* line2_label = nullptr;

    void init(int screenDir);

    // Function to handle routine display tasks
    void routine();

    // Show a small instruction label at the top of the screen
    void showBootInstructions(const char* text);

    // Show/hide the small boot instruction banner
    void hideBootInstructions();

    // Display transcription text with word-wrapping (no scrolling).
    void displayLine1(const char* text);
    void displayLine2(const char* text);
    void clearLines();

    // Getter for tft_show_dirction
    int getTftShowDirection() const { return tft_show_dirction; }

    // Setter for tft_show_dirction
    void setTftShowDirection(int direction) { tft_show_dirction = direction; }
};
// Global display instance (defined in display.cpp)
extern Display display;

#endif
