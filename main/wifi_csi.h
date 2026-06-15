// wifi_csi.h — WiFi station bring-up, CSI capture, and a gateway-ping keepalive
// that guarantees a steady stream of RX packets (and therefore CSI samples).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize NIC, connect to WIFI_SSID, enable CSI, and start the ping task.
// Blocks until connected (or returns an error after WIFI_MAX_RETRY attempts).
esp_err_t wifi_csi_start(void);

// True once the station has an IP.
bool wifi_csi_is_connected(void);

// Copies the current IPv4 address as a string ("0.0.0.0" if not connected).
void wifi_csi_get_ip(char *out, size_t len);

#ifdef __cplusplus
}
#endif
