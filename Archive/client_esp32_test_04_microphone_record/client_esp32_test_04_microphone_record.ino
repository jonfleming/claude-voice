/*
 * Test 04: Microphone/ADC recording test.
 *
 * Goal:
 *   Verify I2S microphone input, estimate signal level, and optionally play
 *   the captured audio back through the already-tested speaker path.
 *
 * Expected result:
 *   Press the button, or type "r" in the serial monitor, to record three
 *   seconds. Speak near the microphone. The display shows peak/RMS readings,
 *   then plays the captured audio back as a generated WAV.
 */

#include "sketch_config.h"

#include <Arduino.h>
#include <math.h>

#include "../client_esp32/board_pins.h"
#include "../client_esp32/display.h"
#include "../client_esp32/driver_audio_input.h"
#include "../client_esp32/driver_audio_output.h"
#include "../client_esp32/driver_button.h"

#ifdef BOARD_AIPI_LITE
#define TEST_SERIAL Serial
#else
#define TEST_SERIAL Serial0
#endif

volatile TaskHandle_t player_task_handle = NULL;

static const uint32_t SAMPLE_RATE = 16000;
static const uint32_t RECORD_SECONDS = 3;
static const uint32_t MAX_PCM_BYTES = SAMPLE_RATE * RECORD_SECONDS * sizeof(int16_t);
static const float MIC_CAPTURE_GAIN = 18.0f;

static int last_button_state = Button::KEY_STATE_IDLE;
static uint8_t* wav_buffer = NULL;
static size_t wav_buffer_size = 0;
static float g_input_dc_offset = 0.0f;

void write_u16_le(uint8_t* p, uint16_t value) {
  p[0] = value & 0xff;
  p[1] = (value >> 8) & 0xff;
}

void write_u32_le(uint8_t* p, uint32_t value) {
  p[0] = value & 0xff;
  p[1] = (value >> 8) & 0xff;
  p[2] = (value >> 16) & 0xff;
  p[3] = (value >> 24) & 0xff;
}

void write_wav_header(uint8_t* wav, uint32_t data_size) {
  memcpy(wav + 0, "RIFF", 4);
  write_u32_le(wav + 4, 36 + data_size);
  memcpy(wav + 8, "WAVE", 4);
  memcpy(wav + 12, "fmt ", 4);
  write_u32_le(wav + 16, 16);
  write_u16_le(wav + 20, 1);
  write_u16_le(wav + 22, 1);
  write_u32_le(wav + 24, SAMPLE_RATE);
  write_u32_le(wav + 28, SAMPLE_RATE * sizeof(int16_t));
  write_u16_le(wav + 32, sizeof(int16_t));
  write_u16_le(wav + 34, 16);
  memcpy(wav + 36, "data", 4);
  write_u32_le(wav + 40, data_size);
}

int16_t convert_i2s_frame_to_pcm16(const int32_t* frame) {
  const float alpha = 0.999f;

  // Mix both stereo lanes so we don't depend on board/channel mapping.
  const int32_t raw = frame[0] + frame[1];
  g_input_dc_offset = (alpha * g_input_dc_offset) + ((1.0f - alpha) * (float)raw);
  float filtered = (float)raw - g_input_dc_offset;

  // Match the proven conversion used in the main client path.
  float amplified = (filtered * MIC_CAPTURE_GAIN) / 65536.0f;

  if (amplified > 32767.0f) amplified = 32767.0f;
  if (amplified < -32768.0f) amplified = -32768.0f;
  return (int16_t)amplified;
}

void warmup_and_flush_input(uint32_t warmup_ms) {
  uint8_t flush_chunk[1024];
  const uint32_t start_ms = millis();
  while (millis() - start_ms < warmup_ms) {
    const int available = audio_input_get_iis_data_available();
    if (available > 0) {
      audio_input_read_iis_data((char*)flush_chunk, sizeof(flush_chunk));
      continue;
    }
    delay(2);
  }
}

void record_and_playback() {
  if (wav_buffer) {
    free(wav_buffer);
    wav_buffer = NULL;
    wav_buffer_size = 0;
  }

  wav_buffer_size = 44 + MAX_PCM_BYTES;
  wav_buffer = (uint8_t*)malloc(wav_buffer_size);
  if (!wav_buffer) {
    TEST_SERIAL.println("Failed to allocate recording WAV buffer.");
    display.displayLine1("Record failed.");
    display.displayLine2("No heap for WAV.");
    return;
  }

  write_wav_header(wav_buffer, MAX_PCM_BYTES);
  uint8_t* pcm_out = wav_buffer + 44;
  size_t pcm_written = 0;
  uint8_t input_chunk[1024];

  uint32_t samples_seen = 0;
  uint32_t peak = 0;
  double sum_squares = 0.0;

#ifdef BOARD_AIPI_LITE
  // Shared I2S bus: always start each recording from a fresh RX init.
  audio_input_deinit();
  delay(10);
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
#endif

  // Start each take with a fresh DC estimate and drop startup transients.
  g_input_dc_offset = 0.0f;
  warmup_and_flush_input(120);

  TEST_SERIAL.println("Recording...");
  display.displayLine1("Recording for 3 seconds...");
  display.displayLine2("Speak near the microphone.");

  const uint32_t start_ms = millis();
  while (millis() - start_ms < RECORD_SECONDS * 1000UL &&
         pcm_written < MAX_PCM_BYTES) {
    const int available = audio_input_get_iis_data_available();
    if (available <= 0) {
      display.routine();
      delay(2);
      continue;
    }

    const size_t read_size =
        audio_input_read_iis_data((char*)input_chunk, sizeof(input_chunk));
    const int32_t* frames = (const int32_t*)input_chunk;
    const size_t frame_count = read_size / 8;

    for (size_t i = 0; i < frame_count && pcm_written < MAX_PCM_BYTES; i++) {
      const int16_t sample = convert_i2s_frame_to_pcm16(frames + (i * 2));
      ((int16_t*)pcm_out)[pcm_written / 2] = sample;
      pcm_written += sizeof(int16_t);

      const uint32_t magnitude = abs((int)sample);
      if (magnitude > peak) peak = magnitude;
      sum_squares += (double)sample * (double)sample;
      samples_seen++;
    }

    if (samples_seen > 0 && samples_seen % 4096 == 0) {
      const double rms = sqrt(sum_squares / samples_seen);
      char line2[96];
      snprintf(line2, sizeof(line2), "Peak: %lu  RMS: %.0f",
               (unsigned long)peak, rms);
      display.displayLine2(line2);
    }
    display.routine();
  }

  write_wav_header(wav_buffer, pcm_written);
  wav_buffer_size = 44 + pcm_written;

  const double rms = samples_seen > 0 ? sqrt(sum_squares / samples_seen) : 0.0;
  TEST_SERIAL.printf("Recording complete: bytes=%u, peak=%lu, rms=%.0f\r\n",
                     (unsigned)wav_buffer_size, (unsigned long)peak, rms);

  char line1[96];
  char line2[96];
  snprintf(line1, sizeof(line1), "Recorded %u bytes.", (unsigned)pcm_written);
  snprintf(line2, sizeof(line2), "Peak: %lu  RMS: %.0f", (unsigned long)peak,
           rms);
  display.displayLine1(line1);
  display.displayLine2(line2);
  delay(700);

  TEST_SERIAL.println("Playing recording back...");
  display.displayLine1("Playing recording back...");
  display.displayLine2("Listen for your voice.");
#ifdef BOARD_AIPI_LITE
  // AIPI-Lite: mic and speaker share the same I2S peripheral (MCLK/BCLK/WS
  // pins are identical). Stop and uninstall the mic driver before switching
  // the bus to TX-only mode for playback; otherwise i2s_output_wav() tries to
  // reconfigure a port that the mic driver still owns and the result is noise.
  audio_input_deinit();
  delay(10); // let the driver flush the RX buffer
  digitalWrite(SPEAKER_AMP_ENABLE, HIGH);
  delay(10);
#endif
  player_task_handle = (TaskHandle_t)1;
  i2s_output_wav(wav_buffer, wav_buffer_size);
  player_task_handle = NULL;
#ifdef BOARD_AIPI_LITE
  delay(20);
  digitalWrite(SPEAKER_AMP_ENABLE, LOW);
  // Re-initialize the mic driver so the next record-and-playback cycle works.
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
#endif

  display.displayLine1("Microphone test complete.");
  display.displayLine2("Press button or send r.");
}

void setup() {
#ifdef BOARD_AIPI_LITE
  pinMode(AIPI_POWER_KEEPALIVE_PIN, OUTPUT);
  digitalWrite(AIPI_POWER_KEEPALIVE_PIN, HIGH);
  pinMode(SPEAKER_AMP_ENABLE, OUTPUT);
  digitalWrite(SPEAKER_AMP_ENABLE, LOW);
#endif

  TEST_SERIAL.begin(115200);
  uint32_t serial_wait_start = millis();
  while (!TEST_SERIAL && (millis() - serial_wait_start < 3000)) {
    delay(10);
  }

  TEST_SERIAL.println();
  TEST_SERIAL.println("Test 04: Microphone recording test");
  TEST_SERIAL.println("[stage] setup: before display.init");
  display.init(TFT_DIRECTION);
  TEST_SERIAL.println("[stage] setup: after display.init");
  button.init();
  TEST_SERIAL.println("[stage] setup: after button.init");
  display.showBootInstructions("Test 04: microphone");
  display.displayLine1("Press button to record.");
  display.displayLine2("Or send r over serial.");

#ifdef BOARD_AIPI_LITE
  if (!audio_output_codec_init()) {
    display.displayLine1("ES8311 init failed.");
    TEST_SERIAL.println("ES8311 init failed.");
  }
  if (!i2s_output_init_mclk(AUDIO_OUTPUT_MCLK, AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT)) {
    display.displayLine1("I2S output init failed.");
    TEST_SERIAL.println("I2S output init mlk failed.");
  }
  TEST_SERIAL.println("Setting volume to 20");    
  audio_output_set_volume(20);
  // Leave the shared I2S peripheral in RX mode before the first capture.
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
#else                              
  audio_input_init(AUDIO_INPUT_SCK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
  if (!i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT)) {
    display.displayLine1("I2S output init failed.");
    TEST_SERIAL.println("I2S output init failed.");    
  }
  TEST_SERIAL.println("Setting volume to 20");    
  audio_output_set_volume(20);
#endif    
}

void loop() {
  button.key_scan();
  const int state = button.get_button_state();
  if (state == Button::KEY_STATE_PRESSED &&
      last_button_state != Button::KEY_STATE_PRESSED) {
    record_and_playback();
  }
  last_button_state = state;

  if (TEST_SERIAL.available()) {
    const char c = TEST_SERIAL.read();
    if (c == 'r' || c == 'R') {
      record_and_playback();
    }
  }

  display.routine();
  delay(10);
}
