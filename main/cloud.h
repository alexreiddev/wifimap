// cloud.h — optional export of the status feed to an MQTT broker and/or an HTTP
// webhook. Both are configured at runtime (setup page → NVS); when nothing is
// configured this does nothing.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reads cloud settings from NVS and starts the publisher task if anything is set.
void cloud_start(void);

#ifdef __cplusplus
}
#endif
