/*
 *  wifi_config.cpp — AP-mode captive portal for provisioning WiFi credentials.
 *
 *  Uses TinyPortal (DNS hijack + captive detection) + WebServer to spin up an
 *  AP with an HTML form where the user enters SSID and password. Credentials are
 *  saved to ESP32 NVS via Preferences, surviving reboots.
 *
 *  Flow:
 *    1. wifi_config_init() is called from setup(). It reads any previously
 *       stored credentials from NVS under namespace "wifi".
 *    2. If credentials exist it returns; the sketch calls wifi_config_connect()
 *       to connect as station.
 *    3. If no credentials are found (first boot, or after a factory reset),
 *       an AP is started with SSID "claude-voice-setup" and a captive portal
 *       hosts an HTML form.  The user enters the target network SSID + password,
 *       hits Save, and the ESP32 reboots to try connecting with the new
 *       credentials.
 *    4. The serial "wifi" command (handled in client_esp32.ino) forces re-entry
 *       into the portal at any time.
 *
 *  Note: TinyPortal does NOT manage WiFi — we call WiFi.softAP() manually.
 *  TinyPortal does NOT have a stop() method — we disconnect AP manually.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <TinyPortal.h>

#include "client_esp32.h"

// Local hostname for WiFi STA mode (mirrors client_esp32.ino)
static const char* WIFI_HOSTNAME = "claude-voice-esp32";

static Preferences* s_prefs = nullptr;

static Preferences& get_prefs() {
  if (!s_prefs) {
    s_prefs = new Preferences();
  }
  return *s_prefs;
}
static WebServer web_server(80);
static TinyPortal portal;

// Buffers to hold the loaded SSID and password.  Zeroed on init so callers
// always get a valid (possibly empty) string pointer.
static char stored_ssid[64]    = {0};
static char stored_password[64]= {0};

// Set by wifi_config_init() when no credentials exist in NVS.
// The caller must call wifi_config_enter_portal() after the WiFi
// stack is initialized (in loop-based setup phase).
static bool _needs_portal = false;

// ---- HTML form for the captive portal ----------------------------

static const char portal_html[] PROGMEM = R"EOF(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Setup</title>
<style>
  body { font-family: sans-serif; text-align: center; padding: 40px; }
  input { margin: 8px 0; padding: 8px; width: 260px; font-size: 16px; }
  button { margin-top: 16px; padding: 10px 32px; font-size: 16px; }
</style>
</head>
<body>
<h2>WiFi Setup</h2>
<form action="/save" method="post">
  <input name="ssid" placeholder="Network SSID" required><br>
  <input name="password" placeholder="Password" type="password" required><br>
  <button type="submit">Save</button>
</form>
</body>
</html>
)EOF";

// ---- WebServer route handlers ------------------------------------

void handle_portal_root() {
  web_server.send(200, "text/html", portal_html);
}

void handle_save() {
  String ssid    = web_server.arg("ssid");
  String password = web_server.arg("password");

  if (ssid.length() == 0 || password.length() == 0) {
    web_server.send(400, "text/html", "<h1>Missing credentials</h1><a href='/'>Try again</a>");
    return;
  }

  ssid.toCharArray(stored_ssid, sizeof(stored_ssid));
  password.toCharArray(stored_password, sizeof(stored_password));

  // Persist to NVS
  get_prefs().begin("wifi", false);
  get_prefs().putString("ssid",    stored_ssid);
  get_prefs().putString("password", stored_password);
  get_prefs().end();

  // Show confirmation on display
  request_display_line1("WiFi credentials saved");
  request_display_line2("Rebooting...");

  delay(1000);
  ESP.restart();
}

void handle_not_found() {
  web_server.send(404, "text/plain", "Not Found");
}

// ---- helpers to manage AP + portal lifecycle ---------------------

static void start_captive_portal(const char* ap_ssid) {
  // Start AP
  WiFi.softAP(ap_ssid);
  delay(500);

  // Register routes on the web server
  web_server.on("/", handle_portal_root);
  web_server.on("/save", HTTP_POST, handle_save);
  web_server.onNotFound(handle_not_found);
  web_server.begin();

  // Attach TinyPortal: starts DNS hijack + captive detection on the server
  portal.begin(web_server);

  // Show captive portal instructions on display
  request_showBootInstructions("WiFi Setup");
  request_display_line1("Connect to: ");
  request_display_line2(ap_ssid);
}

static void stop_captive_portal() {
  // TinyPortal has no stop() method — clean up manually
  web_server.stop();
  WiFi.softAPdisconnect(true);
}

// ---- public API --------------------------------------------------

/**
 * Load stored credentials from NVS.  Sets an internal flag if no
 * credentials were found so the caller can start the captive portal
 * *after* the WiFi stack is initialized (in loop-based setup phase).
 */
void wifi_config_init() {
  get_prefs().begin("wifi", true);  // read-only

  char tmp_ssid[64]    = {0};
  char tmp_pass[64]    = {0};

  get_prefs().getString("ssid", tmp_ssid, sizeof(tmp_ssid));
  get_prefs().getString("password", tmp_pass, sizeof(tmp_pass));
  get_prefs().end();

  // If NVS has no SSID, the caller must enter captive portal mode.
  _needs_portal = (tmp_ssid[0] == '\0');

  if (_needs_portal) {
    // Clear the buffers so wifi_config_connect() knows no credentials exist.
    stored_ssid[0]    = '\0';
    stored_password[0] = '\0';
  }
}

/**
 * Connect as STA using the loaded credentials.
 * Returns true on success, false on timeout.
 */
bool wifi_config_connect() {
  if (stored_ssid[0] == '\0') {
    request_display_line1("WiFi not configured");
    request_display_line2("Run wifi_config_init() first");
    return false;
  }

  request_display_line1("Connecting to WiFi");
  request_display_line2(stored_ssid);

  WiFi.mode(WIFI_STA);

  const char* hostname = WIFI_HOSTNAME;
  WiFi.setHostname(hostname);
  WiFi.begin(stored_ssid, stored_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - start > 20000) {
      request_display_line1("WiFi connect timeout");
      request_display_line2("Try again...");
      return false;
    }
  }

  request_display_line1("WiFi connected");
  request_display_line2(WiFi.localIP().toString().c_str());

  WiFi.setSleep(false);

  return true;
}

/**
 * Return pointer to the currently loaded SSID string (never null).
 */
const char* wifi_config_ssid() {
  return stored_ssid;
}

/**
 * Return pointer to the currently loaded password string (never null).
 */
const char* wifi_config_password() {
  return stored_password;
}

/**
 * Force entry into captive-portal configuration mode.
 * Useful when invoked from a serial command or button combo so the user
 * can change credentials at any time without reflashing.
 */
void wifi_config_enter_portal() {
  // Stop any existing portal/AP first
  web_server.stop();
  WiFi.softAPdisconnect(true);
  delay(200);
  WiFi.mode(WIFI_OFF);
  delay(100);

  request_display_line1("WiFi Setup");
  request_display_line2("Enter new credentials");

  start_captive_portal("claude-voice-setup");
}

/**
 * Start the captive portal.  Must be called from the loop-based setup
 * phase (after WiFi stack is initialized).
 */
void wifi_config_start_portal() {
  start_captive_portal("claude-voice-setup");
}

/**
 * Call from main loop() while captive portal is active.
 * Processes DNS hijack requests and web server clients.
 */
void wifi_config_loop() {
  portal.loop();
  web_server.handleClient();
}
