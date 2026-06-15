// wifi_csi.h — WiFi bring-up (AP+STA), CSI capture, and a gateway-ping keepalive
// that guarantees a steady stream of RX packets (and therefore CSI samples).
//
// The board always runs a setup access point (SETUP_AP_SSID) so it can be
// (re)configured from a phone with no recompile, while the station joins the
// user's network for CSI + the dashboard.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up WiFi in AP+STA, enable CSI, and (if credentials are stored or compiled
// in) start connecting the station. Non-blocking: returns once WiFi has started.
esp_err_t wifi_csi_start(void);

// Apply new station credentials at runtime and (re)connect. Persists to NVS.
void wifi_csi_apply_sta(const char *ssid, const char *pass);

// True once the station has an IP.
bool wifi_csi_is_connected(void);

// Station IPv4 as a string ("0.0.0.0" if not connected).
void wifi_csi_get_ip(char *out, size_t len);

// The configured station SSID (may be empty if unprovisioned).
void wifi_csi_get_ssid(char *out, size_t len);

#ifdef __cplusplus
}
#endif
