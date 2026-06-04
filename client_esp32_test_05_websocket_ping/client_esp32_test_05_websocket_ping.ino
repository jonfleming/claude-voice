/*
 * Test 05: WiFi + backend WebSocket ping test.
 *
 * Goal:
 *   Verify WiFi credentials, backend address, WebSocket library support, and
 *   the `/ws` protocol path before streaming microphone audio.
 *
 * Expected result:
 *   The sketch connects to WiFi, opens ws://SERVER_IP:8080/ws, sends
 *   {"type":"ping"}, and logs any pong/message/event received. Press the
 *   button, or type "p" in the serial monitor, to send another ping.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>

#include "../client_esp32/board_pins.h"
#include "../client_esp32/display.h"
#include "../client_esp32/driver_audio_input.h"
#include "../client_esp32/driver_audio_output.h"
#include "../client_esp32/driver_button.h"

using namespace websockets;

// Edit these before upload.
#define WIFI_SSID "GL-SFT1200-3e1"
#define WIFI_PASS "goodlife"
#define SERVER_IP "192.168.8.145"
#define CLAUDE_VOICE_WS_PORT 8080
#define CLAUDE_VOICE_WS_PATH "/ws"

static WebsocketsClient ws_client;
static bool ws_connected = false;
static int last_button_state = Button::KEY_STATE_IDLE;
static uint32_t ping_count = 0;
static uint32_t last_reconnect_ms = 0;

void set_lines(const char *line1, const char *line2) {
  display.displayLine1(line1);
  display.displayLine2(line2);
}

void send_ping() {
  if (!ws_connected) {
    Serial.println("WebSocket is not connected; ping skipped.");
    set_lines("WebSocket not connected.", "Waiting to reconnect...");
    return;
  }

  ping_count++;
  const char *payload = "{\"type\":\"ping\"}";
  const bool ok = ws_client.send(payload);
  Serial.printf("Ping %lu send %s: %s\r\n",
    (unsigned long)ping_count, ok ? "ok" : "failed", payload);

  char line1[96];
  snprintf(line1, sizeof(line1), "Sent ping %lu", (unsigned long)ping_count);
  set_lines(line1, ok ? "Waiting for pong..." : "Send failed.");

  if (!ok) {
    ws_connected = false;
  }
}

void connect_wifi() {
  Serial.printf("Connecting to WiFi SSID: %s\r\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  set_lines("Connecting WiFi...", WIFI_SSID);
  const uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start_ms < 20000) {
    Serial.print(".");
    display.routine();
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
    set_lines("WiFi connected.", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connection timed out.");
    set_lines("WiFi connect failed.", "Check SSID/password.");
  }
}

bool connect_websocket() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  char line2[96];
  snprintf(line2, sizeof(line2), "%s:%d%s", SERVER_IP,
    CLAUDE_VOICE_WS_PORT, CLAUDE_VOICE_WS_PATH);
  set_lines("Connecting WebSocket...", line2);
  Serial.printf("Connecting WebSocket to ws://%s:%d%s\r\n", SERVER_IP,
    CLAUDE_VOICE_WS_PORT, CLAUDE_VOICE_WS_PATH);

  ws_connected = ws_client.connect(SERVER_IP, CLAUDE_VOICE_WS_PORT,
    CLAUDE_VOICE_WS_PATH);

  if (ws_connected) {
    Serial.println("WebSocket connected.");
    set_lines("WebSocket connected.", "Sending first ping...");
    send_ping();
  } else {
    Serial.println("WebSocket connection failed.");
    set_lines("WebSocket failed.", "Retrying every 3 sec.");
  }
  return ws_connected;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println();
  Serial.println("Test 05: WebSocket ping test");
  display.init(TFT_DIRECTION);
  display.showBootInstructions("Test 05: websocket ping");
  set_lines("Starting network test.", "");

  ws_client.onMessage([](WebsocketsMessage message) {
    const String data = message.data();
    Serial.print("WebSocket message: ");
    Serial.println(data);

    char line1[96];
    snprintf(line1, sizeof(line1), "Received %u bytes", (unsigned)data.length());
    set_lines(line1, data.substring(0, 90).c_str());
  });

  ws_client.onEvent([](WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
      ws_connected = true;
      Serial.println("WebSocket event: opened");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
      ws_connected = false;
      Serial.print("WebSocket event: closed ");
      Serial.println(data);
      set_lines("WebSocket closed.", "Will retry.");
    } else if (event == WebsocketsEvent::GotPing) {
      Serial.println("WebSocket event: ping");
    } else if (event == WebsocketsEvent::GotPong) {
      Serial.println("WebSocket event: pong");
      set_lines("Received WS pong event.", "Ping path works.");
    }
  });

  connect_wifi();
  connect_websocket();
}

void loop() {
  button.key_scan();
  const int state = button.get_button_state();
  if (state == Button::KEY_STATE_PRESSED &&
      last_button_state != Button::KEY_STATE_PRESSED) {
    send_ping();
  }
  last_button_state = state;

  if (Serial.available()) {
    const char c = Serial.read();
    if (c == 'p' || c == 'P') {
      send_ping();
    } else if (c == 'w' || c == 'W') {
      ws_connected = false;
      connect_websocket();
    }
  }

  if (ws_connected) {
    const bool still_ok = ws_client.available();
    if (still_ok) {
      ws_client.poll();
    } else {
      ws_connected = false;
      set_lines("WebSocket lost.", "Will retry.");
    }
  }

  if (!ws_connected && millis() - last_reconnect_ms > 3000) {
    last_reconnect_ms = millis();
    if (WiFi.status() != WL_CONNECTED) {
      connect_wifi();
    }
    connect_websocket();
  }

  display.routine();
  delay(10);
}
