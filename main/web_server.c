// web_server.c
//
// Routes:
//   GET /              -> embedded radar dashboard (web/index.html)
//   GET /api/status    -> JSON snapshot of CSI presence + BLE devices + APs
//   POST /api/recalibrate -> restart CSI baseline calibration
//   POST /api/apscan   -> trigger an immediate WiFi AP scan

#include "web_server.h"
#include "app_config.h"
#include "csi_process.h"
#include "ble_scan.h"
#include "wifi_apscan.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static void mac_to_str(const uint8_t *m, char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    const size_t len = index_html_end - index_html_start;
    return httpd_resp_send(req, index_html_start, len);
}

static esp_err_t status_get(httpd_req_t *req)
{
    csi_status_t csi;
    csi_process_get(&csi);

    static ble_device_t bdev[BLE_MAX_DEVICES];
    int bn = ble_scan_get(bdev, BLE_MAX_DEVICES);

    static ap_record_t aps[AP_MAX_RECORDS];
    int an = wifi_apscan_get(aps, AP_MAX_RECORDS);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "presence", csi.presence);
    cJSON_AddNumberToObject(root, "activity", csi.activity);
    cJSON_AddStringToObject(root, "occupancy", occupancy_str(csi.occupancy));
    cJSON_AddNumberToObject(root, "zone_score", csi.zone_score);
    cJSON_AddNumberToObject(root, "csi_confidence", csi.confidence);
    cJSON_AddBoolToObject(root, "csi_calibrated", csi.calibrated);
    cJSON_AddNumberToObject(root, "csi_packets", csi.packets);
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);

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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "{}");
    cJSON_free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t recalibrate_post(httpd_req_t *req)
{
    csi_process_recalibrate();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t apscan_post(httpd_req_t *req)
{
    wifi_apscan_trigger();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = root_get },
        { .uri = "/api/status",      .method = HTTP_GET,  .handler = status_get },
        { .uri = "/api/recalibrate", .method = HTTP_POST, .handler = recalibrate_post },
        { .uri = "/api/apscan",      .method = HTTP_POST, .handler = apscan_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    ESP_LOGI(TAG, "web server on port %d", WEB_SERVER_PORT);
    return ESP_OK;
}
