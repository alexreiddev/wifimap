// wifi_apscan.c
//
// Background task that periodically runs a blocking WiFi scan and stores the
// results for the dashboard. Scans hop channels and therefore briefly interrupt
// CSI capture, so the cadence is deliberately slow (AP_SCAN_INTERVAL_MS).

#include "wifi_apscan.h"
#include "app_config.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "apscan";

static ap_record_t       s_aps[AP_MAX_RECORDS];
static int               s_count;
static SemaphoreHandle_t s_lock;
static volatile bool     s_trigger;

static float rssi_to_distance(int8_t rssi)
{
    float d = powf(10.0f, ((float)RSSI_TX_POWER_REF - (float)rssi) / (10.0f * PATH_LOSS_N));
    if (d < 0.1f) d = 0.1f;
    return d;
}

static float estimated_bearing(const uint8_t *bssid)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ bssid[i]) * 16777619u;
    return (float)(h % 3600) / 10.0f;   // stable per-BSSID, 0..360
}

static void do_scan(void)
{
    wifi_scan_config_t cfg = { .show_hidden = true };
    esp_err_t err = esp_wifi_scan_start(&cfg, true /* block */);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t num = AP_MAX_RECORDS;
    static wifi_ap_record_t records[AP_MAX_RECORDS];
    if (esp_wifi_scan_get_ap_records(&num, records) != ESP_OK) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_count = 0;
    for (int i = 0; i < num && s_count < AP_MAX_RECORDS; i++) {
        ap_record_t *a = &s_aps[s_count++];
        strncpy(a->ssid, (const char *)records[i].ssid, sizeof(a->ssid) - 1);
        a->ssid[sizeof(a->ssid) - 1] = '\0';
        memcpy(a->bssid, records[i].bssid, 6);
        a->rssi = records[i].rssi;
        a->channel = records[i].primary;
        a->dist_m = rssi_to_distance(records[i].rssi);
        a->bearing_deg = estimated_bearing(records[i].bssid);
        float strength = (records[i].rssi + 100.0f) / 60.0f;
        if (strength < 0) strength = 0; if (strength > 1) strength = 1;
        a->confidence = strength;
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "scanned %d access points", s_count);
}

static void apscan_task(void *arg)
{
    // Initial scan shortly after boot.
    vTaskDelay(pdMS_TO_TICKS(2000));
    do_scan();

    int64_t accum = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        accum += 1000;
        if (s_trigger || (AP_SCAN_INTERVAL_MS > 0 && accum >= AP_SCAN_INTERVAL_MS)) {
            s_trigger = false;
            accum = 0;
            do_scan();
        }
    }
}

esp_err_t wifi_apscan_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    BaseType_t ok = xTaskCreate(apscan_task, "apscan", 4096, NULL, 4, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

void wifi_apscan_trigger(void)
{
    s_trigger = true;
}

int wifi_apscan_get(ap_record_t *out, int max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count < max ? s_count : max;
    memcpy(out, s_aps, n * sizeof(ap_record_t));
    xSemaphoreGive(s_lock);
    return n;
}
