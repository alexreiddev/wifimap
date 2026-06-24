// csi_process.c
//
// Pipeline:
//   wifi rx cb -> csi_process_submit() -> queue -> csi_task
//   csi_task: per packet compute a scalar amplitude feature, push into a sliding
//   window, compute the window std ("activity"). During the first
//   CSI_CALIB_SECONDS we learn an empty-room baseline (mean + std of activity).
//   Afterwards presence/occupancy are derived from how far current activity sits
//   above that baseline (a z-score), which we also map to coarse through-wall
//   occupancy zones.
//
// This is intentionally lightweight integer/float math so it keeps up with the
// CSI packet rate on a single core.

#include "csi_process.h"
#include "app_config.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "csi";

// Max CSI payload we copy per packet. HT-LTF can be up to ~384 bytes; 512 is safe.
#define CSI_BUF_MAX 512

typedef struct {
    int16_t len;
    int8_t  buf[CSI_BUF_MAX];
} csi_packet_t;

static QueueHandle_t      s_queue;
static SemaphoreHandle_t  s_lock;
static csi_status_t       s_status;          // shared, guarded by s_lock

// Sliding window of the per-packet amplitude feature.
static float    s_window[CSI_WINDOW_LEN];
static int      s_win_count;
static int      s_win_head;

// Baseline calibration accumulators.
static bool     s_calibrating = true;
static int64_t  s_calib_start_us;
static double    s_base_sum;
static double    s_base_sum_sq;
static uint32_t  s_base_n;
static float     s_base_mean;
static float     s_base_std;

const char *occupancy_str(occupancy_t z)
{
    switch (z) {
        case OCCUPANCY_FAR:    return "far";
        case OCCUPANCY_MEDIUM: return "medium";
        case OCCUPANCY_NEAR:   return "near";
        default:               return "none";
    }
}

// Mean amplitude across the valid subcarriers of one CSI packet.
// CSI buffer holds interleaved (imag, real) int8 pairs per subcarrier.
static float packet_feature(const csi_packet_t *p)
{
    int pairs = p->len / 2;
    if (pairs <= 0) {
        return 0.0f;
    }
    double sum = 0.0;
    int used = 0;
    for (int i = 0; i < pairs; i++) {
        int8_t imag = p->buf[2 * i];
        int8_t real = p->buf[2 * i + 1];
        // Skip null/guard subcarriers (both components zero).
        if (imag == 0 && real == 0) {
            continue;
        }
        sum += sqrt((double)(imag * imag + real * real));
        used++;
    }
    return used > 0 ? (float)(sum / used) : 0.0f;
}

// Compute std of the current sliding window.
static float window_std(void)
{
    if (s_win_count < 2) {
        return 0.0f;
    }
    double sum = 0.0;
    for (int i = 0; i < s_win_count; i++) {
        sum += s_window[i];
    }
    double mean = sum / s_win_count;
    double var = 0.0;
    for (int i = 0; i < s_win_count; i++) {
        double d = s_window[i] - mean;
        var += d * d;
    }
    var /= (s_win_count - 1);
    return (float)sqrt(var);
}

static occupancy_t zone_from_z(float z)
{
    if (z >= CSI_ZONE_NEAR_Z)   return OCCUPANCY_NEAR;
    if (z >= CSI_ZONE_MEDIUM_Z) return OCCUPANCY_MEDIUM;
    if (z >= CSI_ZONE_FAR_Z)    return OCCUPANCY_FAR;
    return OCCUPANCY_NONE;
}

static void csi_task(void *arg)
{
    csi_packet_t pkt;
    s_calib_start_us = esp_timer_get_time();

    while (1) {
        if (xQueueReceive(s_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        float feat = packet_feature(&pkt);

        // Push into ring window.
        if (s_win_count < CSI_WINDOW_LEN) {
            s_window[s_win_count++] = feat;
        } else {
            s_window[s_win_head] = feat;
            s_win_head = (s_win_head + 1) % CSI_WINDOW_LEN;
        }

        float activity = window_std();

        // --- calibration phase ---
        if (s_calibrating) {
            // Only start averaging once the window has filled, so activity is meaningful.
            if (s_win_count >= CSI_WINDOW_LEN) {
                s_base_sum += activity;
                s_base_sum_sq += (double)activity * activity;
                s_base_n++;
            }
            int64_t elapsed = esp_timer_get_time() - s_calib_start_us;
            if (elapsed >= (int64_t)CSI_CALIB_SECONDS * 1000000 && s_base_n > 5) {
                s_base_mean = (float)(s_base_sum / s_base_n);
                double m = s_base_sum / s_base_n;
                double v = s_base_sum_sq / s_base_n - m * m;
                s_base_std = v > 0 ? (float)sqrt(v) : 0.0f;
                if (s_base_std < 1e-3f) {
                    s_base_std = 1e-3f;   // avoid divide-by-zero on a perfectly still channel
                }
                s_calibrating = false;
                ESP_LOGI(TAG, "calibration done: mean=%.3f std=%.3f", s_base_mean, s_base_std);
            }
        }

        // --- detection phase ---
        float z = 0.0f;
        bool presence = false;
        occupancy_t occ = OCCUPANCY_NONE;
        if (!s_calibrating) {
            z = (activity - s_base_mean) / s_base_std;
            presence = z > CSI_PRESENCE_K;
            occ = zone_from_z(z);
        }

        // zone_score: 0..1 normalized perturbation, saturating at the NEAR threshold.
        float zone_score = z / CSI_ZONE_NEAR_Z;
        if (zone_score < 0) zone_score = 0;
        if (zone_score > 1) zone_score = 1;

        // confidence: ramps up while window fills, and reflects calibration state.
        float fill = (float)s_win_count / (float)CSI_WINDOW_LEN;
        float confidence = s_calibrating ? (0.5f * fill) : (0.5f + 0.5f * fill);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.presence   = presence;
        s_status.activity   = activity;
        s_status.zone_score = zone_score;
        s_status.occupancy  = occ;
        s_status.confidence = confidence;
        s_status.calibrated = !s_calibrating;
        s_status.packets++;
        xSemaphoreGive(s_lock);
    }
}

esp_err_t csi_process_init(void)
{
    s_queue = xQueueCreate(64, sizeof(csi_packet_t));
    s_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_lock) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    BaseType_t ok = xTaskCreate(csi_task, "csi_task", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

void csi_process_submit(const wifi_csi_info_t *info)
{
    if (!s_queue || !info || !info->buf || info->len <= 0) {
        return;
    }
    csi_packet_t pkt;
    pkt.len = info->len > CSI_BUF_MAX ? CSI_BUF_MAX : info->len;
    memcpy(pkt.buf, info->buf, pkt.len);
    // Non-blocking: drop if the consumer falls behind rather than stalling WiFi.
    xQueueSend(s_queue, &pkt, 0);
}

void csi_process_get(csi_status_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

void csi_process_recalibrate(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_calibrating = true;
    s_calib_start_us = esp_timer_get_time();
    s_base_sum = s_base_sum_sq = 0;
    s_base_n = 0;
    s_win_count = s_win_head = 0;
    s_status.calibrated = false;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "recalibration requested");
}
