// wifi_csi.c
//
// Brings up the WiFi station, registers a CSI receive callback (forwarding each
// packet to csi_process), and runs an ICMP ping to the gateway so RX packets —
// and thus CSI samples — keep arriving even when the channel is otherwise idle.

#include "wifi_csi.h"
#include "app_config.h"
#include "csi_process.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_csi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int                s_retry;
static esp_netif_t       *s_netif;
static volatile bool      s_connected;
static char               s_ip[16] = "0.0.0.0";

// --- CSI receive callback: runs in the WiFi task; keep it minimal. ---
static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (info && info->buf) {
        csi_process_submit(info);
    }
}

static esp_err_t enable_csi(void)
{
    wifi_csi_config_t csi_cfg = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    ESP_LOGI(TAG, "CSI capture enabled");
    return ESP_OK;
}

// --- ping keepalive ---------------------------------------------------------
static void on_ping_end(esp_ping_handle_t hdl, void *args) { /* loop restarts it */ }

static void start_gateway_ping(void)
{
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_netif, &ip_info) != ESP_OK) {
        ESP_LOGW(TAG, "no ip info; ping not started");
        return;
    }

    ip_addr_t target = {0};
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = ip_info.gw.addr;   // gateway

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.interval_ms = PING_INTERVAL_MS;
    cfg.count = ESP_PING_COUNT_INFINITE;
    cfg.timeout_ms = PING_INTERVAL_MS;          // don't queue up if a reply is late

    esp_ping_callbacks_t cbs = { .on_ping_end = on_ping_end };
    esp_ping_handle_t ping;
    if (esp_ping_new_session(&cfg, &cbs, &ping) == ESP_OK) {
        esp_ping_start(ping);
        ESP_LOGI(TAG, "pinging gateway " IPSTR " every %d ms for CSI traffic",
                 IP2STR(&ip_info.gw), PING_INTERVAL_MS);
    } else {
        ESP_LOGW(TAG, "failed to start ping session");
    }
}

// --- event handling ---------------------------------------------------------
static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        strcpy(s_ip, "0.0.0.0");
        if (s_retry < WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "reconnecting (%d/%d)", s_retry, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "connected, ip=%s", s_ip);
        s_retry = 0;
        s_connected = true;
        start_gateway_ping();
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_csi_start(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // Lock the power-save off so CSI packets are not missed while dozing.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    enable_csi();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "failed to connect to %s", WIFI_SSID);
    return ESP_FAIL;
}

bool wifi_csi_is_connected(void)
{
    return s_connected;
}

void wifi_csi_get_ip(char *out, size_t len)
{
    if (out && len) {
        strncpy(out, s_ip, len);
        out[len - 1] = '\0';
    }
}
