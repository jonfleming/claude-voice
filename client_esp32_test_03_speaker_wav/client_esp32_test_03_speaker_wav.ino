/*
 * Test 03: Speaker/DAC/amplifier WAV playback test.
 *
 * Goal:
 *   Verify I2S audio output on the Freenove ESP32-S3 Media Kit.
 *
 * Expected result:
 *   Press the button, or type "p" in the serial monitor, to play a short
 *   generated 16 kHz mono WAV tone through the device speaker path.
 */
#include "sketch_config.h"

#include <Arduino.h>
#include <math.h>

#include "../client_esp32/board_pins.h"
#include "../client_esp32/driver_button.h"
#include "../client_esp32/display.h"
#include "../client_esp32/driver_audio_output.h"

#ifdef BOARD_AIPI_LITE
#define TEST_SERIAL Serial
#else
#define TEST_SERIAL Serial0
#endif

volatile TaskHandle_t player_task_handle = NULL;

static int last_button_state = Button::KEY_STATE_IDLE;
static uint32_t play_count = 0;

void write_u16_le(uint8_t *p, uint16_t value) {
  p[0] = value & 0xff;
  p[1] = (value >> 8) & 0xff;
}

void write_u32_le(uint8_t *p, uint32_t value) {
  p[0] = value & 0xff;
  p[1] = (value >> 8) & 0xff;
  p[2] = (value >> 16) & 0xff;
  p[3] = (value >> 24) & 0xff;
}

uint8_t *build_test_wav(size_t *wav_size) {
  const uint32_t sample_rate = 16000;
  const uint16_t channels = 1;
  const uint16_t bits_per_sample = 16;
  const uint32_t duration_ms = 900;
  const uint32_t sample_count = (sample_rate * duration_ms) / 1000;
  const uint32_t data_size = sample_count * channels * (bits_per_sample / 8);
  const uint32_t total_size = 44 + data_size;

  uint8_t *wav = (uint8_t *)malloc(total_size);
  if (!wav) {
    return NULL;
  }

  memcpy(wav + 0, "RIFF", 4);
  write_u32_le(wav + 4, 36 + data_size);
  memcpy(wav + 8, "WAVE", 4);
  memcpy(wav + 12, "fmt ", 4);
  write_u32_le(wav + 16, 16);
  write_u16_le(wav + 20, 1);
  write_u16_le(wav + 22, channels);
  write_u32_le(wav + 24, sample_rate);
  write_u32_le(wav + 28, sample_rate * channels * (bits_per_sample / 8));
  write_u16_le(wav + 32, channels * (bits_per_sample / 8));
  write_u16_le(wav + 34, bits_per_sample);
  memcpy(wav + 36, "data", 4);
  write_u32_le(wav + 40, data_size);

  int16_t *samples = (int16_t *)(wav + 44);
  for (uint32_t i = 0; i < sample_count; i++) {
    const float t = (float)i / (float)sample_rate;
    const float envelope = (i < 800) ? (float)i / 800.0f :
      (i > sample_count - 800 ? (float)(sample_count - i) / 800.0f : 1.0f);
    samples[i] = (int16_t)(sinf(2.0f * PI * 660.0f * t) * 12000.0f * envelope);
  }

  *wav_size = total_size;
  return wav;
}

void play_test_wav() {
  size_t wav_size = 0;
  uint8_t *wav = build_test_wav(&wav_size);
  if (!wav) {
    TEST_SERIAL.println("Failed to allocate generated WAV buffer.");
    display.displayLine1("Playback failed.");
    display.displayLine2("No heap for WAV.");
    return;
  }

  play_count++;
  TEST_SERIAL.printf("Playing generated WAV %lu, bytes=%u\r\n",
    (unsigned long)play_count, (unsigned)wav_size);
  display.displayLine1("Playing generated WAV...");
  display.displayLine2("660 Hz, 16 kHz mono");

  player_task_handle = (TaskHandle_t)1;
  i2s_output_wav(wav, wav_size);
  player_task_handle = NULL;

  free(wav);
  display.displayLine1("Playback finished.");
  display.displayLine2("Press button or send p.");
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
  TEST_SERIAL.println("Test 03: Speaker WAV playback test");
  TEST_SERIAL.println("[stage] setup: before display.init");
  display.init(TFT_DIRECTION);
  TEST_SERIAL.println("[stage] setup: after display.init");
  button.init();
  TEST_SERIAL.println("[stage] setup: after button.init");
  display.showBootInstructions("Test 03: speaker WAV");
  display.displayLine1("Press button to play.");
  display.displayLine2("Or send p over serial.");

  if (!i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT)) {
    display.displayLine1("I2S output init failed.");
    TEST_SERIAL.println("I2S output init failed.");
  }
  audio_output_set_volume(10);
}

void loop() {
  button.key_scan();
  const int state = button.get_button_state();
  if (state == Button::KEY_STATE_PRESSED &&
      last_button_state != Button::KEY_STATE_PRESSED) {
    play_test_wav();
  }
  last_button_state = state;

  if (TEST_SERIAL.available()) {
    const char c = TEST_SERIAL.read();
    if (c == 'p' || c == 'P') {
      play_test_wav();
    }
  }

  display.routine();
  delay(10);
}
