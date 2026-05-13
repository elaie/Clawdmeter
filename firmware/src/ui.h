#pragma once
#include "data.h"
#include "net.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_NETWORK,
    SCREEN_WIFI_SETUP,
    SCREEN_COUNT,
};

// Two round-display usage layouts, cycled by long-pressing BOOT on Usage screen.
enum usage_layout_t {
    USAGE_LAYOUT_B_HALVES = 0,      // top half=session arc, bottom half=weekly arc
    USAGE_LAYOUT_C_DOMINANT,        // big session arc fills screen, small weekly pill
    USAGE_LAYOUT_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_cycle_screen(void);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_cycle_usage_layout(void);
usage_layout_t ui_get_usage_layout(void);
void ui_update_net_status(net_state_t state, const char* ssid, const char* ip, int8_t rssi);
void ui_update_battery(int percent, bool charging);

// WiFi setup screen: call once per main loop while on SCREEN_WIFI_SETUP so the
// UI can react to wifi_setup state transitions (scan ready, connecting, etc.).
void ui_wifi_setup_tick(void);

// Trigger entering the WiFi setup flow from outside (e.g. boot path with no
// stored credentials, or the Reconfigure button on the Network screen).
void ui_enter_wifi_setup(void);
