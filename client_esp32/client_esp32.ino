/*
* Sketch_09_3_Reird_And_Play.ino
* This sketch records audio data from an audio input using the I2S bus, sends it to a server for transcription,
* receives the transcribed text, sends it to an AI model for generating a response, and then uses a TTS service
* to convert the response text back into speech, which is played through an audio output using I2S.
* 
* Author: Jon Fleming
* Date:   2026-03-30
*/
#include "driver_audio_input.h"
#include "driver_audio_output.h"
#include "driver_button.h"
#include "client_esp32.h"
#include <esp_heap_caps.h>
#include <ArduinoWebsockets.h>
#include <mbedtls/base64.h>
// WiFi + HTTP
#include <WiFi.h>
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

#define DBG_PRINTLN(msg) Debug.println(msg)
#define DBG_PRINT(msg) Debug.print(msg)
#define DBG_PRINTF(...) Debug.printf(__VA_ARGS__)

// Mutex to protect display request buffers
SemaphoreHandle_t display_mutex = NULL;
SemaphoreHandle_t ws_mutex = NULL;

#define RECORDER_FOLDER ""
// Define the pin number for the button (do not modify)
#define BUTTON_PIN 19
// Define the pin numbers for audio input (do not modify)
#define AUDIO_INPUT_SCK 3
#define AUDIO_INPUT_WS 14     
#define AUDIO_INPUT_DIN 46    
// Define the pin numbers for audio output (do not modify)
#define AUDIO_OUTPUT_BCLK 42  
#define AUDIO_OUTPUT_LRC 41   
#define AUDIO_OUTPUT_DOUT 1   

// Define the size of PSRAM in bytes
#define MOLLOC_SIZE (4 * 1024 * 1024)

// ---------- WiFi / Server configuration (edit before upload) ----------
//#define WIFI_SSID "FLEMING_2"
//#define WIFI_PASS "90130762"
#define WIFI_SSID "GL-SFT1200-3e1"
#define WIFI_PASS "goodlife"

// The server that runs your transcription/TTS services (*two* Tailnet Bridge)
#define SERVER_IP "192.168.8.144"
#define CLAUDE_VOICE_WS_PORT 8080
#define CLAUDE_VOICE_WS_PATH "/ws"

// Ollama model to use for generation (change as needed)
#define OLLAMA_MODEL "llama3.2"

// Global button instance is declared in `driver_button.h` and defined in `display.cpp`.
// Use the shared `button` instance (defined in display.cpp) via the extern declaration.

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

// Coordinate backend "done" with actual audio playback completion.
volatile bool resume_recorder_after_response = false;
volatile unsigned long response_done_ms = 0;
volatile unsigned long last_audio_payload_ms = 0;
volatile bool response_done_received = false;
volatile bool response_audio_seen = false;
volatile bool response_audio_done_received = false;
volatile bool conversation_active = false;

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
  strncpy(display_line1_buf, text, sizeof(display_line1_buf)-1);
  display_line1_buf[sizeof(display_line1_buf)-1] = '\0';
  display_line1_pending = true;
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
    // String token = extract_json_string_value(json, "content");
    // // Do NOT trim token here; Ollama often sends tokens with leading/trailing spaces
    // if (token.length() > 0) {
    //   Serial.print(token);
    //   request_display_line2(token.c_str());
    // }
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
        display_line1_pending = false;
      }
      if (display_mutex) xSemaphoreGive(display_mutex);
    }
  } else if (type == "error") {
    String err = extract_json_string_value(json, "content");
    DBG_PRINTF("[WS] Backend error: %s\n", err.c_str());
    request_display_line1("Backend error");
    request_display_line2(err.c_str());
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
      player_task_handle = NULL;
    }
    return;
  }

  handle_claude_ws_json(message.data());
}

void claude_ws_on_event(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    claude_ws_connected = true;
    claude_ws_connecting = false;
    DBG_PRINTLN("[WS] Connection opened");
    request_display_line2("Connected using GL router");
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    claude_ws_connected = false;
    claude_ws_connecting = false;
    DBG_PRINTF("[WS] Connection closed: %s\n", data.c_str());
    request_display_line2("Disconnected");
  } else if (event == WebsocketsEvent::GotPing) {
    DBG_PRINTLN("[WS] ping");
  } else if (event == WebsocketsEvent::GotPong) {
    DBG_PRINTLN("[WS] pong event");
  }
}

bool claude_ws_connect() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN("[WS] WiFi not connected; cannot connect websocket");
    return false;
  }
  if (claude_ws_connected || claude_ws_connecting) return claude_ws_connected;

  claude_ws_connecting = true;
  DBG_PRINTF("[WS] Connecting to %s:%d%s\n", SERVER_IP, CLAUDE_VOICE_WS_PORT, CLAUDE_VOICE_WS_PATH);

  bool ok = false;
  if (ws_mutex) xSemaphoreTake(ws_mutex, portMAX_DELAY);
  ok = claude_ws_client.connect(SERVER_IP, CLAUDE_VOICE_WS_PORT, CLAUDE_VOICE_WS_PATH);
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
      DBG_PRINTF("[WS] Sent audio chunk, size: %u\n", (unsigned)len);
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

// Track last debounced button state to detect edges
int last_button_state_for_toggle = Button::KEY_STATE_IDLE;

// Setup function to initialize the hardware and software components
void setup() {
  // Initialize the serial communication at 115200 baud rate
  Serial.begin(115200);
  // Wait for the serial port to be ready
  while (!Serial) {
    delay(10);
  }
  // Display
  display.init(TFT_DIRECTION);
  // Show boot instruction at top of screen
  display.showBootInstructions("Press button to start a conversation.");
  Serial.println("");
  request_display_line2("");

  // Initialize the I2S bus for audio input
  audio_input_init(AUDIO_INPUT_SCK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
  // Initialize the I2S bus for audio output
  i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT);
  // Set default volume to ~half (range 0-21)
  audio_output_set_volume(10);

  // Create button handler task
  xTaskCreate(loop_task_button_handler, "button_handler", 4096, NULL, 2, NULL);

  // Create mutex for display buffer protection
  display_mutex = xSemaphoreCreateMutex();
  if (!display_mutex) {
    Serial.println("[Setup] Warning: failed to create display mutex");
  }
  ws_mutex = xSemaphoreCreateMutex();
  if (!ws_mutex) {
    Serial.println("[Setup] Warning: failed to create websocket mutex");
  }

  // Connect to WiFi (used for HTTP requests)
  wifi_connect();

  // Start RemoteDebug only after WiFi stack init/connect to avoid lwIP mbox assert.
  Debug.begin("claude-voice-esp32");
  Debug.setResetCmdEnabled(true);
  Debug.showProfiler(true);

  // Configure websocket callbacks and connect to claude-voice backend.
  claude_ws_client.onMessage(claude_ws_on_message);
  claude_ws_client.onEvent(claude_ws_on_event);
  claude_ws_connect();

  DBG_PRINTLN("[Setup] RemoteDebug ready");
  DBG_PRINTLN("[Setup] Serial commands: (w)s reconnect WS, (i)p info\n");
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

  // Signal backend that we are starting to send audio
  if (claude_ws_connected) {
    claude_ws_send_transcribe();
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

void handle_button_events() {
  int button_state = button.get_button_state();
  if (button_state == Button::KEY_STATE_PRESSED && last_button_state_for_toggle != Button::KEY_STATE_PRESSED) {
    // If either recorder or player is running, stop them
    if (recorder_task_handle != NULL || player_task_handle != NULL) {
      conversation_active = false;
      button_abort = true;
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
      request_showBootInstructions("Press button to start a conversation.");
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
    button.key_scan();
    handle_button_events();
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// Main loop function that runs continuously
int loop_counter = 0;
void loop() {
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
    DBG_PRINTF("[Loop] line1: %s  line2: %s\n", display_line1_buf, display_line2_buf);
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    char tmp[128];
    strncpy(tmp, display_line1_buf, sizeof(tmp));
    display_line1_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.displayLine1(tmp);
  }
  if (display_line2_pending) {
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    char tmp2[128];
    strncpy(tmp2, display_line2_buf, sizeof(tmp2));
    display_line2_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.displayLine2(tmp2);
  }
  if (display_clear_pending) {
    if (display_mutex) xSemaphoreTake(display_mutex, portMAX_DELAY);
    display_clear_pending = false;
    if (display_mutex) xSemaphoreGive(display_mutex);
    display.clearLines();
  }
  display.routine(); 

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
  // Light reconnect policy while idle.
  static unsigned long last_ws_retry = 0;
  if (!claude_ws_connected && millis() - last_ws_retry > 2000) {
    last_ws_retry = millis();
    claude_ws_connect();
  }
  // Simple serial UI
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input == "w") {
      DBG_PRINTLN("[Loop] Reconnecting websocket...");
      claude_ws_connected = false;
      claude_ws_connect();
    } else if (input == "i") {
      // Print IP info
      Serial.print("[Loop] IP: ");
      Serial.println(WiFi.localIP());
    }
  }
  // Delay for 10 milliseconds
  delay(10);
}

// Connect to WiFi with simple retry logic
void wifi_connect() {
  Serial.printf("[WiFi] Connecting to WiFi SSID: %s\r\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\n[WiFi] WiFi connect timeout");
      return;
    }
  }
  Serial.println("\n[WiFi] WiFi connected");
  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.localIP());
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
  String url = String("http://") + SERVER_IP + ":" + String(CLAUDE_VOICE_WS_PORT) + "/";
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


