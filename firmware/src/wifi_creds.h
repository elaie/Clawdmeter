#pragma once
#include <stdbool.h>
#include <stddef.h>

// NVS-backed WiFi credential storage. Survives reboots and reflashes (unless
// the user erases NVS). When empty, net.cpp falls back to the values in
// secrets.h so existing setups keep working.

#define WIFI_CREDS_MAX_SSID   33   // 32 + NUL
#define WIFI_CREDS_MAX_PASS   65   // 64 + NUL (WPA2 max passphrase)

#ifdef __cplusplus
extern "C" {
#endif

void wifi_creds_init(void);

// True when both SSID and password are non-empty in NVS.
bool wifi_creds_present(void);

// Reads into caller buffers. Returns false (and writes empty strings) if no
// credentials are stored.
bool wifi_creds_load(char* ssid_out, size_t ssid_cap,
                     char* pass_out, size_t pass_cap);

// Persists creds to NVS. Empty SSID is treated as "clear all".
bool wifi_creds_save(const char* ssid, const char* pass);

// Wipes the stored credentials. Caller is responsible for triggering a
// reconnect / setup flow afterwards.
void wifi_creds_clear(void);

#ifdef __cplusplus
}
#endif
