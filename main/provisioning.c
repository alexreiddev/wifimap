// provisioning.c — NVS-backed runtime configuration.

#include "provisioning.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "prov";
static const char *NS = "wifimap";

static bool get_str(nvs_handle_t h, const char *key, char *out, size_t len)
{
    size_t n = len;
    if (nvs_get_str(h, key, out, &n) == ESP_OK && out[0] != '\0') {
        return true;
    }
    out[0] = '\0';
    return false;
}

bool provisioning_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (ssid_len) ssid[0] = '\0';
    if (pass_len) pass[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool have = get_str(h, "ssid", ssid, ssid_len);
    get_str(h, "pass", pass, pass_len);
    nvs_close(h);
    return have;
}

void provisioning_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return;
    }
    nvs_set_str(h, "ssid", ssid ? ssid : "");
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved wifi credentials for '%s'", ssid ? ssid : "");
}

void provisioning_clear_wifi(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "pass");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "cleared wifi credentials");
}

bool provisioning_load_cloud(char *mqtt_uri, size_t mqtt_len,
                             bool *mqtt_enabled,
                             char *webhook_url, size_t webhook_len)
{
    if (mqtt_len) mqtt_uri[0] = '\0';
    if (webhook_len) webhook_url[0] = '\0';
    if (mqtt_enabled) *mqtt_enabled = false;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool any = false;
    if (get_str(h, "mqtt_uri", mqtt_uri, mqtt_len)) any = true;
    if (get_str(h, "hook_url", webhook_url, webhook_len)) any = true;
    uint8_t en = 0;
    if (nvs_get_u8(h, "mqtt_en", &en) == ESP_OK && mqtt_enabled) {
        *mqtt_enabled = en != 0;
    }
    nvs_close(h);
    return any;
}

void provisioning_save_cloud(const char *mqtt_uri, bool mqtt_enabled,
                             const char *webhook_url)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "mqtt_uri", mqtt_uri ? mqtt_uri : "");
    nvs_set_str(h, "hook_url", webhook_url ? webhook_url : "");
    nvs_set_u8(h, "mqtt_en", mqtt_enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved cloud settings (mqtt_en=%d)", mqtt_enabled);
}
