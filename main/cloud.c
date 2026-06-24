// cloud.c — publish the status JSON to MQTT and/or an HTTP webhook.

#include "cloud.h"
#include "app_config.h"
#include "provisioning.h"
#include "status_json.h"
#include "wifi_csi.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "mqtt_client.h"

static const char *TAG = "cloud";

static esp_mqtt_client_handle_t s_mqtt;
static bool  s_mqtt_enabled;
static char  s_webhook[256];

static void post_webhook(const char *json)
{
    if (s_webhook[0] == '\0') {
        return;
    }
    esp_http_client_config_t cfg = {
        .url = s_webhook,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 2000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, json, strlen(json));
    esp_err_t err = esp_http_client_perform(c);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "webhook POST failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(c);
}

static void cloud_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CLOUD_PUBLISH_MS));
        if (!wifi_csi_is_connected()) {
            continue;   // nothing leaves the box until the station is online
        }
        char *json = status_json_build();
        if (!json) continue;

        if (s_mqtt_enabled && s_mqtt) {
            esp_mqtt_client_publish(s_mqtt, MQTT_TOPIC, json, 0, 0, 0);
        }
        post_webhook(json);
        free(json);
    }
}

void cloud_start(void)
{
    char mqtt_uri[256];
    bool mqtt_en = false;
    if (!provisioning_load_cloud(mqtt_uri, sizeof(mqtt_uri), &mqtt_en,
                                 s_webhook, sizeof(s_webhook))) {
        ESP_LOGI(TAG, "no cloud export configured");
        return;
    }

    if (mqtt_en && mqtt_uri[0] != '\0') {
        esp_mqtt_client_config_t cfg = {
            .broker.address.uri = mqtt_uri,
        };
        s_mqtt = esp_mqtt_client_init(&cfg);
        if (s_mqtt && esp_mqtt_client_start(s_mqtt) == ESP_OK) {
            s_mqtt_enabled = true;
            ESP_LOGI(TAG, "MQTT export to %s topic %s", mqtt_uri, MQTT_TOPIC);
        } else {
            ESP_LOGW(TAG, "MQTT client failed to start");
        }
    }

    if (s_webhook[0] != '\0') {
        ESP_LOGI(TAG, "webhook export to %s", s_webhook);
    }

    if (s_mqtt_enabled || s_webhook[0] != '\0') {
        xTaskCreate(cloud_task, "cloud", 6144, NULL, 4, NULL);
    }
}
