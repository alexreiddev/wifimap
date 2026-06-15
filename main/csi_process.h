// csi_process.h — turns raw WiFi CSI packets into a human motion / occupancy signal.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"   // wifi_csi_info_t

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OCCUPANCY_NONE = 0,
    OCCUPANCY_FAR,
    OCCUPANCY_MEDIUM,
    OCCUPANCY_NEAR,
} occupancy_t;

typedef struct {
    bool        presence;       // motion detected above baseline
    float       activity;       // raw sliding-window std of the CSI feature
    float       zone_score;     // 0..1 normalized perturbation strength
    occupancy_t occupancy;      // coarse through-wall zone
    float       confidence;     // 0..1, based on CSI packet rate / calibration
    bool        calibrated;     // baseline calibration finished
    uint32_t    packets;        // total CSI packets processed
} csi_status_t;

// Create queue, mutex and the processing task. Call once at boot.
esp_err_t csi_process_init(void);

// Called from the WiFi CSI rx callback. Copies the packet and queues it; safe to
// drop under pressure. Keep it fast.
void csi_process_submit(const wifi_csi_info_t *info);

// Snapshot the latest status (thread-safe).
void csi_process_get(csi_status_t *out);

// Restart the empty-room baseline calibration.
void csi_process_recalibrate(void);

// Returns the human-readable name for an occupancy zone.
const char *occupancy_str(occupancy_t z);

#ifdef __cplusplus
}
#endif
