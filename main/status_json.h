// status_json.h — builds the /api/status JSON document from the live CSI, BLE and
// AP state. Shared by the web server and the cloud (MQTT/webhook) publisher.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Returns a newly-allocated JSON string (free with free()), or NULL on OOM.
char *status_json_build(void);

#ifdef __cplusplus
}
#endif
