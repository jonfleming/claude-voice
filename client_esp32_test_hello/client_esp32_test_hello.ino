#include <lvgl.h>
#include <TFT_eSPI.h>   // Must be configured for ST7735 in User_Setup.h

// LVGL draw buffer: 1/10 screen size is typical
static const uint16_t SCREEN_WIDTH  = 128;
static const uint16_t SCREEN_HEIGHT = 128;
static const uint32_t DRAW_BUF_SIZE = (SCREEN_WIDTH * SCREEN_HEIGHT / 10);

static lv_color_t draw_buf_1[DRAW_BUF_SIZE];
static lv_color_t draw_buf_2[DRAW_BUF_SIZE];   // double buffer (optional but recommended)
static lv_disp_draw_buf_t lv_draw_buf;
static lv_disp_drv_t lv_disp_drv;

// Display instance
TFT_eSPI tft = TFT_eSPI();

void tftRst(void) {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  delay(50);
  digitalWrite(TFT_BL, HIGH);
  delay(50);
}
// ---- LVGL flush callback for TFT_eSPI ----
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *px_map) {
    // Set drawing window
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1,
                      area->x2 - area->x1 + 1,
                      area->y2 - area->y1 + 1);

    uint32_t size = lv_area_get_width(area) * lv_area_get_height(area);
    tft.pushColors((uint16_t *)px_map, size, true);
    tft.endWrite();

    // Tell LVGL flushing is finished
    lv_disp_flush_ready(disp);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    // Initialize TFT_eSPI
    tft.init();
    tft.setRotation(1);
    tftRst();

    Serial.println("Setup");
    // Initialize LVGL
    lv_init();
    lv_disp_draw_buf_init(&lv_draw_buf, draw_buf_1, draw_buf_2, DRAW_BUF_SIZE);

    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.hor_res = SCREEN_WIDTH;
    lv_disp_drv.ver_res = SCREEN_HEIGHT;
    lv_disp_drv.flush_cb = my_disp_flush;
    lv_disp_drv.draw_buf = &lv_draw_buf;

    // Register display driver with LVGL
    lv_disp_t *disp = lv_disp_drv_register(&lv_disp_drv);

    // Rotation (LVGL 8.x API) — optional
    lv_disp_set_rotation(disp, LV_DISP_ROT_90);

    // Simple label
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello LVGL!");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    Serial.println("Display Loop");
}

void loop() {
    static uint32_t last_ms = millis();
    uint32_t now_ms = millis();
    lv_tick_inc(now_ms - last_ms);
    last_ms = now_ms;

    lv_timer_handler();  // process LVGL tasks
    delay(5);
    Serial.print(".");
}
