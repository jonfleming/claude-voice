#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>

// Initialize the WiFi config module (call once from setup()).
// Loads credentials from NVS or enters captive portal if none saved.
void wifi_config_init();

// Attempt to connect to the configured network with retry timeout.
// Returns true on success; blocks until connected or timed out.
bool wifi_config_connect();

// Get the currently configured SSID (empty string if not set).
const char* wifi_config_ssid();

// Get the currently configured password (empty string if not set).
const char* wifi_config_password();

// Force entry into captive-portal config mode. Call from serial handler
// or a long-press callback. Re-enters AP mode until credentials are saved.
void wifi_config_enter_portal();

// Start the captive portal.  Must be called from the loop-based setup
// phase (after WiFi stack is initialized), not from setup().
void wifi_config_start_portal();

// Call from main loop() while captive portal is active.
// Processes DNS hijack requests and handles web server clients.
void wifi_config_loop();

// Saved networks list management
int wifi_config_get_saved_networks(char out_ssid[][64], int max_networks);
bool wifi_config_add_network(const char* ssid, const char* password);
bool wifi_config_delete_network(const char* ssid);
const char* wifi_config_get_last_saved();
void wifi_config_set_last_saved(const char* ssid);

#endif // WIFI_CONFIG_H
