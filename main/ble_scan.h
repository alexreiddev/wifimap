// ble_scan.h — continuous passive BLE advertisement scan with an RSSI-based
// distance/confidence estimate and an (illustrative, not measured) bearing.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  mac[6];
    char     name[32];
    int8_t   rssi;
    float    dist_m;        // RSSI path-loss estimate
    float    dist_err_m;    // rough +/- from RSSI jitter
    float    bearing_deg;   // ESTIMATED/animated, not physically measured
    float    confidence;    // 0..1 from signal strength + stability
    int64_t  last_seen_us;
    bool     in_use;
} ble_device_t;

esp_err_t ble_scan_start(void);

// Copy the currently-live devices into out[]; returns the count written.
int ble_scan_get(ble_device_t *out, int max);

#ifdef __cplusplus
}
#endif
