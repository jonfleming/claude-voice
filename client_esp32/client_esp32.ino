/*
* Sketch_09_3_Reird_And_Play.ino
* This sketch records audio data from an audio input using the I2S bus, sends it to a server for transcription,
* receives the transcribed text, sends it to an AI model for generating a response, and then uses a TTS service
* to convert the response text back into speech, which is played through an audio output using I2S.
* 
* Author: Jon Fleming
* Date:   2026-03-30
*
* Board Selection:
*   Default build targets **Freenove** (FNK0102A/B).
*   To build for **AIPI Lite**: define `BOARD_AIPI_LITE`
*   (add -DBOARD_AIPI_LITE in build flags, or uncomment the define in board_pins.h).
*/
#include "sketch_config.h"

#include "client_esp32.h"
#include "board_pins.h"
#include "wifi_config.h"
#include <esp_heap_caps.h>
#include "driver_audio_input.h"
#include "driver_audio_output.h"
#include "driver_button.h"
#include <ArduinoWebsockets.h>
#include <mbedtls/base64.h>
#include <time.h>

// Wi-Fi Portal
#include <TinyPortal.h>

// ArduinoOTA
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
// HTTP
#include <HTTPClient.h>
#include <RemoteDebug.h>
// Display
#include "display.h"
#include <lvgl.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string>

using namespace websockets;

RemoteDebug Debug;

#ifdef BOARD_AIPI_LITE
#define APP_SERIAL Serial
#else
#define APP_SERIAL Serial0
#endif

#define DBG_PRINTLN(msg) do { APP_SERIAL.printf("[%8lu] ", millis()); APP_SERIAL.println(msg); Debug.printf("[%8lu] ", millis()); Debug.println(msg); } while(0)
#define DBG_PRINT(msg) do { APP_SERIAL.print(msg); Debug.print(msg); } while(0)
#define DBG_PRINTF(...) do { APP_SERIAL.printf(__VA_ARGS__); Debug.printf(__VA_ARGS__); } while(0)

#ifdef BOARD_AIPI_LITE
static const float CLIENT_VAD_ENERGY_THRESHOLD = 0.005f;
#else
static const float CLIENT_VAD_ENERGY_THRESHOLD = 0.07f;
#endif

// Mutex to protect display request buffers
SemaphoreHandle_t display_mutex = NULL;
SemaphoreHandle_t ws_mutex = NULL;

#define RECORDER_FOLDER ""

// Board-profile pin macros are defined in board_pins.h
// (uncomment BOARD_AIPI_LITE to switch from Freenove to AIPI Lite)
//
// Freenove pins:
//   BUTTON_PIN=19, AUDIO_INPUT_SCK=3, AUDIO_INPUT_WS=14, AUDIO_INPUT_DIN=46
//   AUDIO_OUTPUT_BCLK=42, AUDIO_OUTPUT_LRC=41, AUDIO_OUTPUT_DOUT=1
//
// AIPI-Lite pins:
//   BUTTON_PIN=42, AUDIO_INPUT_MCLK=6, AUDIO_INPUT_BCLK=14, AUDIO_INPUT_LRCLK=12, AUDIO_INPUT_DIN=13
//   AUDIO_OUTPUT_MCLK=6, AUDIO_OUTPUT_BCLK=14, AUDIO_OUTPUT_LRCLK=12, AUDIO_OUTPUT_DOUT=11
//   SPEAKER_AMP_ENABLE=9, POWER_KEEP_ALIVE_PIN=10
//
// Note: AUDIO_INPUT_WS is aliased to AUDIO_INPUT_BCLK for the ES8311 codec on AIPI-Lite.
// The audio_input_init() function maps its (sck, ws, din) params to (BCLK, LRCLK, DIN).

// Fallback local defines (only used for Freenove; AIPI-Lite pins come from board_pins.h)
#ifndef AUDIO_INPUT_SCK
#define AUDIO_INPUT_SCK 3
#endif
#ifndef AUDIO_INPUT_WS
#define AUDIO_INPUT_WS 14
#endif
#ifndef AUDIO_INPUT_DIN
#define AUDIO_INPUT_DIN 46
#endif
#ifndef AUDIO_OUTPUT_BCLK
#define AUDIO_OUTPUT_BCLK 42
#endif
#ifndef AUDIO_OUTPUT_LRC
#define AUDIO_OUTPUT_LRC 41
#endif
#ifndef AUDIO_OUTPUT_DOUT
#define AUDIO_OUTPUT_DOUT 1
#endif

// Define the size of PSRAM in bytes
#define MOLLOC_SIZE (4 * 1024 * 1024)
// ---------- WiFi / Server configuration (edit before upload) ----------
#define WIFI_SSID "FLEMING_2"
#define WIFI_PASS "90130762"
//#define WIFI_SSID "GL-SFT1200-3e1"
//#define WIFI_PASS "goodlife"
//#define WIFI_SSID "iJon"
//#define WIFI_PASS "source.code"
// mDNS hostname for OTA + RemoteDebug telnet (DNS labels: letters, digits, hyphen only)
static const char *DEVICE_HOSTNAME = "claude-voice-esp32";

// The server that runs your transcription/TTS services (*two* Tailnet Bridge)
//#define SERVER_HOST "192.168.8.145"
// Nimo connected to GL-SFT1200-3e1
#define SERVER_HOST "voice.fleming.ai"
#define CLAUDE_VOICE_WS_PORT 443
#define CLAUDE_VOICE_WS_PATH "/ws"

// Save wav data
uint8_t *wav_buffer;
// Size of the last recorded buffer stored in PSRAM
size_t last_recorded_size = 0;

volatile bool button_abort = false;

// Task handles for state control
// If handle is NULL, the task/feature is inactive; non-NULL means active
volatile TaskHandle_t recorder_task_handle = NULL;  // NULL = not recording, non-NULL = recording
volatile TaskHandle_t player_task_handle = NULL;    // NULL = not playing, non-NULL = playing

// Thread-safe display request buffers (background tasks must never call LVGL directly)
char display_line1_buf[128] = {0};
volatile bool display_line1_pending = false;
char display_line1_next_buf[128] = {0};
volatile bool display_line1_next_pending = false;
char display_line2_buf[128] = {0};
volatile bool display_line2_pending = false;

// Boot instruction requests
char display_boot_buf[128] = {0};
volatile bool display_boot_show_pending = false;
volatile bool display_boot_hide_pending = false;
// Request to clear both display lines (processed on main loop)
volatile bool display_clear_pending = false;

WebsocketsClient claude_ws_client;
volatile bool claude_ws_connected = false;
volatile bool claude_ws_connecting = false;
volatile bool claude_ws_config_pending = false;

// Coordinate backend "done" with actual audio playback completion.
volatile bool resume_recorder_after_response = false;
volatile unsigned long response_done_ms = 0;
volatile unsigned long last_audio_payload_ms = 0;
volatile bool response_done_received = false;
volatile bool response_audio_seen = false;
volatile bool response_audio_done_received = false;
volatile bool conversation_active = false;

#ifdef BOARD_AIPI_LITE
static const uint32_t POWER_BUTTON_LONG_PRESS_MS = 2000;
volatile bool power_off_in_progress = false;
bool left_button_last_pressed = false;
bool left_button_longpress_fired = false;
unsigned long left_button_press_start_ms = 0;
#endif

bool claude_ws_send_vad_config(float energy_threshold);
void abort_conversation_and_return_idle(bool show_boot_instructions = true);
void handle_left_power_button_events();

// Cert
const char VOICE_CA_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

bool sync_time() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  for (int i = 0; i < 20 && !getLocalTime(&timeinfo); i++) {
    delay(500);
  }
  if (!getLocalTime(&timeinfo)) {
    DBG_PRINTLN("[WiFi] NTP sync failed; TLS cert validation may fail");
    return false;
  }
  DBG_PRINTF("[WiFi] Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return true;
}
void request_showBootInstructions(const char *text) {
  if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
  strncpy(display_boot_buf, text, sizeof(display_boot_buf)-1);
  display_boot_buf[sizeof(display_boot_buf)-1] = '\0';
  display_boot_show_pending = true;
  display_boot_hide_pending = false;
  if (display_mutex) xSemaphoreGive(display_mutex);
}

void request_hideBootInstructions() {
  if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
  display_boot_hide_pending = true;
  display_boot_show_pending = false;
  if (display_mutex) xSemaphoreGive(display_mutex);
}

// Request to clear display lines from background tasks
void request_clear_lines() {
  if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
  display_clear_pending = true;
  if (display_mutex) xSemaphoreGive(display_mutex);
}

// Request a main-loop display update for line1
void request_display_line1(const char *text) {
  if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
  if (!display_line1_pending) {
    strncpy(display_line1_buf, text, sizeof(display_line1_buf)-1);
    display_line1_buf[sizeof(display_line1_buf)-1] = '\0';
    display_line1_pending = true;
  } else {
    // Keep one extra queued update so rapid state transitions don't drop line1.
    strncpy(display_line1_next_buf, text, sizeof(display_line1_next_buf)-1);
    display_line1_next_buf[sizeof(display_line1_next_buf)-1] = '\0';
    display_line1_next_pending = true;
  }
  if (display_mutex) xSemaphoreGive(display_mutex);
}

// Request a main-loop display update for line2
void request_display_line2(const char *text) {
  if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
  strncpy(display_line2_buf, text, sizeof(display_line2_buf)-1);
  display_line2_buf[sizeof(display_line2_buf)-1] = '\0';
  display_line2_pending = true;
  if (display_mutex) xSemaphoreGive(display_mutex);
}

bool claude_ws_send_stop() {
  if (!claude_ws_connected) return false;
  const char *msg = "{\"type\":\"stop\"}";
  bool ok = false;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  ok = claude_ws_client.send(msg);
  if (ws_mutex) xSemaphoreGive(ws_mutex);
  if (!ok) {
    DBG_PRINTLN("[WS] Failed to send stop control message");
    claude_ws_connected = false;
    return false;
  }
  DBG_PRINTLN("[WS] Sent stop control message");
  return true;
}

// Convert I2S input frames (32-bit stereo, 16kHz) to backend format
// (16-bit mono, 16kHz). This version adds robust DC removal and 
// higher software gain to significantly improve Whisper accuracy.
size_t convert_input_to_backend_pcm(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap) {
  if (!in || !out || in_len < 8) return 0;
  
  const int32_t *samples = (const int32_t *)in;
  size_t stereo_pairs = in_len / 8; // 2 channels * 4 bytes
  size_t out_samples = stereo_pairs;
  
  if (out_samples * sizeof(int16_t) > out_cap) {
    out_samples = out_cap / sizeof(int16_t);
  }

  // DC removal and Gain settings
  static float dc_offset = 0;
  const float alpha = 0.999f;
  const float gain = 12.0f; // ~22dB software gain for better accuracy

  int16_t *out16 = (int16_t *)out;

  for (size_t i = 0; i < out_samples; ++i) {
    // Sum L and R channels (handles mics on either channel)
    int32_t raw_sample = samples[i * 2] + samples[i * 2 + 1];
    
    // 1. Remove DC offset (High-pass filter)
    dc_offset = (alpha * dc_offset) + ((1.0f - alpha) * (float)raw_sample);
    float filtered = (float)raw_sample - dc_offset;
    
    // 2. Apply Gain and scale from 32-bit to 16-bit
    float amplified = (filtered * gain) / 65536.0f;
    
    // 3. Clamp and store
    if (amplified > 32767.0f) amplified = 32767.0f;
    else if (amplified < -32768.0f) amplified = -32768.0f;
    
    out16[i] = (int16_t)amplified;
  }
  return out_samples * sizeof(int16_t);
}

void play_backend_audio_base64(const String &b64_audio) {
  if (b64_audio.length() == 0) return;

  size_t decoded_len = 0;
  int len_rc = mbedtls_base64_decode(NULL, 0, &decoded_len,
    (const unsigned char *)b64_audio.c_str(), b64_audio.length());
  if (len_rc != 0 && len_rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
    DBG_PRINTF("[WS] base64 length decode failed: %d\n", len_rc);
    return;
  }
  if (decoded_len == 0) return;

  uint8_t *decoded = (uint8_t *)malloc(decoded_len);
  if (!decoded) {
    DBG_PRINTLN("[WS] Failed to allocate decoded audio buffer");
    return;
  }

  size_t out_len = 0;
  int dec_rc = mbedtls_base64_decode(decoded, decoded_len, &out_len,
    (const unsigned char *)b64_audio.c_str(), b64_audio.length());
  if (dec_rc != 0 || out_len == 0) {
    DBG_PRINTF("[WS] base64 decode failed: %d\n", dec_rc);
    free(decoded);
    return;
  }

  player_task_handle = (TaskHandle_t)1;

  // ============ AIPI-Lite Speaker Amp Enable ============
#ifdef BOARD_AIPI_LITE
  digitalWrite(SPEAKER_AMP_ENABLE, HIGH);  // Enable speaker amp
  delay(10);  // Brief delay for amp to stabilize
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#endif
  // =====================================================

  bool is_wav = out_len >= 12 &&
    decoded[0] == 'R' && decoded[1] == 'I' && decoded[2] == 'F' && decoded[3] == 'F' &&
    decoded[8] == 'W' && decoded[9] == 'A' && decoded[10] == 'V' && decoded[11] == 'E';

  if (is_wav) {
    i2s_output_wav(decoded, out_len);
  } else {
    if (i2s_output_stream_begin(16000, 16, 1)) {
      i2s_output_stream_write(decoded, out_len);
      delay(20);
      i2s_output_stream_end();
    } else {
      DBG_PRINTLN("[WS] Failed to initialize I2S stream for backend audio");
    }
  }

  // ============ AIPI-Lite Speaker Amp Disable ============
#ifdef BOARD_AIPI_LITE
  digitalWrite(SPEAKER_AMP_ENABLE, LOW);  // Disable speaker amp
  delay(10);
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#endif
  // ======================================================

  player_task_handle = NULL;
  free(decoded);
}

void handle_claude_ws_json(const String &json) {
  String type = extract_json_string_value(json, "type");
  if (type.length() == 0) {
    DBG_PRINTF("[WS] Non-typed message: %s\n", json.c_str());
    return;
  }

  // Ignore stale backend conversation messages when the user has explicitly
  // returned to idle boot state.
  if (!conversation_active &&
      (type == "text" || type == "response" || type == "audio" ||
       type == "stop_recording" || type == "transcribing" ||
       type == "done" || type == "audio_done")) {
    DBG_PRINTF("[WS] Ignoring '%s' while idle\n", type.c_str());
    return;
  }

  if (type == "text") {
    String text = extract_json_string_value(json, "content");
    text.trim();
    if (text.length() > 0) {
      DBG_PRINTLN("[WS] Transcription:");
      DBG_PRINTLN(text.c_str());
      request_display_line1(text.c_str());
      request_display_line2("Generating response...");
    }
  } else if (type == "response") {
    String token = extract_json_string_value(json, "content");
    
    if (token.length() > 0) {
      APP_SERIAL.print(token);
      request_display_line2(token.c_str());
    }
  } else if (type == "audio") {
    // We now prefer raw binary audio frames (handled in on_message) for efficiency.
    // Skip JSON-encoded audio to avoid double-playing.
    DBG_PRINTLN("[WS] Skipping JSON audio message (preferring binary)");
  } else if (type == "stop_recording") {
    DBG_PRINTLN("[WS] Server requested stop recording (VAD)");
    stop_recorder_task();
  } else if (type == "transcribing") {
    DBG_PRINTLN("[WS] Transcribing...");
    request_display_line1("Transcribing...");
    request_display_line2("");
  } else if (type == "done") {
    if (!button_abort && conversation_active) {
      DBG_PRINTLN("\n[WS] Response complete.");
      response_done_received = true;
      resume_recorder_after_response = true;
      response_done_ms = millis();
    }
  } else if (type == "audio_done") {
    if (!button_abort && conversation_active) {
      DBG_PRINTLN("[WS] Response audio complete.");
      response_audio_done_received = true;
      resume_recorder_after_response = true;
      // Drop a stale pending "Playing response..." update that may have been
      // queued just before audio_done arrived.
      if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
      if (display_line1_pending && strcmp(display_line1_buf, "Playing response...") == 0) {
        if (display_line1_next_pending) {
          strncpy(display_line1_buf, display_line1_next_buf, sizeof(display_line1_buf)-1);
          display_line1_buf[sizeof(display_line1_buf)-1] = '\0';
          display_line1_next_pending = false;
          display_line1_pending = true;
        } else {
          display_line1_pending = false;
        }
      }
      if (display_line1_next_pending && strcmp(display_line1_next_buf, "Playing response...") == 0) {
        display_line1_next_pending = false;
      }
      if (display_mutex) xSemaphoreGive(display_mutex);
    }
  } else if (type == "error") {
    String err = extract_json_string_value(json, "content");
    DBG_PRINTF("[WS] Backend error: %s\n", err.c_str());
    request_display_line1("Backend error");
    request_display_line2(err.c_str());
  } else if (type == "config_ack") {
    String threshold = extract_json_string_value(json, "energy_threshold");
    if (threshold.length() > 0) {
      DBG_PRINTF("[WS] Config ack energy_threshold=%s\n", threshold.c_str());
    } else {
      DBG_PRINTLN("[WS] Config ack received");
    }
  } else if (type == "pong") {
    DBG_PRINTLN("[WS] pong");
  } else {
    DBG_PRINTF("[WS] Unhandled message type '%s'\n", type.c_str());
  }
}

void claude_ws_on_message(WebsocketsMessage message) {
  if (message.isBinary()) {
    if (button_abort || !conversation_active) {
      DBG_PRINTLN("[WS] Received audio payload while idle/aborted; ignoring.");
      claude_ws_send_stop();
      return;
    }
    std::string payload = message.rawData();
    if (!payload.empty()) {
      DBG_PRINTF("[WS] Received binary audio payload: %u bytes\n", (unsigned)payload.size());
      response_audio_seen = true;
      last_audio_payload_ms = millis();
      if (!response_audio_done_received) {
        request_display_line1("Playing response...");
        request_display_line2("");
      }
      player_task_handle = (TaskHandle_t)1;

    #ifdef BOARD_AIPI_LITE
      // The ES8311 input/output paths share the same I2S/codec resources.
      // Release RX before reconfiguring TX for playback to avoid channel allocation failures.
      audio_input_deinit();
    #endif
      
      // ============ AIPI-Lite Speaker Amp Enable ============
#ifdef BOARD_AIPI_LITE
      digitalWrite(SPEAKER_AMP_ENABLE, HIGH);  // Enable speaker amp
      delay(10);  // Brief delay for amp to stabilize
#endif
      // =====================================================
      
      const uint8_t *data = (const uint8_t *)payload.data();
      size_t len = payload.size();

      bool is_wav = len >= 12 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E';

      if (is_wav) {
        i2s_output_wav((uint8_t *)data, len);
      } else {
        if (i2s_output_stream_begin(16000, 16, 1)) {
          i2s_output_stream_write(data, len);
          delay(20);
          i2s_output_stream_end();
        }
      }

      // ============ AIPI-Lite Speaker Amp Disable ============
#ifdef BOARD_AIPI_LITE
      digitalWrite(SPEAKER_AMP_ENABLE, LOW);  // Disable speaker amp
      delay(10);
      audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#endif
      // ======================================================

      player_task_handle = NULL;
    }

    return;
  }

  handle_claude_ws_json(message.data());
}

void claude_ws_on_event(WebsocketsEvent event, String data) {
  switch(event) {
    case WebsocketsEvent::ConnectionOpened: {
      claude_ws_connected = true;
      claude_ws_connecting = false;
      DBG_PRINTLN("[WS] Connection opened");
      // Defer config send to main loop to avoid taking ws_mutex re-entrantly
      // from within poll() callback context.
      claude_ws_config_pending = true;
      String text = "Connected: ";
      text += WiFi.localIP().toString();
      request_display_line2(text.c_str());
      break;
    }
    case WebsocketsEvent::ConnectionClosed: {
      claude_ws_connected = false;
      claude_ws_connecting = false;
      DBG_PRINTF("[WS] Connection closed: %s\n", data.c_str());
      request_display_line2("Disconnected");
      break;
    }
    case WebsocketsEvent::GotPing: {
      DBG_PRINTLN("[WS] ping");
      break;
    }
    case WebsocketsEvent::GotPong: {
      DBG_PRINTLN("[WS] pong event");
      break;
    }
  }
}

bool claude_ws_connect() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN("[WS] WiFi not connected; cannot connect websocket");
    return false;
  }
  if (claude_ws_connected || claude_ws_connecting) return claude_ws_connected;

  claude_ws_connecting = true;
  DBG_PRINTF("[WS] Connecting to %s:%d%s\n", SERVER_HOST, CLAUDE_VOICE_WS_PORT, CLAUDE_VOICE_WS_PATH);

  bool ok = false;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  
  claude_ws_client.setCACert(VOICE_CA_CERT);
  ok = claude_ws_client.connectSecure(SERVER_HOST, CLAUDE_VOICE_WS_PORT, CLAUDE_VOICE_WS_PATH);
  if (ws_mutex) xSemaphoreGive(ws_mutex);

  claude_ws_connected = ok;
  claude_ws_connecting = false;
  if (!ok) {
    DBG_PRINTF("[WS] Connection failed (WiFi status: %d)\n", WiFi.status());
    request_display_line2("WS connect failed");
  }
  return ok;
}

void claude_ws_poll() {
  if (!claude_ws_connected) return;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  bool still_ok = claude_ws_client.available();
  if (still_ok) {
    claude_ws_client.poll();
  }
  if (ws_mutex) xSemaphoreGive(ws_mutex);

  if (!still_ok) {
    claude_ws_connected = false;
    DBG_PRINTLN("[WS] Lost connection");
  }
}

bool claude_ws_send_audio_chunk(const uint8_t *pcm, size_t len) {
  if (!claude_ws_connected || !pcm || len == 0) return false;
  bool ok = false;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  ok = claude_ws_client.sendBinary((const char *)pcm, len);
  if (ws_mutex) xSemaphoreGive(ws_mutex);
  if (!ok) {
    DBG_PRINTLN("[WS] sendBinary failed");
    claude_ws_connected = false;
  } else {
    // Optional: show some activity
    static unsigned long last_chunk_print = 0;
    if (millis() - last_chunk_print > 1000) {
      last_chunk_print = millis();
      DBG_PRINTF("-");
    }
  }
  return ok;
}

bool claude_ws_send_transcribe() {
  if (!claude_ws_connected) return false;
  const char *msg = "{\"type\":\"transcribe\"}";
  bool ok = false;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  ok = claude_ws_client.send(msg);
  if (ws_mutex) xSemaphoreGive(ws_mutex);
  if (!ok) {
    DBG_PRINTLN("[WS] Failed to send transcribe control message");
    claude_ws_connected = false;
    return false;
  }
  DBG_PRINTLN("[WS] Sent transcribe control message");
  return true;
}

bool claude_ws_send_vad_config(float energy_threshold) {
  if (!claude_ws_connected) return false;
  char msg[96];
  snprintf(msg, sizeof(msg), "{\"type\":\"config\",\"energy_threshold\":%.5f}", energy_threshold);

  bool ok = false;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  ok = claude_ws_client.send(msg);
  if (ws_mutex) xSemaphoreGive(ws_mutex);

  if (!ok) {
    DBG_PRINTLN("[WS] Failed to send config message");
    claude_ws_connected = false;
    return false;
  }
  DBG_PRINTF("[WS] Sent config energy_threshold=%.5f\n", energy_threshold);
  return true;
}

// Track last debounced button state to detect edges
int last_button_state_for_toggle = Button::KEY_STATE_IDLE;

// ---- setup phase flags (processed in loop()) ---------------------
static bool setup_phase0_portal = false;
static bool setup_phase1_saved  = false;
static bool setup_phase2_wifi   = false;
static bool setup_phase3_ota    = false;
static bool setup_phase4_debug  = false;
static bool setup_phase5_ws     = false;
static bool setup_complete      = false;

// Setup function to initialize the hardware and software components
void setup() {
  // ============ AIPI-Lite Power Management (CRITICAL) ============
  // MUST set GPIO10 HIGH immediately to prevent bootloader power-down.
  // Only applies when BOARD_AIPI_LITE is defined.
#ifdef BOARD_AIPI_LITE
  pinMode(POWER_KEEP_ALIVE_PIN, OUTPUT);
  digitalWrite(POWER_KEEP_ALIVE_PIN, HIGH);  // Keep device powered on battery
  delay(10);  // Brief delay to stabilize power

  // Left button controls power latch behavior on AIPI-Lite.
  pinMode(BUTTON_PIN_LEFT, INPUT_PULLUP);

  // Speaker Amplifier Control (start disabled)
  pinMode(SPEAKER_AMP_ENABLE, OUTPUT);
  digitalWrite(SPEAKER_AMP_ENABLE, LOW);  // Amp disabled by default
#endif

  // Initialize the board-selected serial port at 115200 baud.
  APP_SERIAL.begin(115200);
  // Match test behavior: wait briefly for monitor, but continue for battery/standalone mode.
  uint32_t serial_wait_start = millis();
  while (!APP_SERIAL && (millis() - serial_wait_start < 3000)) {
    delay(10);
  }

  APP_SERIAL.println();
  APP_SERIAL.println("[Setup] Stage 1: serial ready");

#ifdef BOARD_AIPI_LITE
  // Battery Voltage Monitoring (optional)
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogSetAttenuation(ADC_11db);  // ~0-3.3V input range
  APP_SERIAL.println("[Setup] Stage 2: battery ADC configured");
#endif
  // ===============================================================

  APP_SERIAL.println("[Setup] Initializing...");
  
  // Display
  display.init(TFT_DIRECTION);

  // Button
  button.init();
  APP_SERIAL.println("[Setup] Button initialized");

  // Initialize the I2S bus for audio input
#ifdef BOARD_AIPI_LITE
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#else
  audio_input_init(AUDIO_INPUT_SCK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
#endif
  // Initialize the I2S bus for audio output
#ifdef BOARD_AIPI_LITE
  if (!audio_output_codec_init()) {
    APP_SERIAL.println("[Setup] ES8311 codec init failed");
  } else {
    APP_SERIAL.println("[Setup] ES8311 codec initialized");
  }
  if (!i2s_output_init_mclk(AUDIO_OUTPUT_MCLK, AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT)) {
    APP_SERIAL.println("[Setup] I2S output (MCLK) init failed");
  } else {
    APP_SERIAL.println("[Setup] I2S output (MCLK) initialized");
  }
  audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#else
  if (!i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT)) {
    APP_SERIAL.println("[Setup] I2S output init failed");
  } else {
    APP_SERIAL.println("[Setup] I2S output initialized");
  }
#endif
  // Set default volume to ~half (range 0-21)
  APP_SERIAL.println("[Setup] Setting volume to 10 (approx half)");
  audio_output_set_volume(10);

  // Create button handler task
  xTaskCreate(loop_task_button_handler, "button_handler", 4096, NULL, 2, NULL);

  // Create mutex for display buffer protection
  display_mutex = xSemaphoreCreateMutex();
  if (!display_mutex) {
    APP_SERIAL.println("[Setup] Warning: failed to create display mutex");
  }
  ws_mutex = xSemaphoreCreateMutex();
  if (!ws_mutex) {
    APP_SERIAL.println("[Setup] Warning: failed to create websocket mutex");
  }
  APP_SERIAL.println("[Setup] mutex initialized");
  
  // Load WiFi credentials from NVS (or sets flag to enter captive portal).
  // If no credentials exist, the captive portal is started in the loop-based
  // setup phase (after the WiFi stack is initialized).
  APP_SERIAL.println("[Setup] Starting WiFi configuration...");
  wifi_config_init();
  APP_SERIAL.println("[Setup] WiFi configuration initialized");

  
  // Transition to loop-based setup phase.
  if (wifi_config_ssid()[0] != '\0') {
    APP_SERIAL.printf("[Setup] Found WiFi credentials: SSID='%s'\n", wifi_config_ssid());
    // Credentials found — proceed to connect WiFi.
    setup_phase0_portal = true;
    setup_phase1_saved = true;
  } else {
    // No credentials — leave setup_phase0_portal as false so loop() Phase 0
    // actually starts the captive portal.  (Setting it true here would cause
    // loop() to skip Phase 0 because its guard is `if (!setup_phase0_portal)`.
    // setup_phase0_portal is already false by default.)
  }
}

/* Main recording task loop */
void loop_task_sound_recorder(void *pvParameters) {
  DBG_PRINTF("[Recorder] Task '%s' min free stack: %u bytes\n", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));
  DBG_PRINTLN("[Recorder] loop_task_sound_recorder start...");
  bool stop_requested = false;

  uint8_t input_chunk[1024];  // Increased for efficiency (32ms @ 16kHz Stereo 32-bit)
  uint8_t backend_chunk[512]; // 1024 bytes @ 32-bit stereo -> 512 bytes @ 16-bit mono

  if (!claude_ws_connected) {
    claude_ws_connect();
  }

  button_abort = false;
  response_done_received = false;
  response_audio_seen = false;
  response_audio_done_received = false;
  last_audio_payload_ms = 0;
  DBG_PRINTLN("Listening...");
  // Brief delay to allow UI to update before showing listening state
  vTaskDelay(100 / portTICK_PERIOD_MS);
  request_display_line1("Listening...");
  request_display_line2("");

  while (!stop_requested && recorder_task_handle != NULL) {
    // Do not touch the input channel while playback is active.
    // Playback can reconfigure I2S and momentarily leave RX disabled.
    if (player_task_handle != NULL) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    int iis_buffer_size = audio_input_get_iis_data_available();
    if (iis_buffer_size <= 0) {
      vTaskDelay(2 / portTICK_PERIOD_MS);
      continue;
    }
    
    int processed = 0;
    while (iis_buffer_size > 0 && processed < 4096) {
      if (ulTaskNotifyTake(pdTRUE, 0) > 0 || recorder_task_handle == NULL) {
        DBG_PRINTLN("[Recorder] Stop requested");
        stop_requested = true;
        break;
      }
      
      int real_size = audio_input_read_iis_data((char *)input_chunk, sizeof(input_chunk));
      if (real_size <= 0) break;

      size_t pcm_size = convert_input_to_backend_pcm(input_chunk, real_size, backend_chunk, sizeof(backend_chunk));
      if (pcm_size > 0) {
        if (claude_ws_connected) {
          claude_ws_send_audio_chunk(backend_chunk, pcm_size);
        }
      }
      iis_buffer_size -= real_size;
      processed += real_size;
    }
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }

  DBG_PRINTLN("[Recorder] loop_task_sound_recorder stop...");
  if (!button_abort && conversation_active) {
    request_display_line1("Generating response...");
    request_display_line2("");
  }
  
  // Signal backend that we are done sending audio and want transcription
  if (claude_ws_connected && !button_abort && conversation_active) {
    claude_ws_send_transcribe();
  }
  
  recorder_task_handle = NULL;
  vTaskDelete(NULL);
}


/* Start recording task */
void start_recorder_task(void) {
  // Do not start recorder while player is active
  if (player_task_handle != NULL) {
    DBG_PRINTLN("[Recorder] Recorder start suppressed: player active");
    return;
  }
  // Check if the recorder task is not already running
  if (recorder_task_handle == NULL) {
    // Create a new task for recording sound, store its handle
    TaskHandle_t temp_handle;
    xTaskCreate(loop_task_sound_recorder, "loop_task_sound_recorder", 8192, NULL, 1, &temp_handle);
    recorder_task_handle = temp_handle;
  }
}

/* Stop recording task */
void stop_recorder_task(void) {
  // Request the recorder task to stop via its task handle (graceful stop)
  if (recorder_task_handle != NULL) {
    DBG_PRINTLN("[Recorder] Signaling loop_task_sound_recorder to stop...");
    DBG_PRINTLN("Please wait...");
    request_display_line1("Please wait...");
    // Clear the handle to signal stop and send notification
    TaskHandle_t temp = recorder_task_handle;
    recorder_task_handle = NULL;
    xTaskNotifyGive(temp);
  } else {
    DBG_PRINTLN("[Recorder] Recorder task not running");
  }
}

void abort_conversation_and_return_idle(bool show_boot_instructions) {
  conversation_active = false;
  button_abort = true;

  if (claude_ws_connected) {
    claude_ws_send_stop();
  }
    
  if (recorder_task_handle != NULL) {
    DBG_PRINTLN("[Button] Stopping listening...");
    stop_recorder_task();
  }

  bool was_player_running = (player_task_handle != NULL);
  if (was_player_running) {
    DBG_PRINTLN("[Button] Stopping playback...");
    stop_player_task();
    i2s_output_stream_end();
  }

  resume_recorder_after_response = false;
  response_done_received = false;
  response_audio_seen = false;
  response_audio_done_received = false;
  last_audio_payload_ms = 0;

  if (!was_player_running) {
    stop_player_task();
  }

  request_clear_lines();
  if (show_boot_instructions) {
    request_showBootInstructions("Press button to start a conversation.");
  }
}

void handle_left_power_button_events() {
#ifdef BOARD_AIPI_LITE
  if (power_off_in_progress) return;

  const bool pressed = (digitalRead(BUTTON_PIN_LEFT) == LOW);
  if (pressed && !left_button_last_pressed) {
    left_button_press_start_ms = millis();
    left_button_longpress_fired = false;
  }

  if (pressed && !left_button_longpress_fired &&
      (millis() - left_button_press_start_ms >= POWER_BUTTON_LONG_PRESS_MS)) {
    left_button_longpress_fired = true;
    power_off_in_progress = true;

    DBG_PRINTLN("[Power] Left button long press: cutting power latch");
    request_display_line1("Powering off...");
    request_display_line2("");

    abort_conversation_and_return_idle(true);

    delay(150);
    digitalWrite(SPEAKER_AMP_ENABLE, LOW);
    digitalWrite(POWER_KEEP_ALIVE_PIN, LOW);
  }

  left_button_last_pressed = pressed;
#endif
}

void handle_button_events() {
  int button_state = button.get_button_state();
  if (button_state == Button::KEY_STATE_PRESSED && last_button_state_for_toggle != Button::KEY_STATE_PRESSED) {
    // If either recorder or player is running, stop them
    if (recorder_task_handle != NULL || player_task_handle != NULL) {
      abort_conversation_and_return_idle(true);
    } else {
      // Start a new conversation
      button_abort = false;
      conversation_active = true;
      resume_recorder_after_response = false;
      response_done_received = false;
      response_audio_seen = false;
      response_audio_done_received = false;
      last_audio_payload_ms = 0;
      DBG_PRINTLN("[Button] Starting continuous listening...");
      request_hideBootInstructions();
      start_recorder_task();
    }
  }
  last_button_state_for_toggle = button_state;
}

void loop_task_button_handler(void *pvParameters) {
  while (1) {
    handle_left_power_button_events();
    button.key_scan();
    handle_button_events();
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void update_display() {
  // Apply any pending display requests from background tasks
  // loop_counter++;
  // if (loop_counter % 10 == 0) {
  //   Serial.println("[Loop] Running main loop tasks..."); // Debug print every 10 loops
  // }
  // Boot-show/hide must be processed BEFORE line updates so that line labels
  // are positioned correctly (top-aligned vs. below-banner) from the moment
  // they are first created.
  if (display_boot_show_pending) {
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    char tmp3[128];
    strncpy(tmp3, display_boot_buf, sizeof(tmp3));
    display_boot_show_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.showBootInstructions(tmp3);
  } else if (display_boot_hide_pending) {
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    display_boot_hide_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.hideBootInstructions();
  }
  if (display_line1_pending) {
    DBG_PRINTF("[Loop] line1: %s\n", display_line1_buf);
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    char tmp[128];
    strncpy(tmp, display_line1_buf, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    if (display_line1_next_pending) {
      strncpy(display_line1_buf, display_line1_next_buf, sizeof(display_line1_buf)-1);
      display_line1_buf[sizeof(display_line1_buf)-1] = '\0';
      display_line1_next_pending = false;
      display_line1_pending = true;
    } else {
      display_line1_pending = false;
    }
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.displayLine1(tmp);
  }
  if (display_line2_pending) {
    DBG_PRINTF("[Loop] line2: %s\n", display_line2_buf);
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    char tmp2[128];
    strncpy(tmp2, display_line2_buf, sizeof(tmp2));
    display_line2_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.displayLine2(tmp2);
  }
  if (display_clear_pending) {
    DBG_PRINTF("[Loop] line1: clear pending\n");
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    display_clear_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.clearLines();
  }
  display.routine(); 
}

void portal_saved() {
  setup_phase1_saved = true;  
}

// Main loop function that runs continuously
int loop_counter = 0;

void loop() {
  update_display();

  // ---- One-time setup phases (run after loop() first enters) -------
  if (!setup_complete) {
    // Phase 0: Start captive portal if no credentials in NVS.
    if (!setup_phase0_portal) {
      if (loop_counter % 5000 == 0) {
              APP_SERIAL.print("-");
      }

      // No credentials — start the captive portal (one-shot, guarded by setup_phase0_portal).
      APP_SERIAL.println("[Loop Setup] Phase 0: Starting captive portal");
      wifi_config_start_portal();
      setup_phase0_portal = true;
      APP_SERIAL.println("[Loop Setup] Phase 1: Waiting for portal credentials to be saved...");
      return;
    }

    // Portal is running; wait for user to save credentials (which reboots).
    // If setup_phase1_saved is true but the device somehow got here without
    // credentials, keep serving the portal.
    if (!setup_phase1_saved && wifi_config_ssid()[0] == '\0') {
      if (loop_counter % 5000 == 0) {
              APP_SERIAL.print("=");
      }
      
      // Must process web server + display while portal is active,
      // otherwise the captive portal never handles client connections
      // and the display never updates.
      wifi_config_loop();
      
      return;
    }

    // Credentials now exist — proceed to Phase 2.
    if (!setup_phase2_wifi) {
      // Phase 2: Connect WiFi using provisioned credentials.
      APP_SERIAL.println("[Loop Setup] Phase 2: WiFi connect");

      if (wifi_config_connect()) {
        sync_time();
        setup_phase2_wifi = true;
      } else {
        DBG_PRINTLN("[Setup] WiFi connect failed — will retry in loop");
        return;
      }
    }

    if (!setup_phase3_ota) {
      if (WiFi.status() == WL_CONNECTED) {
        // Phase 3: ArduinoOTA + mDNS (requires WiFi connected).
        APP_SERIAL.println("[Loop Setup] Phase 3: ArduinoOTA + mDNS");

        if (!MDNS.begin(DEVICE_HOSTNAME)) {
          Serial.println("Error setting up MDNS responder!");
        }
        MDNS.addService("_http", "_tcp", 80);

        ArduinoOTA.setHostname(DEVICE_HOSTNAME);
        ArduinoOTA
        .onStart([]() {
           APP_SERIAL.println("[Setup] OTA Start");
           abort_conversation_and_return_idle(true);
         })
        .onEnd([]() { APP_SERIAL.println("\n[Setup] OTA End"); })
        .onError([](ota_error_t error) {
          APP_SERIAL.printf("[Setup] OTA Error[%u]\n", error);
        });

        ArduinoOTA.begin();
        MDNS.addService("telnet", "tcp", 23);
        APP_SERIAL.printf("[Setup] OTA ready at %s.local (%s)\n",
                          DEVICE_HOSTNAME, WiFi.localIP().toString().c_str());
        setup_phase3_ota = true;
        return;
      } else {
        APP_SERIAL.println("[Setup] OTA skipped: WiFi not connected");
        return;
      }
    }

    // Phase 4: RemoteDebug (requires WiFi stack init).
    if (!setup_phase4_debug) {
      APP_SERIAL.println("[Loop Setup] Phase 4: RemoteDebug");
      Debug.begin(DEVICE_HOSTNAME);
      Debug.setResetCmdEnabled(true);
      Debug.showProfiler(true);
      DBG_PRINTLN("[Setup] RemoteDebug ready");
      DBG_PRINTLN("[Setup] Serial commands: (w)s reconnect WS, (i)p info\n");
      setup_phase4_debug = true;
      return;
    }

    // Phase 5: WebSocket callbacks + connect.
    if (!setup_phase5_ws) {
      APP_SERIAL.println("[Loop Setup] Phase 5: WebSocket callbacks + connect");
      claude_ws_client.onMessage(claude_ws_on_message);
      claude_ws_client.onEvent(claude_ws_on_event);
      claude_ws_connect();

      // Show boot instruction on screen
      display.showBootInstructions("Press button to start a conversation.");
      request_display_line2("");

      setup_phase5_ws = true;
      setup_complete = true;
    }
  }
  // ------------------------------------------------------------------

  // oTA
  ArduinoOTA.handle();

  // Process captive portal DNS hijack + web requests (no-op when not in portal mode)
  wifi_config_loop();

  // Keep UI/state in response mode until backend confirms all audio is done.
  if (resume_recorder_after_response && !button_abort && conversation_active) {
    unsigned long now = millis();
    bool player_idle = (player_task_handle == NULL);
    bool done_settled = response_done_received && (now - response_done_ms > 120);

    // Primary gate: explicit backend signal that all response audio is complete.
    bool audio_done = response_audio_done_received;
    // Backward-compatible fallback for older servers that don't send audio_done.
    if (!audio_done && response_audio_seen) {
      audio_done = (now - last_audio_payload_ms > 2500);
    }

    // If no audio ever arrives (e.g. text-only backend response), recover after
    // a conservative timeout instead of restarting too early.
    bool no_audio_timeout = (!response_audio_seen) && (now - response_done_ms > 15000);
    if (!audio_done && no_audio_timeout) {
      DBG_PRINTLN("[WS] No response audio observed; resuming recorder after timeout.");
      audio_done = true;
    }

    if (player_idle && audio_done && done_settled) {
      resume_recorder_after_response = false;
      response_done_received = false;
      response_audio_seen = false;
      response_audio_done_received = false;
      request_display_line1("Please wait. Turning on microphone...");
      request_display_line2("");
      start_recorder_task();
    }
  }

  // Keep websocket alive and process backend messages.
  Debug.handle();
  claude_ws_poll();
  if (claude_ws_connected && claude_ws_config_pending) {
    if (claude_ws_send_vad_config(CLIENT_VAD_ENERGY_THRESHOLD)) {
      claude_ws_config_pending = false;
    }
  }
  // Light reconnect policy while idle.
  static unsigned long last_ws_retry = 0;
  if (!claude_ws_connected && millis() - last_ws_retry > 2000) {
    last_ws_retry = millis();
    claude_ws_connect();
  }
  // Simple serial UI
  if (APP_SERIAL.available()) {
    String input = APP_SERIAL.readStringUntil('\n');
    input.trim();
    if (input == "w") {
      DBG_PRINTLN("[Loop] Reconnecting websocket...");
      claude_ws_connected = false;
      claude_ws_connect();
    } else if (input == "i") {
      // Print IP info
      APP_SERIAL.print("[Loop] IP: ");
      APP_SERIAL.println(WiFi.localIP());
    } else if (input == "wifi") {
      // Enter captive portal for WiFi configuration
      DBG_PRINTLN("[Loop] Entering WiFi configuration portal...");
      setup_phase0_portal = false;
      setup_phase1_saved = false;
      setup_complete = false;
      // Stop any ongoing conversation
      if (conversation_active) {
        abort_conversation_and_return_idle(false);
      }
      // Stop WebSocket
      if (claude_ws_connected) {
        claude_ws_connected = false;
      }
      // Reset phases so loop() re-enters Phase 0
      setup_phase2_wifi = false;
      setup_phase3_ota = false;
      setup_phase4_debug = false;
      setup_phase5_ws = false;
      return;
    }
  }
  // Delay for 10 milliseconds
  delay(10);
}

// Connect to WiFi with simple retry logic
void wifi_connect() {
  APP_SERIAL.printf("[WiFi] Connecting to WiFi SSID: %s\r\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (!MDNS.begin(DEVICE_HOSTNAME)) {
    Serial.println("Error setting up MDNS responder!");
  }
  MDNS.addService("_http", "_tcp", 80);  // Critical for persistence

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    APP_SERIAL.print(".");
    if (millis() - start > 20000) {
      APP_SERIAL.println("\n[WiFi] WiFi connect timeout");
      return;
    }
  }
  APP_SERIAL.println("\n[WiFi] WiFi connected");
  APP_SERIAL.print("[WiFi] IP address: ");
  APP_SERIAL.println(WiFi.localIP());
  // Power save can drop mDNS responses; OTA discovery needs reliable WiFi timing.
  WiFi.setSleep(false);
  sync_time();
  // Give the network stack a moment to stabilize
  delay(1000);
}

// Simple HTTP GET to the server root for a connectivity test
void http_test_get() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN("[HTTP] Not connected to WiFi");
    return;
  }

  HTTPClient http;
  String url = String("https://") + SERVER_HOST;
  DBG_PRINTF("[HTTP] GET %s\r\n", url.c_str());
  http.begin(url);
  int code = http.GET();
  if (code > 0) {
    DBG_PRINTF("[HTTP] HTTP code: %d\r\n", code);
    String payload = http.getString();
    DBG_PRINTLN("[HTTP] Response (truncated to 1024 chars):");
    if (payload.length() > 1024) payload = payload.substring(0, 1024);
    DBG_PRINTLN(payload.c_str());
  } else {
    DBG_PRINTF("[HTTP] HTTP GET failed, error: %s\r\n", http.errorToString(code).c_str());
  }
  http.end();
}

// Helper: write 44-byte WAV header to the given client for PCM32, stereo as configured
void write_wav_header_to_client(WiFiClient &client, uint32_t data_bytes) {
  uint32_t sample_rate = 32000; // match hardware sample rate
  uint16_t channels = 2;
  uint16_t bits_per_sample = 32;

  uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
  uint16_t block_align = channels * bits_per_sample / 8;
  uint32_t subchunk2_size = data_bytes;
  uint32_t chunk_size = 36 + subchunk2_size;

  // RIFF header
  client.write((const uint8_t *)"RIFF", 4);
  client.write((const uint8_t *)&chunk_size, 4);
  client.write((const uint8_t *)"WAVE", 4);

  // fmt subchunk
  client.write((const uint8_t *)"fmt ", 4);
  uint32_t subchunk1_size = 16;
  client.write((const uint8_t *)&subchunk1_size, 4);
  uint16_t audio_format = 1; // PCM
  client.write((const uint8_t *)&audio_format, 2);
  client.write((const uint8_t *)&channels, 2);
  client.write((const uint8_t *)&sample_rate, 4);
  client.write((const uint8_t *)&byte_rate, 4);
  client.write((const uint8_t *)&block_align, 2);
  client.write((const uint8_t *)&bits_per_sample, 2);

  // data subchunk
  client.write((const uint8_t *)"data", 4);
  client.write((const uint8_t *)&subchunk2_size, 4);
}

// Simple JSON value extractor for top-level string fields
String extract_json_string_value(const String &json, const String &key) {
  String needle = String("\"") + key + String("\"") + String(":");
  int idx = json.indexOf(needle);
  if (idx < 0) return String("");
  
  // move to first quote after ':'
  int q = json.indexOf('"', idx + needle.length());
  if (q < 0) return String("");
  int q_start = q + 1;
  
  // Find the end quote, taking escapes into account
  int q_end = -1;
  int curr = q_start;
  while (curr < json.length()) {
    if (json[curr] == '\\') {
      curr += 2; // skip escape and the escaped char
      continue;
    }
    if (json[curr] == '"') {
      q_end = curr;
      break;
    }
    curr++;
  }
  
  if (q_end == -1) return String("");
  
  String raw = json.substring(q_start, q_end);
  // Now handle escapes if any
  if (raw.indexOf('\\') == -1) return raw; // common case, no escapes
  
  String out = "";
  out.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); ++i) {
    char c = raw[i];
    if (c == '\\' && i + 1 < raw.length()) {
      char esc = raw[i + 1];
      if (esc == '"') out += '"';
      else if (esc == 'n') out += '\n';
      else if (esc == 'r') out += '\r';
      else if (esc == 't') out += '\t';
      else if (esc == '/') out += '/';
      else if (esc == '\\') out += '\\';
      i++;
    } else {
      out += c;
    }
  }
  return out;
}

// Extract all occurrences of a string field (useful for streaming JSON lines)
String extract_all_json_string_values(const String &json, const String &key) {
  String out = "";
  String needle = String("\"") + key + String("\"") + String(":");
  int start = 0;
  while (true) {
    int idx = json.indexOf(needle, start);
    if (idx < 0) break;
    // move to first quote after ':'
    int q = json.indexOf('"', idx + needle.length());
    if (q < 0) break;
    int q_start = q + 1;
    
    int q_end = -1;
    int curr = q_start;
    while (curr < json.length()) {
        if (json[curr] == '\\') {
            curr += 2;
            continue;
        }
        if (json[curr] == '"') {
            q_end = curr;
            break;
        }
        curr++;
    }
    if (q_end == -1) break;
    
    String val = json.substring(q_start, q_end);
    // basic unescape
    if (val.indexOf('\\') != -1) {
        String unesc = "";
        unesc.reserve(val.length());
        for(size_t i=0; i<val.length(); i++) {
            if (val[i] == '\\' && i+1 < val.length()) {
                char esc = val[i+1];
                if (esc == '"') unesc += '"';
                else if (esc == 'n') unesc += '\n';
                else if (esc == '/') unesc += '/';
                else if (esc == '\\') unesc += '\\';
                i++;
            } else {
                unesc += val[i];
            }
        }
        out += unesc;
    } else {
        out += val;
    }
    
    // advance search position
    start = q_end + 1;
  }
  return out;
}

// Minimal JSON string escaper for safe embedding in request bodies
String json_escape(const String &s) {
  String out = "";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

/* Check if recording task is active */
int is_recorder_task_running(void) {
  // Return the status based on handle
  return (recorder_task_handle != NULL) ? 1 : 0;
}

/* Start player task */
void start_player_task(void) {
  // Check if the player task is not already running
  if (player_task_handle == NULL) {
    TaskHandle_t temp_handle;
    xTaskCreate(loop_task_play_handle, "loop_task_play_handle", 8192, NULL, 1, &temp_handle);
    player_task_handle = temp_handle;
  }
}

/* Stop player task */
void stop_player_task(void) {
  // Request player task to stop by notifying it
  if (player_task_handle != NULL) {
    DBG_PRINTLN("[Player] Signaling playback to stop...");
    // Clear the handle to signal stop and send notification
    TaskHandle_t temp = player_task_handle;
    player_task_handle = NULL;
    // Only notify if it's a real task handle (not the flag value 1)
    if (temp != (TaskHandle_t)1) {
      xTaskNotifyGive(temp);
    }
  } else {
      DBG_PRINTLN("[Player] Player task not running");
  }
}

/* Check if player task is active */
int is_player_task_running(void) {
  // Return the status based on handle
  return (player_task_handle != NULL) ? 1 : 0;
}

/* Main player task loop */
void loop_task_play_handle(void *pvParameters) {
  DBG_PRINTF("[Player] Task '%s' min free stack: %u bytes\n", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));

  // Print a message indicating the start of the player task
  DBG_PRINTLN("[Player] loop_task_play_handle start...");
  bool stop_requested = false;
  // Loop while the player task is running and handle is not NULL
  while (!stop_requested && player_task_handle != NULL && !button_abort) {
      if (button_abort) {
        // Stop the player task if button abort is requested
        DBG_PRINTLN("[Player] Button abort requested, stopping player task");
        DBG_PRINTLN("Stopped Playing - Button Aborted");
        request_display_line1("Stopped Playing - Button Aborted");
        stop_requested = true;
        break;
      }
      // Check for a stop notification (non-blocking) or if handle was cleared
      if (ulTaskNotifyTake(pdTRUE, 0) > 0 || player_task_handle == NULL) {
        DBG_PRINTLN("Stopped Responding - Task Stopped");
        request_display_line1("Stopped Responding - Task Stopped");
        stop_requested = true;
        break;
      }
      // Play the last in-memory recording (PSRAM)
      if (wav_buffer != NULL && last_recorded_size > 0) {
        DBG_PRINTF("Playing in-memory recording, size=%u\r\n", (unsigned)last_recorded_size);
        i2s_output_wav(wav_buffer, last_recorded_size);
      } else {
        DBG_PRINTLN("[Player] No in-memory recording available to play.");
      }
      // After playback, stop
      DBG_PRINTLN("Stopped Responding - Task Finished");
      request_display_line1("Stopped Responding - Task Finished");
      stop_requested = true;
  }
  // Print a message indicating the end of the player task
  DBG_PRINTLN("[Player] loop_task_play_handle stop...");
  // Clear handle and delete the current task
  player_task_handle = NULL;
  vTaskDelete(NULL);
}


