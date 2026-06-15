// app_config.h — central tunables for wifimap.
//
// Edit WIFI_SSID / WIFI_PASS to your network. The ESP32 connects as a station so
// it can (a) generate steady RX traffic for CSI by pinging the gateway and
// (b) serve the radar dashboard on your LAN.
#pragma once

// ---------------------------------------------------------------------------
// WiFi credentials.
// You normally DON'T need to edit these: on first boot the board hosts a setup
// WiFi (SETUP_AP_SSID) where you enter your network from a phone, and the values
// are saved to flash (NVS). These compile-time values are only used as a fallback
// if you prefer to bake credentials in. Leave them as the placeholder to force
// the setup portal.
// ---------------------------------------------------------------------------
#define WIFI_SSID        "your-ssid"
#define WIFI_PASS        "your-password"
#define WIFI_MAX_RETRY   10

// Always-on setup access point (open network). Connect a phone to this and open
// http://192.168.4.1/ to configure WiFi (and optional cloud export).
#define SETUP_AP_SSID    "wifimap-setup"

// mDNS hostname → the dashboard is reachable at http://<MDNS_HOSTNAME>.local/
#define MDNS_HOSTNAME    "wifimap"
#define MDNS_INSTANCE    "wifimap radar"

// ---------------------------------------------------------------------------
// CSI / ping
// ---------------------------------------------------------------------------
// How often to ping the gateway to keep CSI packets flowing (milliseconds).
#define PING_INTERVAL_MS        50      // ~20 packets/sec
// Sliding window length (number of recent CSI packets) used for the motion metric.
#define CSI_WINDOW_LEN          128
// Duration of the automatic empty-room baseline calibration at boot (seconds).
#define CSI_CALIB_SECONDS       10
// presence = activity > baseline_mean + CSI_PRESENCE_K * baseline_std
#define CSI_PRESENCE_K          3.0f

// Through-wall occupancy zone thresholds, expressed as the normalized perturbation
// z = (activity - baseline_mean) / baseline_std. Larger z ⇒ stronger/closer motion.
#define CSI_ZONE_FAR_Z          3.0f    // z >= this ⇒ at least "far" occupancy
#define CSI_ZONE_MEDIUM_Z       6.0f    // z >= this ⇒ "medium"
#define CSI_ZONE_NEAR_Z         12.0f   // z >= this ⇒ "near"

// ---------------------------------------------------------------------------
// BLE scanning
// ---------------------------------------------------------------------------
#define BLE_MAX_DEVICES         48
// Drop a device from the table if not seen for this long (milliseconds).
#define BLE_DEVICE_TTL_MS       30000
// Reference RSSI at 1 m (dBm) and path-loss exponent for the distance estimate
// dist = 10 ^ ((TX_POWER_REF - rssi) / (10 * PATH_LOSS_N)).
#define RSSI_TX_POWER_REF       (-59)
#define PATH_LOSS_N             2.5f

// ---------------------------------------------------------------------------
// WiFi AP environment scan
// ---------------------------------------------------------------------------
#define AP_MAX_RECORDS          32
// Re-scan the AP environment on this slow cadence (milliseconds). Each scan
// briefly interrupts CSI, so keep it large. 0 disables periodic re-scan.
#define AP_SCAN_INTERVAL_MS     120000

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
#define WEB_SERVER_PORT         80

// ---------------------------------------------------------------------------
// Cloud export (optional; configured at runtime from the setup page / NVS)
// ---------------------------------------------------------------------------
// How often to publish the status JSON to MQTT / the webhook (milliseconds).
#define CLOUD_PUBLISH_MS        1000
// MQTT topic the status JSON is published to.
#define MQTT_TOPIC              "wifimap/status"
