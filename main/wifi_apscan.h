// wifi_apscan.h — periodic environment scan of nearby WiFi access points.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char    ssid[33];
    uint8_t bssid[6];
    int8_t  rssi;
    uint8_t channel;
    float   dist_m;
    float   bearing_deg;   // ESTIMATED, not measured
    float   confidence;
} ap_record_t;

// Starts a background task that scans once now, then every AP_SCAN_INTERVAL_MS.
// Note: each scan briefly interrupts CSI capture (channel hopping).
esp_err_t wifi_apscan_start(void);

// Trigger an immediate scan on the next loop.
void wifi_apscan_trigger(void);

// Copy the latest results; returns count written.
int wifi_apscan_get(ap_record_t *out, int max);

#ifdef __cplusplus
}
#endif
