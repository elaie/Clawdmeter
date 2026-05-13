#pragma once
#include <stdint.h>

enum net_state_t {
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING,
    NET_STATE_CONNECTED,
};

void net_init(void);
void net_tick(void);

net_state_t net_get_state(void);
const char* net_get_ssid(void);
const char* net_get_ip(void);
int8_t      net_get_rssi(void);

bool        net_has_data(void);
const char* net_get_data(void);
void        net_consume_data(void);
