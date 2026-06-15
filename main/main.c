// main.c — wifimap entry point.
//
// Boot order:
//   1. NVS (needed by WiFi + NimBLE)
//   2. CSI processing pipeline (so it's ready before packets arrive)
//   3. WiFi station + CSI capture + gateway ping
//   4. WiFi AP environment scan task
//   5. BLE passive scan
//   6. Web/radar dashboard

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "csi_process.h"
#include "wifi_csi.h"
#include "wifi_apscan.h"
#include "ble_scan.h"
#include "web_server.h"

static const char *TAG = "wifimap";

void app_main(void)
{
    ESP_LOGI(TAG, "wifimap starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(csi_process_init());

    if (wifi_csi_start() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed; check WIFI_SSID/WIFI_PASS in app_config.h");
        // Continue anyway so BLE scanning + dashboard still come up.
    }

    ESP_ERROR_CHECK(wifi_apscan_start());
    ESP_ERROR_CHECK(ble_scan_start());
    ESP_ERROR_CHECK(web_server_start());

    char ip[16];
    wifi_csi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "ready — open the radar at http://%s/", ip);
    ESP_LOGI(TAG, "calibrating CSI baseline for %d s; keep the area still", CSI_CALIB_SECONDS);
}
