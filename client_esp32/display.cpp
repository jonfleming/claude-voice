#include <TFT_eSPI.h>
#include <SPI.h>

#include "display.h"

#ifndef BUTTON_PIN
#define BUTTON_PIN 19   // Freenove default; overridden by board_pins.h for other boards
#endif

#ifndef DISPLAY_DEBUG_SERIAL
#define DISPLAY_DEBUG_SERIAL Serial
#endif

lv_indev_t *indev_keypad; // External declaration of the keypad input device

// Define screen dimensions
#ifdef FNK0102A_1P14_135x240_ST7789
static const uint16_t screenWidth = 135;
static const uint16_t screenHeight = 240;
#elif defined AIPI_LITE_128x128_ST7735
static const uint16_t screenWidth = 128;
static const uint16_t screenHeight = 128;
#else // Default FNK0102B_3P5_320x480_ST7796
static const uint16_t screenWidth = 320;
static const uint16_t screenHeight = 480;
#endif

// Buffer for drawing
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * screenHeight / 5];

// TFT instance
TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);

// Display instance
Display display;

namespace {

int get_logical_screen_width()
{
  lv_disp_t *disp = lv_disp_get_default();
  if (!disp) {
    return screenWidth;
  }
  return lv_disp_get_hor_res(disp);
}

void log(const char* object, const char* message) {
  DISPLAY_DEBUG_SERIAL.printf("[display] %s: %s\n", object, message);
}

void layout_display_labels(Display &display_instance)
{
  const int logical_width = get_logical_screen_width();
  const int margin = 12;
  const int content_width = logical_width > (margin * 2) ? logical_width - (margin * 2) : logical_width;

  if (display_instance.boot_label) {
    lv_obj_set_width(display_instance.boot_label, logical_width);
    lv_obj_align(display_instance.boot_label, LV_ALIGN_TOP_MID, 0, 6);
  }

  if (display_instance.line1_label) {
    lv_obj_set_width(display_instance.line1_label, content_width);
    if (display_instance.boot_label) {
      lv_obj_align_to(display_instance.line1_label, display_instance.boot_label,
        LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    } else {
      lv_obj_align(display_instance.line1_label, LV_ALIGN_TOP_MID, 0, 12);
    }
  }

  if (display_instance.line2_label) {
    lv_obj_set_width(display_instance.line2_label, content_width);
    if (display_instance.line1_label) {
      lv_obj_align_to(display_instance.line2_label, display_instance.line1_label,
        LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    } else if (display_instance.boot_label) {
      lv_obj_align_to(display_instance.line2_label, display_instance.boot_label,
        LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    } else {
      lv_obj_align(display_instance.line2_label, LV_ALIGN_TOP_MID, 0, 12);
    }
  }

  lv_obj_t *active_screen = lv_scr_act();
  if (active_screen) {
    lv_obj_invalidate(active_screen);
  }
}

} // namespace

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char *buf)
{
  DISPLAY_DEBUG_SERIAL.printf(buf); // Print the buffer to the serial monitor
  DISPLAY_DEBUG_SERIAL.flush();     // Ensure all data is sent
}
#endif

// Display flush function
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1); // Calculate width of the area to flush
  uint32_t h = (area->y2 - area->y1 + 1); // Calculate height of the area to flush

  tft.startWrite();                                        // Start writing to the TFT
  tft.setAddrWindow(area->x1, area->y1, w, h);             // Set the address window for writing
  tft.pushColors((uint16_t *)&color_p->full, w * h, true); // Push colors to the TFT
  tft.endWrite();                                          // End writing to the TFT
  lv_disp_flush_ready(disp);                               // Inform LVGL that flushing is complete
}

// Keypad read function
#ifndef BOARD_AIPI_LITE
void my_keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  static int last_key = 0; // Static variable to store the last key value

  button.key_scan();                           // Scan the button state
  int buttonState = button.get_button_state(); // Get the current button state
  int act_key = button.get_button_key_value(); // Get the current button key value

  // Update the state based on the button state
  switch (buttonState)
  {
  case Button::KEY_STATE_PRESSED:
    data->state = LV_INDEV_STATE_PR; // Button is pressed
    break;
  case Button::KEY_STATE_RELEASED:
    data->state = LV_INDEV_STATE_REL; // Button is released
    break;
  }

  // Map button key values to LVGL key codes
  switch (act_key)
  {
  case 1:
    act_key = LV_KEY_ENTER; // Map to Enter key
    break;
  case 2:
    if (display.getTftShowDirection() == 0)
      act_key = LV_KEY_PREV;
    else if (display.getTftShowDirection() == 1)
      act_key = LV_KEY_LEFT;
    else if (display.getTftShowDirection() == 2)
      act_key = LV_KEY_NEXT;
    else if (display.getTftShowDirection() == 3)
      act_key = LV_KEY_RIGHT;
    break;
  case 3:
    if (display.getTftShowDirection() == 0)
      act_key = LV_KEY_NEXT;
    else if (display.getTftShowDirection() == 1)
      act_key = LV_KEY_RIGHT;
    else if (display.getTftShowDirection() == 2)
      act_key = LV_KEY_PREV;
    else if (display.getTftShowDirection() == 3)
      act_key = LV_KEY_LEFT;
    break;
  case 4:
    if (display.getTftShowDirection() == 0)
      act_key = LV_KEY_LEFT;
    else if (display.getTftShowDirection() == 1)
      act_key = LV_KEY_NEXT;
    else if (display.getTftShowDirection() == 2)
      act_key = LV_KEY_RIGHT;
    else if (display.getTftShowDirection() == 3)
      act_key = LV_KEY_PREV;
    break;
  case 5:
    if (display.getTftShowDirection() == 0)
      act_key = LV_KEY_RIGHT;
    else if (display.getTftShowDirection() == 1)
      act_key = LV_KEY_PREV;
    else if (display.getTftShowDirection() == 2)
      act_key = LV_KEY_LEFT;
    else if (display.getTftShowDirection() == 3)
      act_key = LV_KEY_NEXT;
    break;
  default:
    break;
  }
  last_key = act_key;   // Update the last key value
  data->key = last_key; // Set the key value in the input device data
}
#endif

void tftRst(void) {
  log("tftRst", "Backlight reset sequence start");
  String logMsg = "TFT_BL=" + String(TFT_BL);
  log("tftRst", logMsg.c_str());
  pinMode(TFT_BL, OUTPUT);
  log("tftRst", "Backlight pin set to OUTPUT");
  digitalWrite(TFT_BL, LOW);
  log("tftRst", "Backlight turned OFF");
  delay(50);
  digitalWrite(TFT_BL, HIGH);
  log("tftRst", "Backlight turned ON");
  delay(50);
}

// Setup the TFT display
void setupTFT(int direction)
{
  log("setupTFT", "Setting up TFT display");
  tftRst();
  log("setupTFT", "TFT reset complete");
  display.setTftShowDirection(direction);
  log("setupTFT", "tft_show_direction set");

#ifdef AIPI_LITE_128x128_ST7735
  // TFT_eSPI initializes the selected ESP32-S3 SPI port for the AIPI ST7735.
  // Calling the global SPI object here can hang before the driver gets control.
  log("setupTFT", "SPI.begin skipped for AIPI");
#else
  // Explicitly initialize SPI pins before TFT_eSPI driver init on ESP32-S3.
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  log("setupTFT", "SPI.begin complete");
#endif

  tft.begin();                                    // Initialize the TFT
  log("setupTFT", "TFT initialization complete");
  tft.setRotation(display.getTftShowDirection()); // Set the rotation of the TFT using the tft_show_dirction macro
  log("setupTFT", "TFT rotation set");
}

// Setup LVGL
void setupLVGL()
{
#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print); // Register the print function for debugging
#endif
  lv_init(); // Initialize LVGL

  // Initialize the display buffer
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 5);

  // Initialize the display driver
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);

  // Set the resolution based on the TFT direction
  switch (display.getTftShowDirection())
  {
  case 0: // Normal orientation
  case 2: // 180 degree rotation
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    break;
  case 1: // 90 degree rotation
  case 3: // 270 degree rotation
    disp_drv.hor_res = screenHeight;
    disp_drv.ver_res = screenWidth;
    break;
  default:
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    break;
  }

  disp_drv.flush_cb = my_disp_flush; // Set the flush callback
  disp_drv.draw_buf = &draw_buf;     // Set the draw buffer
  lv_disp_drv_register(&disp_drv);   // Register the display driver

  // Initialize the input device driver
#ifndef BOARD_AIPI_LITE
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_KEYPAD;            // Set the input device type to keypad
  indev_drv.read_cb = my_keypad_read;               // Set the read callback
  indev_keypad = lv_indev_drv_register(&indev_drv); // Register the input device driver
#else
  indev_keypad = nullptr;
#endif
}

// Initialize the display
void Display::init(int screenDir)
{
  setupTFT(screenDir); // Setup the TFT display
  setupLVGL();         // Setup LVGL
}

// Create a small instruction label at the top of the screen.
// Uses LVGL so the label is part of LV's object tree and will persist
// until you remove or hide it. Call after `Display::init()`.
void Display::showBootInstructions(const char* text)
{
  // If a boot label already exists, update text and make sure it's visible
  log("boot_label", text);
  if (boot_label) {
    lv_label_set_text(boot_label, text);
    lv_obj_clear_flag(boot_label, LV_OBJ_FLAG_HIDDEN);
    layout_display_labels(*this);
    return;
  }

  // Create a label on the active screen and save the pointer
  lv_obj_t *active_screen = lv_scr_act();
  if (!active_screen) {
    log("boot_label", "LVGL active screen is null");
    return;
  }

  boot_label = lv_label_create(active_screen);
  lv_label_set_long_mode(boot_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(boot_label, text);
  lv_obj_set_style_text_align(boot_label, LV_TEXT_ALIGN_CENTER, 0);
  layout_display_labels(*this);
}

void Display::hideBootInstructions()
{
  log("boot_label", "(Hide)");
  if (!boot_label) return;
  lv_obj_del(boot_label);
  boot_label = nullptr;
  layout_display_labels(*this);
}

void Display::displayLine1(const char* text)
{
  log("line1_label", text);

  // Remove existing transcription if present
  if (line1_label) {
    lv_label_set_text(line1_label, text);
    layout_display_labels(*this);
    return;
  }

  // Create label, enable wrapping, and position below the boot label (or near top)
  lv_obj_t *active_screen = lv_scr_act();
  if (!active_screen) {
    log("line1_label", "LVGL active screen is null");
    return;
  }

  line1_label = lv_label_create(active_screen);
  lv_label_set_long_mode(line1_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(line1_label, text);
  layout_display_labels(*this);
}

void Display::displayLine2(const char* text)
{
  log("line2_label", text);
  // Remove existing transcription if present
  if (line2_label) {
    lv_label_set_text(line2_label, text);
    layout_display_labels(*this);
    return;
  }

  // Create label, enable wrapping, and position below line1_label (or near top)
  lv_obj_t *active_screen = lv_scr_act();
  if (!active_screen) {
    log("line2_label", "LVGL active screen is null");
    return;
  }

  line2_label = lv_label_create(active_screen);
  lv_label_set_long_mode(line2_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(line2_label, text);
  layout_display_labels(*this);
}

void Display::clearLines()
{
  log("clearLines", "(Clear)");
  if (line1_label) {
    lv_obj_del(line1_label);
    line1_label = nullptr;
  }

  if (line2_label) {
    lv_obj_del(line2_label);
    line2_label = nullptr;
  }

  layout_display_labels(*this);
}

// Handle routine display tasks
void Display::routine(void)
{
  static uint32_t last_tick_ms = 0;
  const uint32_t now_ms = millis();

  if (last_tick_ms == 0) {
    last_tick_ms = now_ms;
  }

  const uint32_t elapsed_ms = now_ms - last_tick_ms;
  if (elapsed_ms > 0) {
    lv_tick_inc(elapsed_ms);
    last_tick_ms = now_ms;
  }

  lv_task_handler(); // Handle LVGL tasks
}
