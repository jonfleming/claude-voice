#include <SPI.h>

#include "display.h"

#ifndef BUTTON_PIN
#define BUTTON_PIN 19   // Freenove default; overridden by board_pins.h for other boards
#endif

#ifndef DISPLAY_DEBUG_SERIAL
#define DISPLAY_DEBUG_SERIAL Serial1
#endif

lv_indev_t *indev_keypad; // External declaration of the keypad input device

// =============================================================================
// Screen dimensions per board
// =============================================================================
#ifdef BOARD_WAVESHARE_AMOLED
static const uint16_t screenWidth = LCD_WIDTH;   // 368
static const uint16_t screenHeight = LCD_HEIGHT;  // 448
#elif defined(FNK0102A_1P14_135x240_ST7789)
static const uint16_t screenWidth = 135;
static const uint16_t screenHeight = 240;
#elif defined(AIPI_LITE_128x128_ST7735)
static const uint16_t screenWidth = 128;
static const uint16_t screenHeight = 128;
#else // Default Freenove ST7796
static const uint16_t screenWidth = 320;
static const uint16_t screenHeight = 480;
#endif

// LVGL draw buffer (LVGL 9.x uses uint8_t buffers)
static lv_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * screenHeight / 5];

// =============================================================================
// Board-specific display driver instances
// =============================================================================
#ifdef BOARD_WAVESHARE_AMOLED
// Waveshare: QSPI AMOLED via Arduino_GFX
static Arduino_ESP32QSPI* s_gfx_bus = nullptr;
static Arduino_SH8601* s_gfx = nullptr;
#else
// Freenove / AIPI-Lite: TFT_eSPI
TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight);
#endif

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
  DISPLAY_DEBUG_SERIAL.printf(buf);
  DISPLAY_DEBUG_SERIAL.flush();
}
#endif

// =============================================================================
// LVGL 9.x flush callback — board-specific implementations
// =============================================================================

#ifdef BOARD_WAVESHARE_AMOLED

// Waveshare QSPI AMOLED flush using Arduino_SH8601
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  // SH8601 writeRect: set address window and write pixel data in one call
  s_gfx->startWrite();
  s_gfx->writeAddrWindow(area->x1, area->y1, w, h);
  s_gfx->writePixels((uint16_t *)px_map, w * h);
  s_gfx->endWrite();

  lv_display_flush_ready(disp);
}

#else

// Freenove / AIPI-Lite flush using TFT_eSPI
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px_map, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

#endif

// Keypad read function (LVGL 9.x API)
#ifndef BOARD_AIPI_LITE
void my_keypad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
  static int last_key = 0;

  button.key_scan();
  int buttonState = button.get_button_state();
  int act_key = button.get_button_key_value();

  switch (buttonState)
  {
  case Button::KEY_STATE_PRESSED:
    data->state = LV_INDEV_STATE_PRESSED;
    break;
  case Button::KEY_STATE_RELEASED:
    data->state = LV_INDEV_STATE_RELEASED;
    break;
  }

  switch (act_key)
  {
  case 1:
    act_key = LV_KEY_ENTER;
    break;
  case 2:
    if (display.getTftShowDirection() == 0) act_key = LV_KEY_PREV;
    else if (display.getTftShowDirection() == 1) act_key = LV_KEY_LEFT;
    else if (display.getTftShowDirection() == 2) act_key = LV_KEY_NEXT;
    else if (display.getTftShowDirection() == 3) act_key = LV_KEY_RIGHT;
    break;
  case 3:
    if (display.getTftShowDirection() == 0) act_key = LV_KEY_NEXT;
    else if (display.getTftShowDirection() == 1) act_key = LV_KEY_RIGHT;
    else if (display.getTftShowDirection() == 2) act_key = LV_KEY_PREV;
    else if (display.getTftShowDirection() == 3) act_key = LV_KEY_LEFT;
    break;
  case 4:
    if (display.getTftShowDirection() == 0) act_key = LV_KEY_LEFT;
    else if (display.getTftShowDirection() == 1) act_key = LV_KEY_NEXT;
    else if (display.getTftShowDirection() == 2) act_key = LV_KEY_RIGHT;
    else if (display.getTftShowDirection() == 3) act_key = LV_KEY_PREV;
    break;
  case 5:
    if (display.getTftShowDirection() == 0) act_key = LV_KEY_RIGHT;
    else if (display.getTftShowDirection() == 1) act_key = LV_KEY_PREV;
    else if (display.getTftShowDirection() == 2) act_key = LV_KEY_LEFT;
    else if (display.getTftShowDirection() == 3) act_key = LV_KEY_NEXT;
    break;
  default:
    break;
  }
  last_key = act_key;
  data->key = last_key;
}
#endif

// =============================================================================
// Board-specific TFT/display initialization
// =============================================================================

#ifdef BOARD_WAVESHARE_AMOLED

// Initialize Waveshare SH8601 QSPI AMOLED display
static bool init_waveshare_display() {
  DISPLAY_DEBUG_SERIAL.println("[display] Initializing SH8601 QSPI AMOLED...");

  // Create QSPI data bus: CS, SCLK, SDIO0-3
  s_gfx_bus = new Arduino_ESP32QSPI(
      LCD_CS, LCD_SCLK,
      LCD_SDIO0, LCD_SDIO1,
      LCD_SDIO2, LCD_SDIO3
  );

  // Create SH8601 display driver
  s_gfx = new Arduino_SH8601(s_gfx_bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);

  if (!s_gfx->begin()) {
    DISPLAY_DEBUG_SERIAL.println("[display] SH8601 begin() FAILED!");
    return false;
  }

  // Landscape orientation (rotation 1 = 90 degrees clockwise)
  s_gfx->setRotation(1);

  // Fill black
  s_gfx->fillScreen(RGB565_BLACK);

  // Set brightness
  s_gfx->setBrightness(255);

  DISPLAY_DEBUG_SERIAL.printf("[display] SH8601 initialized: %dx%d, rotation=%d\n",
      LCD_WIDTH, LCD_HEIGHT, s_gfx->getRotation());
  return true;
}

// Backlight reset — Waveshare AMOLED has no traditional backlight
static void tftRst(void) {
  log("tftRst", "Waveshare AMOLED: no traditional backlight");
}

#else

// Backlight reset for Freenove (TFT_BL controlled via GPIO)
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

#endif

// Setup the display
void setupTFT(int direction)
{
  log("setupTFT", "Setting up display");
  display.setTftShowDirection(direction);
  log("setupTFT", "tft_show_direction set");

#ifdef BOARD_WAVESHARE_AMOLED
  if (!init_waveshare_display()) {
    DISPLAY_DEBUG_SERIAL.println("[display] Waveshare display init FAILED!");
  }
#else
  tftRst();
  log("setupTFT", "TFT reset complete");

#ifdef AIPI_LITE_128x128_ST7735
  // TFT_eSPI initializes the selected ESP32-S3 SPI port for the AIPI ST7735.
  // Calling the global SPI object here can hang before the driver gets control.
  log("setupTFT", "SPI.begin skipped for AIPI");
#else
  // Explicitly initialize SPI pins before TFT_eSPI driver init on ESP32-S3.
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  log("setupTFT", "SPI.begin complete");
#endif

  tft.begin();
  log("setupTFT", "TFT initialization complete");
  tft.setRotation(display.getTftShowDirection());
  log("setupTFT", "TFT rotation set");
#endif
}

// Setup LVGL 9.x
void setupLVGL()
{
#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print);
#endif
  lv_init();

  // Create display with landscape orientation (hor=screenHeight, ver=screenWidth)
  lv_display_t *disp = lv_display_create(screenHeight, screenWidth);
  lv_display_set_default(disp);

  // Set color format to RGB565 (AMOLED displays use 16-bit color)
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  // Set the draw buffer
  lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Set the flush callback
  lv_display_set_flush_cb(disp, my_disp_flush);

  // Set rotation (Waveshare is already in landscape via setRotation(1))
  lv_display_set_rotation(disp, (lv_display_rotation_t)display.getTftShowDirection());

  // Initialize the input device driver (keypad)
#ifndef BOARD_AIPI_LITE
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(indev, my_keypad_read);
  indev_keypad = indev;
#else
  indev_keypad = nullptr;
#endif
}

// Initialize the display
void Display::init(int screenDir)
{
  setupTFT(screenDir);
  setupLVGL();
}

// Create a small instruction label at the top of the screen.
void Display::showBootInstructions(const char* text)
{
  log("boot_label", text);
  if (boot_label) {
    lv_label_set_text(boot_label, text);
    lv_obj_clear_flag(boot_label, LV_OBJ_FLAG_HIDDEN);
    layout_display_labels(*this);
    return;
  }

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

  if (line1_label) {
    lv_label_set_text(line1_label, text);
    layout_display_labels(*this);
    return;
  }

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
  if (line2_label) {
    lv_label_set_text(line2_label, text);
    layout_display_labels(*this);
    return;
  }

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

  lv_task_handler();
}
