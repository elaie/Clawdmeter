#pragma once
#include <stdint.h>

enum net_state_t {
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING,
    NET_STATE_CONNECTED,
};

// net_init() reads stored credentials and brings up WiFi if any exist.
// If nothing's stored (and secrets.h has placeholders), it leaves WiFi off
// and the caller (main.cpp) drops into the setup flow.
void net_init(void);
void net_tick(void);

// True when there are usable creds (NVS or secrets.h fallback). When false,
// main.cpp routes the user to the WiFi setup screen instead of the splash.
bool net_has_credentials(void);

// Used by wifi_setup.cpp. Persists the creds, disconnects, and reconnects
// with the new SSID/password. Returns false on NVS write failure (rare).
bool net_apply_new_credentials(const char* ssid, const char* pass);

// Used by the setup screen to start a connection attempt without persisting
// to NVS yet. Caller polls net_get_state() and commits via the persisted
// path once WL_CONNECTED is reached.
void net_try_credentials(const char* ssid, const char* pass);

net_state_t net_get_state(void);
const char* net_get_ssid(void);
const char* net_get_ip(void);
int8_t      net_get_rssi(void);

bool        net_has_data(void);
const char* net_get_data(void);
void        net_consume_data(void);
