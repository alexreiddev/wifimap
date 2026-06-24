// ble_scan.c
//
// NimBLE passive GAP discovery. Each advertisement updates a small device table
// keyed by MAC. We derive:
//   - distance  from a log-distance path-loss model on RSSI
//   - confidence from RSSI strength and short-term stability
//   - bearing   as a stable per-MAC pseudo-angle with a slow drift (ILLUSTRATIVE
//               only; a single antenna cannot measure angle of arrival)
// Stale entries are aged out by ble_scan_get().

#include "ble_scan.h"
#include "app_config.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"

static const char *TAG = "ble";

static ble_device_t      s_devices[BLE_MAX_DEVICES];
static SemaphoreHandle_t s_lock;

// Keep a tiny RSSI history per slot for a stability estimate.
static int8_t  s_rssi_hist[BLE_MAX_DEVICES][4];
static uint8_t s_rssi_idx[BLE_MAX_DEVICES];

static float rssi_to_distance(int8_t rssi)
{
    float d = powf(10.0f, ((float)RSSI_TX_POWER_REF - (float)rssi) / (10.0f * PATH_LOSS_N));
    if (d < 0.1f) d = 0.1f;
    return d;
}

// Stable angle from the MAC bytes (0..360), plus a slow shared drift so the radar
// gently rotates. NOT a real bearing.
static float estimated_bearing(const uint8_t *mac)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) {
        h = (h ^ mac[i]) * 16777619u;
    }
    float base = (float)(h % 3600) / 10.0f;                 // 0..360
    float drift = (float)((esp_timer_get_time() / 1000000) % 360); // 1 deg/s
    float b = base + drift;
    while (b >= 360.0f) b -= 360.0f;
    return b;
}

static int find_or_alloc(const uint8_t *mac)
{
    int free_slot = -1;
    int oldest = -1;
    int64_t oldest_us = INT64_MAX;
    for (int i = 0; i < BLE_MAX_DEVICES; i++) {
        if (s_devices[i].in_use && memcmp(s_devices[i].mac, mac, 6) == 0) {
            return i;
        }
        if (!s_devices[i].in_use && free_slot < 0) {
            free_slot = i;
        }
        if (s_devices[i].in_use && s_devices[i].last_seen_us < oldest_us) {
            oldest_us = s_devices[i].last_seen_us;
            oldest = i;
        }
    }
    if (free_slot >= 0) return free_slot;
    return oldest;  // table full: evict the least-recently-seen
}

static void parse_name(const struct ble_hs_adv_fields *f, char *out, size_t len)
{
    out[0] = '\0';
    if (f->name != NULL && f->name_len > 0) {
        size_t n = f->name_len < (len - 1) ? f->name_len : (len - 1);
        memcpy(out, f->name, n);
        out[n] = '\0';
    }
}

static void update_device(const uint8_t *mac, int8_t rssi,
                          const struct ble_hs_adv_fields *fields)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int i = find_or_alloc(mac);
    ble_device_t *d = &s_devices[i];

    bool is_new = !(d->in_use && memcmp(d->mac, mac, 6) == 0);
    if (is_new) {
        memset(d, 0, sizeof(*d));
        memcpy(d->mac, mac, 6);
        s_rssi_idx[i] = 0;
        memset(s_rssi_hist[i], rssi, sizeof(s_rssi_hist[i]));
    }
    d->in_use = true;
    d->rssi = rssi;
    d->last_seen_us = esp_timer_get_time();
    d->dist_m = rssi_to_distance(rssi);
    d->bearing_deg = estimated_bearing(mac);

    char name[32];
    parse_name(fields, name, sizeof(name));
    if (name[0] != '\0') {
        strncpy(d->name, name, sizeof(d->name) - 1);
    }

    // RSSI stability -> dist_err + confidence.
    s_rssi_hist[i][s_rssi_idx[i] % 4] = rssi;
    s_rssi_idx[i]++;
    int n = s_rssi_idx[i] < 4 ? s_rssi_idx[i] : 4;
    float mean = 0;
    for (int k = 0; k < n; k++) mean += s_rssi_hist[i][k];
    mean /= n;
    float var = 0;
    for (int k = 0; k < n; k++) { float e = s_rssi_hist[i][k] - mean; var += e * e; }
    var = n > 1 ? var / (n - 1) : 0;
    float rssi_std = sqrtf(var);

    // err grows with distance and jitter.
    d->dist_err_m = d->dist_m * (0.2f + rssi_std / 20.0f);

    // confidence: stronger and steadier => higher.
    float strength = (rssi + 100.0f) / 60.0f;        // ~ -100dBm->0, -40dBm->1
    if (strength < 0) strength = 0; if (strength > 1) strength = 1;
    float steadiness = 1.0f - (rssi_std / 15.0f);
    if (steadiness < 0) steadiness = 0; if (steadiness > 1) steadiness = 1;
    d->confidence = 0.5f * strength + 0.5f * steadiness;

    xSemaphoreGive(s_lock);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
            update_device(event->disc.addr.val, event->disc.rssi, &fields);
        }
    }
    return 0;
}

static void start_disc(void)
{
    struct ble_gap_disc_params params = {0};
    params.passive = 1;          // listen only, don't send scan requests
    params.itvl = 0;             // use defaults
    params.window = 0;
    params.filter_duplicates = 0; // keep RSSI fresh
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE passive scan started");
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    start_disc();
}

static void host_task(void *param)
{
    nimble_port_run();           // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

esp_err_t ble_scan_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", err);
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

int ble_scan_get(ble_device_t *out, int max)
{
    int64_t now = esp_timer_get_time();
    int64_t ttl_us = (int64_t)BLE_DEVICE_TTL_MS * 1000;
    int count = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < BLE_MAX_DEVICES && count < max; i++) {
        if (!s_devices[i].in_use) continue;
        if (now - s_devices[i].last_seen_us > ttl_us) {
            s_devices[i].in_use = false;   // age out
            continue;
        }
        out[count++] = s_devices[i];
    }
    xSemaphoreGive(s_lock);
    return count;
}
