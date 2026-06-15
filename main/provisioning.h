// provisioning.h — persistent (NVS) storage for WiFi credentials and cloud
// export settings, so the device is configured at runtime instead of by editing
// source and recompiling.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFi station credentials. Returns true if a non-empty SSID is stored.
bool provisioning_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void provisioning_save_wifi(const char *ssid, const char *pass);
void provisioning_clear_wifi(void);

// Cloud export settings (optional). Returns true if anything is stored.
bool provisioning_load_cloud(char *mqtt_uri, size_t mqtt_len,
                             bool *mqtt_enabled,
                             char *webhook_url, size_t webhook_len);
void provisioning_save_cloud(const char *mqtt_uri, bool mqtt_enabled,
                             const char *webhook_url);

#ifdef __cplusplus
}
#endif
