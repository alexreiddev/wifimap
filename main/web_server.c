// web_server.c
//
// Serves both the radar dashboard and a zero-config setup page, plus a small JSON
// API. Because esp_http_server binds all interfaces, the same server answers on
// the always-on setup AP (192.168.4.1) and on the LAN once the station connects.
//
// Routes:
//   GET  /                 -> radar dashboard (web/index.html)
//   GET  /setup            -> WiFi / cloud setup page (web/setup.html)
//   GET  /api/status       -> JSON snapshot
//   POST /api/save         -> {ssid,pass,mqtt_uri,mqtt_en,hook_url} -> NVS + apply
//   POST /api/forget       -> clear WiFi creds + reboot into setup
//   POST /api/recalibrate  -> restart CSI baseline calibration
//   POST /api/apscan       -> trigger an immediate WiFi AP scan

#include "web_server.h"
#include "app_config.h"
#include "csi_process.h"
#include "wifi_apscan.h"
#include "wifi_csi.h"
#include "provisioning.h"
#include "status_json.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");
extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[]   asm("_binary_setup_html_end");

static esp_err_t send_asset(httpd_req_t *req, const char *start, const char *end)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, start, end - start);
}

static esp_err_t root_get(httpd_req_t *req)
{
    return send_asset(req, index_html_start, index_html_end);
}

static esp_err_t setup_get(httpd_req_t *req)
{
    return send_asset(req, setup_html_start, setup_html_end);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char *json = status_json_build();
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json ? json : "{}");
    free(json);
    return err;
}

// Read the (small) request body into a heap buffer the caller must free.
static char *read_body(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 2048) {
        return NULL;
    }
    char *buf = malloc(total + 1);
    if (!buf) return NULL;
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) { free(buf); return NULL; }
        received += r;
    }
    buf[total] = '\0';
    return buf;
}

static const char *json_str(cJSON *o, const char *key)
{
    cJSON *i = cJSON_GetObjectItem(o, key);
    return (i && cJSON_IsString(i)) ? i->valuestring : "";
}

static esp_err_t save_post(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    cJSON *o = cJSON_Parse(body);
    free(body);
    if (!o) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }

    const char *ssid = json_str(o, "ssid");
    const char *pass = json_str(o, "pass");
    const char *mqtt = json_str(o, "mqtt_uri");
    const char *hook = json_str(o, "hook_url");
    cJSON *en = cJSON_GetObjectItem(o, "mqtt_en");
    bool mqtt_en = en && cJSON_IsTrue(en);

    provisioning_save_cloud(mqtt, mqtt_en, hook);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"ok\":true,\"note\":\"WiFi applied now; cloud settings take effect after reboot\"}");

    if (ssid[0] != '\0') {
        // Apply after responding so the client gets a reply before we reconnect.
        static char s_ssid[33], s_pass[65];
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1); s_ssid[sizeof(s_ssid)-1] = '\0';
        strncpy(s_pass, pass, sizeof(s_pass) - 1); s_pass[sizeof(s_pass)-1] = '\0';
        cJSON_Delete(o);
        vTaskDelay(pdMS_TO_TICKS(300));
        wifi_csi_apply_sta(s_ssid, s_pass);
        return ESP_OK;
    }
    cJSON_Delete(o);
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static esp_err_t forget_post(httpd_req_t *req)
{
    provisioning_clear_wifi();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"note\":\"rebooting into setup\"}");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
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
    config.max_uri_handlers = 12;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = root_get },
        { .uri = "/setup",           .method = HTTP_GET,  .handler = setup_get },
        { .uri = "/api/status",      .method = HTTP_GET,  .handler = status_get },
        { .uri = "/api/save",        .method = HTTP_POST, .handler = save_post },
        { .uri = "/api/forget",      .method = HTTP_POST, .handler = forget_post },
        { .uri = "/api/recalibrate", .method = HTTP_POST, .handler = recalibrate_post },
        { .uri = "/api/apscan",      .method = HTTP_POST, .handler = apscan_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    ESP_LOGI(TAG, "web server on port %d", WEB_SERVER_PORT);
    return ESP_OK;
}
