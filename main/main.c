// main.c — wifimap entry point.
//
// Boot order:
//   1. NVS (needed by WiFi, NimBLE, and saved settings)
//   2. CSI processing pipeline (ready before packets arrive)
//   3. WiFi AP+STA (always-on setup AP + station) + CSI capture + gateway ping
//   4. mDNS  -> http://wifimap.local/
//   5. WiFi AP environment scan + BLE passive scan
//   6. Web/radar dashboard + setup page
//   7. Optional cloud export (MQTT / webhook)

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"

#include "app_config.h"
#include "csi_process.h"
#include "wifi_csi.h"
#include "wifi_apscan.h"
#include "ble_scan.h"
#include "web_server.h"
#include "cloud.h"

static const char *TAG = "wifimap";

static void start_mdns(void)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns init failed");
        return;
    }
    mdns_hostname_set(MDNS_HOSTNAME);
    mdns_instance_name_set(MDNS_INSTANCE);
    mdns_service_add(NULL, "_http", "_tcp", WEB_SERVER_PORT, NULL, 0);
    // Custom service so the companion apps can auto-discover the board.
    mdns_service_add(NULL, "_wifimap", "_tcp", WEB_SERVER_PORT, NULL, 0);
    ESP_LOGI(TAG, "mDNS up: http://%s.local/", MDNS_HOSTNAME);
}

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
    ESP_ERROR_CHECK(wifi_csi_start());

    start_mdns();

    ESP_ERROR_CHECK(wifi_apscan_start());
    ESP_ERROR_CHECK(ble_scan_start());
    ESP_ERROR_CHECK(web_server_start());
    cloud_start();

    ESP_LOGI(TAG, "ready");
    ESP_LOGI(TAG, "  dashboard: http://%s.local/  (or the station IP)", MDNS_HOSTNAME);
    ESP_LOGI(TAG, "  setup:     join WiFi '%s' then open http://192.168.4.1/setup",
             SETUP_AP_SSID);
    ESP_LOGI(TAG, "calibrating CSI baseline for %d s; keep the area still", CSI_CALIB_SECONDS);
}
