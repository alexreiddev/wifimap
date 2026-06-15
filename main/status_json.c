// status_json.c — gather live state and serialize it to JSON.

#include "status_json.h"
#include "app_config.h"
#include "csi_process.h"
#include "ble_scan.h"
#include "wifi_apscan.h"
#include "wifi_csi.h"

#include <stdio.h>
#include "esp_timer.h"
#include "cJSON.h"

static void mac_to_str(const uint8_t *m, char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

char *status_json_build(void)
{
    csi_status_t csi;
    csi_process_get(&csi);

    static ble_device_t bdev[BLE_MAX_DEVICES];
    int bn = ble_scan_get(bdev, BLE_MAX_DEVICES);

    static ap_record_t aps[AP_MAX_RECORDS];
    int an = wifi_apscan_get(aps, AP_MAX_RECORDS);

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddBoolToObject(root, "presence", csi.presence);
    cJSON_AddNumberToObject(root, "activity", csi.activity);
    cJSON_AddStringToObject(root, "occupancy", occupancy_str(csi.occupancy));
    cJSON_AddNumberToObject(root, "zone_score", csi.zone_score);
    cJSON_AddNumberToObject(root, "csi_confidence", csi.confidence);
    cJSON_AddBoolToObject(root, "csi_calibrated", csi.calibrated);
    cJSON_AddNumberToObject(root, "csi_packets", csi.packets);
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);

    // device / network info (handy for the UI and discovery)
    char ip[16], ssid[33];
    wifi_csi_get_ip(ip, sizeof(ip));
    wifi_csi_get_ssid(ssid, sizeof(ssid));
    cJSON_AddBoolToObject(root, "connected", wifi_csi_is_connected());
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "setup_ap", SETUP_AP_SSID);
    cJSON_AddStringToObject(root, "mdns", MDNS_HOSTNAME ".local");

    cJSON *devs = cJSON_AddArrayToObject(root, "devices_ble");
    for (int i = 0; i < bn; i++) {
        char mac[18];
        mac_to_str(bdev[i].mac, mac);
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "mac", mac);
        cJSON_AddStringToObject(d, "name", bdev[i].name);
        cJSON_AddNumberToObject(d, "rssi", bdev[i].rssi);
        cJSON_AddNumberToObject(d, "dist_m", bdev[i].dist_m);
        cJSON_AddNumberToObject(d, "dist_err_m", bdev[i].dist_err_m);
        cJSON_AddNumberToObject(d, "bearing_deg", bdev[i].bearing_deg);
        cJSON_AddNumberToObject(d, "confidence", bdev[i].confidence);
        cJSON_AddItemToArray(devs, d);
    }

    cJSON *aparr = cJSON_AddArrayToObject(root, "aps");
    for (int i = 0; i < an; i++) {
        char bssid[18];
        mac_to_str(aps[i].bssid, bssid);
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", aps[i].ssid);
        cJSON_AddStringToObject(a, "bssid", bssid);
        cJSON_AddNumberToObject(a, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(a, "ch", aps[i].channel);
        cJSON_AddNumberToObject(a, "dist_m", aps[i].dist_m);
        cJSON_AddNumberToObject(a, "bearing_deg", aps[i].bearing_deg);
        cJSON_AddNumberToObject(a, "confidence", aps[i].confidence);
        cJSON_AddItemToArray(aparr, a);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
