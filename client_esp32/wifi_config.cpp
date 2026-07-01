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
 *       an AP is started with SSID "voice-setup" and a captive portal
 *       hosts an HTML form.  The user enters the target network SSID + password,
 *       hits Save, and the ESP32 reboots to try connecting with the new
 *       credentials.
 *    4. The serial "wifi" command (handled in client_esp32.ino) forces re-entry
 *       into the portal at any time.
 *
 *  Saved networks:
 *    - Up to 8 networks stored in NVS under namespace "saved_wifi".
 *    - Last-used SSID stored under "last_saved" key.
 *    - Portal HTML shows available APs (scanned) and saved networks (selectable).
 *    - When connecting, tries last_saved first, falls back to stored_ssid.
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
#include "sketch_config.h"
#include "display.h"

// Board-specific Serial (matches client_esp32.ino)
#ifdef BOARD_AIPI_LITE
#define WIFI_SERIAL Serial
#else
#define WIFI_SERIAL Serial0
#endif

// Debug macro — matches DBG_PRINTF style from client_esp32.ino
#define WIFI_DBG(...) do { WIFI_SERIAL.printf(__VA_ARGS__); } while(0)
#define WIFI_DBG_LN(msg) do { WIFI_SERIAL.println(msg); } while(0)

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

// Last-used SSID (for auto-retry on boot)
static char last_saved_ssid[64] = {0};

// ---- Saved networks list (NVS) -----------------------------------

static const int MAX_SAVED_NETWORKS = 8;

/**
 * Save a network to the NVS saved networks list.
 * If the SSID already exists, its password is updated.
 * Returns true on success.
 */
bool wifi_config_add_network(const char* ssid, const char* password) {
  get_prefs().begin("saved_wifi", false);  // read-write

  // Check if SSID already exists
  int count = get_prefs().getInt("count", 0);

  for (int i = 0; i < count; ++i) {
    String key = "ssid_" + String(i);

    // Buffer to hold the SSID, 64 bytes max
    char ssidBuf[65] = {0};           // one byte for NUL terminator
    size_t len = get_prefs().getString(key.c_str(), ssidBuf, sizeof(ssidBuf));

    String existing(ssidBuf);          // convert buffer to Arduino String

    if (existing == ssid) {
        get_prefs().putString(("pass_" + String(i)).c_str(), password);
        get_prefs().putString("last_saved", ssid);
        get_prefs().end();
        return true;
    }
  }

  // Add new entry if we have room
  if (count < MAX_SAVED_NETWORKS) {
    String idx_str = String(count);
    get_prefs().putString(String("ssid_" + idx_str).c_str(), ssid);
    get_prefs().putString(String("pass_" + idx_str).c_str(), password);
    get_prefs().putInt("count", count + 1);
    get_prefs().putString("last_saved", ssid);
    get_prefs().end();
    return true;
  }

  get_prefs().end();
  return false;  // list full
}

/**
 * Delete a network from the saved list by SSID.
 * Returns true if found and deleted.
 */
bool wifi_config_delete_network(const char* ssid) {
  get_prefs().begin("saved_wifi", false);  // read-write

  int count = get_prefs().getInt("count", 0);
  int found = -1;

  for (int i = 0; i < count; ++i) {
    String key = "ssid_" + String(i);

    // Buffer to hold the SSID, 64 bytes max
    char ssidBuf[65] = {0};           // one byte for NUL terminator
    size_t len = get_prefs().getString(key.c_str(), ssidBuf, sizeof(ssidBuf));

    String existing(ssidBuf);         // convert buffer to Arduino String
    if (existing == ssid) {
        found = i;
        break;
    }

    if (found < 0) {
      get_prefs().end();
      return false;
    }

    for (int i = found; i < count - 1; i++) {
      String ssid_key = "ssid_" + String(i);
      String pass_key = "pass_" + String(i);
      String ssid_next_key = "ssid_" + String(i + 1);
      String pass_next_key = "pass_" + String(i + 1);

      // Buffer to hold the SSID and password, 64 bytes max
      char ssidBuf[65] = {0};           // one byte for NUL terminator
      char passBuf[65] = {0};           // one byte for NUL terminator

      // Remove the entry by shifting subsequent entries down
      get_prefs().getString(ssid_next_key.c_str(), ssidBuf, sizeof(ssidBuf));
      get_prefs().getString(pass_next_key.c_str(), passBuf, sizeof(passBuf));
      get_prefs().putString(("ssid_" + String(i)).c_str(), ssidBuf);
      get_prefs().putString(("pass_" + String(i)).c_str(), passBuf);
    }

    get_prefs().putInt("count", count - 1);

    // Clear the last_saved if it was the deleted network
    char lastBuf[65] = {0};
    get_prefs().getString("last_saved", lastBuf, sizeof(lastBuf));
    String last(lastBuf);             // convert buffer to Arduino String
    if (last == ssid) {
      get_prefs().putString("last_saved", "");
    }

    get_prefs().end();
  }

  return true;
}

/**
 * Get all saved network SSIDs into the output array.
 * Returns the number of networks returned.
 */
int wifi_config_get_saved_networks(char out_ssid[][64], int max_networks) {
  get_prefs().begin("saved_wifi", true);  // read-only

  int count = get_prefs().getInt("count", 0);
  int out_count = 0;


  for (int i = 0; i < count && out_count < max_networks; i++) {
    String key = "ssid_" + String(i);

    // Buffer to hold the SSID, 64 bytes max
    char ssidBuf[65] = {0};           // one byte for NUL terminator
    size_t len = get_prefs().getString(key.c_str(), ssidBuf, sizeof(ssidBuf));
    String s(ssidBuf);                // convert buffer to Arduino String

    if (s.length() > 0) {
      strncpy(out_ssid[out_count], s.c_str(), 63);
      out_ssid[out_count][63] = '\0';
      out_count++;
    }
  }

  get_prefs().end();
  return out_count;
}

const char* wifi_config_get_last_saved() {
  get_prefs().begin("saved_wifi", true);  // read-only
  char ssidBuf[65] = {0};           // one byte for NUL terminator
  size_t len = get_prefs().getString("last_saved", ssidBuf, sizeof(ssidBuf));
  String last(ssidBuf);             // convert buffer to Arduino String
  get_prefs().end();

  if (last.length() > 0) {
    strncpy(last_saved_ssid, last.c_str(), sizeof(last_saved_ssid) - 1);
    last_saved_ssid[sizeof(last_saved_ssid) - 1] = '\0';
    return last_saved_ssid;
  }
  return "";
}

void wifi_config_set_last_saved(const char* ssid) {
  get_prefs().begin("saved_wifi", false);  // read-write
  get_prefs().putString("last_saved", ssid);
  get_prefs().end();
}

// ---- HTML form for the captive portal ----------------------------

static const char portal_html[] PROGMEM = R"EOF(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Setup</title>
<style>
  body { font-family: sans-serif; text-align: center; padding: 20px; background: #f5f5f5; }
  h2 { color: #333; }
  .section { background: #fff; border-radius: 8px; padding: 16px; margin: 12px auto; max-width: 360px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
  .section h3 { margin: 0 0 10px 0; color: #555; font-size: 14px; text-transform: uppercase; letter-spacing: 1px; }
  input { margin: 6px 0; padding: 8px; width: 100%; font-size: 16px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
  button { margin-top: 8px; padding: 10px 24px; font-size: 16px; border: none; border-radius: 4px; cursor: pointer; }
  .btn-save { background: #4CAF50; color: white; }
  .btn-save:hover { background: #45a049; }
  .btn-delete { background: #f44336; color: white; padding: 4px 10px; font-size: 13px; margin-left: 8px; }
  .btn-delete:hover { background: #d32f2f; }
  .network-list { list-style: none; padding: 0; margin: 0; }
  .network-list li { display: flex; justify-content: space-between; align-items: center; padding: 8px; border-bottom: 1px solid #eee; }
  .network-list li:last-child { border-bottom: none; }
  .network-name { font-size: 15px; color: #333; }
  .network-signal { font-size: 12px; color: #999; margin-left: 8px; }
  .available-list li { cursor: pointer; }
  .available-list li:hover { background: #f0f0f0; }
  .available-list li.selected { background: #e3f2fd; }
  .info-text { font-size: 13px; color: #777; margin: 8px 0; }
  .divider { border: none; border-top: 1px solid #eee; margin: 16px 0; }
</style>
</head>
<body>
<h2>WiFi Setup</h2>

<div class="section">
  <h3>Saved Networks</h3>
  <ul class="network-list" id="saved-list">
    <li class="info-text">No saved networks</li>
  </ul>
</div>

<hr class="divider">

<div class="section">
  <h3>Available Networks</h3>
  <p class="info-text">Scanning...</p>
  <ul class="network-list available-list" id="available-list">
  </ul>
</div>

<hr class="divider">

<div class="section">
  <h3>Add New Network</h3>
  <form action="/save" method="post">
    <input name="ssid" placeholder="Network SSID" required>
    <input name="password" placeholder="Password" type="password">
    <button type="submit" class="btn-save">Save & Connect</button>
  </form>
</div>

<script>
// Scan results are injected by the server
var networks = %NETWORKS_JSON%;
var savedNetworks = %SAVED_JSON%;

// Render saved networks
var savedList = document.getElementById('saved-list');
if (savedNetworks.length > 0) {
  savedList.innerHTML = '';
  savedNetworks.forEach(function(ssid) {
    var li = document.createElement('li');
    li.innerHTML = '<span class="network-name">' + ssid + '</span>';
    var delBtn = document.createElement('button');
    delBtn.className = 'btn-delete';
    delBtn.textContent = 'Remove';
    delBtn.onclick = function(e) {
      e.stopPropagation();
      if (confirm('Remove network "' + ssid + '"?')) {
        fetch('/delete/' + encodeURIComponent(ssid), {method: 'DELETE'})
          .then(function() { location.reload(); });
      }
    };
    li.appendChild(delBtn);
    savedList.appendChild(li);
  });
}

// Render available networks
var availList = document.getElementById('available-list');
if (networks.length > 0) {
  networks.forEach(function(n) {
    var li = document.createElement('li');
    var sigText = n.rssi > -50 ? 'Excellent' : n.rssi > -65 ? 'Good' : n.rssi > -75 ? 'Fair' : 'Weak';
    li.innerHTML = '<span class="network-name">' + n.ssid + '<span class="network-signal">(' + sigText + ')</span></span>';
    li.onclick = function() {
      // Highlight selected
      document.querySelectorAll('.available-list li').forEach(function(el) { el.classList.remove('selected'); });
      li.classList.add('selected');
      // Fill the form
      document.querySelector('input[name="ssid"]').value = n.ssid;
      document.querySelector('input[name="password"]').focus();
    };
    availList.appendChild(li);
  });
} else {
  availList.innerHTML = '<li class="info-text">No networks found</li>';
}
</script>
</body>
</html>
)EOF";

// ---- WebServer route handlers ------------------------------------

void handle_portal_root() {
  // Scan for available networks
  int n = WiFi.scanNetworks();

  // Build networks JSON array
  String networks_json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) networks_json += ",";
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    // Escape special chars in SSID for JSON
    String escaped_ssid = "";
    for (char c : ssid) {
      if (c == '"') escaped_ssid += "\\\"";
      else if (c == '\\') escaped_ssid += "\\\\";
      else escaped_ssid += c;
    }
    networks_json += "{\"ssid\":\"" + escaped_ssid + "\",\"rssi\":" + String(rssi) + "}";
  }
  networks_json += "]";

  // Build saved networks JSON array
  char saved_ssids[MAX_SAVED_NETWORKS][64];
  int saved_count = wifi_config_get_saved_networks(saved_ssids, MAX_SAVED_NETWORKS);
  String saved_json = "[";
  for (int i = 0; i < saved_count; i++) {
    if (i > 0) saved_json += ",";
    saved_json += "\"" + String(saved_ssids[i]) + "\"";
  }
  saved_json += "]";

  // Replace placeholders in the HTML
  String html = String(portal_html);
  html.replace("%NETWORKS_JSON%", networks_json);
  html.replace("%SAVED_JSON%", saved_json);

  web_server.send(200, "text/html", html);
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

  // Also save to the saved networks list
  wifi_config_add_network(stored_ssid, stored_password);
  wifi_config_set_last_saved(stored_ssid);

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

void handle_delete() {
  String ssid = web_server.pathArg(0);
  if (ssid.length() == 0) {
    web_server.send(400, "text/plain", "Missing SSID");
    return;
  }

  wifi_config_delete_network(ssid.c_str());

  // If this was the stored credential, clear it
  if (ssid == String(stored_ssid)) {
    stored_ssid[0] = '\0';
    stored_password[0] = '\0';
  }

  web_server.send(200, "text/plain", "OK");
}

void handle_not_found() {
  web_server.send(404, "text/plain", "Not Found");
}

// ---- helpers to manage AP + portal lifecycle ---------------------

static void start_captive_portal(const char* ap_ssid) {
  WIFI_DBG_LN("[WiFi] start_captive_portal: starting...");

  // Clear stale scan results
  WiFi.scanDelete();

  // Start AP
  WIFI_DBG("[WiFi] start_captive_portal: calling WiFi.softAP('%s')\r\n", ap_ssid);
  WiFi.softAP(ap_ssid);
  delay(500);

  // Register routes on the web server
  web_server.on("/", HTTP_GET, handle_portal_root);
  web_server.on("/save", HTTP_POST, handle_save);
  web_server.on("/delete/{ssid}", HTTP_DELETE, handle_delete);
  web_server.onNotFound(handle_not_found);
  WIFI_DBG_LN("[WiFi] start_captive_portal: calling web_server.begin()");
  web_server.begin();

  // Attach TinyPortal: starts DNS hijack + captive detection on the server
  WIFI_DBG_LN("[WiFi] start_captive_portal: calling portal.begin()");
  portal.begin(web_server);

  // Show captive portal instructions on display
  request_showBootInstructions("WiFi Setup");
  request_display_line1("Connect to: ");
  request_display_line2(ap_ssid);

  WIFI_DBG_LN("[WiFi] start_captive_portal: done");
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
  WIFI_DBG_LN("[WiFi] wifi_config_init: reading NVS...");

  get_prefs().begin("wifi", true);  // read-only

  char tmp_ssid[64]    = {0};
  char tmp_pass[64]    = {0};

  get_prefs().getString("ssid", tmp_ssid, sizeof(tmp_ssid));
  get_prefs().getString("password", tmp_pass, sizeof(tmp_pass));
  get_prefs().end();

  // If NVS has no SSID, the caller must enter captive portal mode.
  _needs_portal = (tmp_ssid[0] == '\0');

  WIFI_DBG_LN("[WiFi] wifi_config_init: ssid='" + String(tmp_ssid) + "' password='" + String(tmp_pass) + "' portal=" + String(_needs_portal));
  if (_needs_portal) {
    // Clear the buffers so wifi_config_connect() knows no credentials exist.
    stored_ssid[0]    = '\0';
    stored_password[0] = '\0';
    WIFI_DBG_LN("[WiFi] wifi_config_init: no credentials in NVS — portal required");
  } else {
    // Copy loaded credentials to the global stored buffers.
    strncpy(stored_ssid, tmp_ssid, sizeof(stored_ssid) - 1);
    stored_ssid[sizeof(stored_ssid) - 1] = '\0';
    strncpy(stored_password, tmp_pass, sizeof(stored_password) - 1);
    stored_password[sizeof(stored_password) - 1] = '\0';

    portal_saved();
  }
}

/**
 * Connect as STA using the loaded credentials.
 * Returns true on success, false on timeout.
 *
 * Retry logic:
 *   1. Try last_saved_ssid first (if different from stored_ssid)
 *   2. Fall back to stored_ssid
 *   3. If both fail, enter portal mode
 */
bool wifi_config_connect() {
  WIFI_DBG_LN("[WiFi] wifi_config_connect: starting...");

  // Determine which SSIDs to try
  char try_ssid[64];
  char try_pass[64];

  // Get last saved SSID
  const char* last = wifi_config_get_last_saved();

  if (last[0] != '\0' && strcmp(last, stored_ssid) != 0) {
    // Try last saved first
    strncpy(try_ssid, last, sizeof(try_ssid) - 1);
    try_ssid[sizeof(try_ssid) - 1] = '\0';

    // Look up password for last saved
    get_prefs().begin("saved_wifi", true);
    int count = get_prefs().getInt("count", 0);
    strncpy(try_pass, stored_password, sizeof(try_pass) - 1);  // fallback to stored
    for (int i = 0; i < count; i++) {
      String key = "ssid_" + String(i);
      char ssidBuf[65] = {0};           // one byte for NUL terminator
      size_t len = get_prefs().getString(key.c_str(), ssidBuf, sizeof(ssidBuf));
      String existing(ssidBuf);         // convert buffer to Arduino String

      if (existing == last) {
        get_prefs().getString(String("pass_" + String(i)).c_str(), try_pass, sizeof(try_pass));
        break;
      }
    }
    get_prefs().end();

    WIFI_DBG("[WiFi] Trying last saved: '%s'\r\n", try_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(try_ssid, try_pass);

    WIFI_DBG_LN("[WiFi] wifi_config_connect: checking status...");

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      WIFI_DBG(".");
      delay(500);
      if (millis() - start > 15000) {
        WIFI_DBG_LN("[WiFi] Last saved connection timeout");
        WiFi.disconnect();
        delay(100);
        break;
      }
    }

    WIFI_DBG("[WiFi] wifi_config_connect: status: '%s'\r\n", WiFi.status() == WL_CONNECTED ? "connected" : "not connected");

    if (WiFi.status() == WL_CONNECTED) {
      WIFI_DBG("[WiFi] Connected to last saved '%s'! IP=%s\r\n", try_ssid, WiFi.localIP().toString().c_str());
      strncpy(stored_ssid, try_ssid, sizeof(stored_ssid) - 1);
      strncpy(stored_password, try_pass, sizeof(stored_password) - 1);
      WiFi.setSleep(false);
      sync_time();
      request_display_line1("WiFi connected");
      request_display_line2(stored_ssid);
      return true;
    }

    // Jon - display failure on screen
    WIFI_DBG_LN("[WiFi] Last saved connection failed, trying stored...");
  }

  // Fall back to stored_ssid
  if (stored_ssid[0] == '\0') {
    WIFI_DBG_LN("[WiFi] wifi_config_connect: no SSID configured");
    request_display_line1("WiFi not configured");
    request_display_line2("Run wifi_config_init() first");
    return false;
  }

  strncpy(try_ssid, stored_ssid, sizeof(try_ssid) - 1);
  try_ssid[sizeof(try_ssid) - 1] = '\0';
  strncpy(try_pass, stored_password, sizeof(try_pass) - 1);
  try_pass[sizeof(try_pass) - 1] = '\0';

  WIFI_DBG("[WiFi] wifi_config_connect: connecting to '%s' with password '%s'\r\n", try_ssid, try_pass);
  request_display_line1("Connecting to WiFi");
  request_display_line2(try_ssid);

  WiFi.mode(WIFI_STA);

  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(try_ssid, try_pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    WIFI_DBG("+");
    delay(500);
    if (millis() - start > 20000) {
      WIFI_DBG_LN("[WiFi] wifi_config_connect: timeout");
      request_display_line1("WiFi connect timeout");
      request_display_line2("Try again...");
      return false;
    }
  }

  WIFI_DBG("[WiFi] wifi_config_connect: connected! IP=%s\r\n", WiFi.localIP().toString().c_str());
  request_display_line1("WiFi connected");
  request_display_line2(stored_ssid);

  WiFi.setSleep(false);
  sync_time();

  return true;
}

/**
 * Return pointer to the currently loaded SSID string (never null).
 */
const char* wifi_config_ssid() {
  return stored_ssid;
}

/**
 * Clears current SSID to force portal mode.
 */
void wifi_clear_ssid() {
  stored_ssid[0]    = '\0';
  stored_password[0] = '\0';  
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
void wifi_config_enter_portal(String ssid) {
  // Stop any existing portal/AP first
  web_server.stop();
  WiFi.softAPdisconnect(true);
  delay(200);
  WiFi.mode(WIFI_OFF);
  delay(100);

  request_display_line1("WiFi Setup");
  request_display_line2("Enter new credentials");

  start_captive_portal(ssid.c_str());
}

/**
 * Start the captive portal.  Must be called from the loop-based setup
 * phase (after WiFi stack is initialized).
 */
void wifi_config_start_portal() {
  WIFI_DBG_LN("[WiFi] wifi_config_start_portal: starting...");
  WIFI_DBG_LN("[WiFi] wifi_config_start_portal: about to call wifi_config_enter_portal");
  wifi_config_enter_portal("voice-setup");
  WIFI_DBG_LN("[WiFi] wifi_config_start_portal: done");
}

/**
 * Call from main loop() while captive portal is active.
 * Processes DNS hijack requests and web server clients.
 */
void wifi_config_loop() {
  display.routine();
  portal.loop();
  web_server.handleClient();
}
