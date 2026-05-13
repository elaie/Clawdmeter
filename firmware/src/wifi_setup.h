#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Drives the on-device WiFi setup flow. State machine sits behind the
// SCREEN_WIFI_SETUP UI; the UI observes wifi_setup_get_state() and renders
// the matching sub-view.

typedef enum {
    WIFI_SETUP_IDLE = 0,
    WIFI_SETUP_SCAN_START,   // (transient) kicked off, waiting for async to begin
    WIFI_SETUP_SCANNING,
    WIFI_SETUP_PICK,         // scan results ready, waiting for user choice
    WIFI_SETUP_ENTER_PASS,
    WIFI_SETUP_CONNECTING,
    WIFI_SETUP_FAILED,
    WIFI_SETUP_DONE,         // creds saved, ready for normal operation
} wifi_setup_state_t;

#define WIFI_SETUP_MAX_RESULTS 24

#ifdef __cplusplus
extern "C" {
#endif

void wifi_setup_init(void);
void wifi_setup_begin(void);    // enter setup (kicks off a scan)
void wifi_setup_cancel(void);   // abandon and reset to IDLE
void wifi_setup_tick(void);     // call once per main loop

wifi_setup_state_t wifi_setup_get_state(void);

// Scan results — valid while state is PICK / ENTER_PASS / CONNECTING / FAILED.
int          wifi_setup_scan_count(void);
const char*  wifi_setup_scan_ssid(int i);
int8_t       wifi_setup_scan_rssi(int i);
bool         wifi_setup_scan_open(int i);   // true if no auth needed
void         wifi_setup_rescan(void);

// User actions
void wifi_setup_pick_network(int i);
void wifi_setup_submit_password(const char* pass);
void wifi_setup_back_to_list(void);  // ENTER_PASS or FAILED → PICK
void wifi_setup_retry_connect(void); // FAILED → CONNECTING with last attempt

// Reads after PICK
const char* wifi_setup_get_selected_ssid(void);
bool        wifi_setup_selected_is_open(void);

// Error reason for the FAILED state (human-readable).
const char* wifi_setup_get_error(void);

#ifdef __cplusplus
}
#endif
