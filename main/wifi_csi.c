// wifi_csi.c
//
// Brings up WiFi in AP+STA mode:
//   - AP  "wifimap-setup" is always on, so the setup page is reachable from a
//     phone even before the board has joined any network (zero-config onboarding).
//   - STA joins the user's network (credentials from NVS, falling back to the
//     compile-time defaults in app_config.h) for CSI + the LAN dashboard.
//
// It also registers the CSI receive callback (forwarding each packet to
// csi_process) and pings the gateway so RX packets — and thus CSI samples — keep
// arriving even when the channel is otherwise idle.

#include "wifi_csi.h"
#include "app_config.h"
#include "csi_process.h"
#include "provisioning.h"

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

static esp_netif_t  *s_sta_netif;
static volatile bool s_connected;
static char          s_ip[16] = "0.0.0.0";
static char          s_ssid[33] = "";
static bool          s_ping_started;

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
    if (s_ping_started) {
        return;   // one persistent infinite session is enough
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) != ESP_OK) {
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
        s_ping_started = true;
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
        if (s_ssid[0] != '\0') {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        strcpy(s_ip, "0.0.0.0");
        // Keep retrying forever; the setup AP stays available meanwhile so the
        // user can fix bad credentials without a reboot.
        if (s_ssid[0] != '\0') {
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "connected, ip=%s", s_ip);
        s_connected = true;
        start_gateway_ping();
    }
}

// Pick station credentials: NVS first, then compile-time defaults (unless they
// are still the placeholder).
static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (provisioning_load_wifi(ssid, ssid_len, pass, pass_len)) {
        return true;
    }
    if (strcmp(WIFI_SSID, "your-ssid") != 0 && WIFI_SSID[0] != '\0') {
        strncpy(ssid, WIFI_SSID, ssid_len - 1); ssid[ssid_len - 1] = '\0';
        strncpy(pass, WIFI_PASS, pass_len - 1); pass[pass_len - 1] = '\0';
        return true;
    }
    return false;
}

static void set_sta_config(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;  // allow open or any secured AP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
}

esp_err_t wifi_csi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        event_handler, NULL, NULL));

    // Always-on setup access point.
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid_len = (uint8_t)strlen(SETUP_AP_SSID),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, SETUP_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    char ssid[33], pass[65];
    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        set_sta_config(ssid, pass);
        ESP_LOGI(TAG, "station configured for '%s'", ssid);
    } else {
        ESP_LOGW(TAG, "no WiFi credentials — join '%s' and open the setup page",
                 SETUP_AP_SSID);
    }

    // Lock power-save off so CSI packets are not missed while dozing.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    enable_csi();
    return ESP_OK;
}

void wifi_csi_apply_sta(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        return;
    }
    provisioning_save_wifi(ssid, pass);
    esp_wifi_disconnect();
    set_sta_config(ssid, pass);
    esp_wifi_connect();
    ESP_LOGI(TAG, "applied new station credentials for '%s'", ssid);
}

bool wifi_csi_is_connected(void) { return s_connected; }

void wifi_csi_get_ip(char *out, size_t len)
{
    if (out && len) {
        strncpy(out, s_ip, len);
        out[len - 1] = '\0';
    }
}

void wifi_csi_get_ssid(char *out, size_t len)
{
    if (out && len) {
        strncpy(out, s_ssid, len);
        out[len - 1] = '\0';
    }
}
