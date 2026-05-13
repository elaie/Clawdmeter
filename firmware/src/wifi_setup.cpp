#include "wifi_setup.h"
#include "net.h"
#include "wifi_creds.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

// ---- State ----
static wifi_setup_state_t s_state = WIFI_SETUP_IDLE;

// Scan results, copied out of Arduino-WiFi-owned memory so they survive
// past the next scan call.
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t enc;  // wifi_auth_mode_t
} scan_row_t;

static scan_row_t s_results[WIFI_SETUP_MAX_RESULTS];
static int        s_result_count = 0;

static int        s_selected = -1;
static char       s_last_pass[WIFI_CREDS_MAX_PASS] = {0};
static char       s_error[64] = {0};

// Connect attempt watchdog.
#define CONNECT_TIMEOUT_MS 20000
static uint32_t s_connect_started_ms = 0;

// ---- Helpers ----
static void copy_scan_results(int n) {
    if (n < 0) n = 0;
    if (n > WIFI_SETUP_MAX_RESULTS) n = WIFI_SETUP_MAX_RESULTS;

    // Build a temp index sorted by RSSI desc. Avoids sorting WiFi's internal
    // result array (which we don't own).
    int idx[WIFI_SETUP_MAX_RESULTS];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {
        int j = i;
        while (j > 0 && WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[j - 1])) {
            int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t;
            j--;
        }
    }

    s_result_count = 0;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(idx[i]);
        if (ssid.length() == 0) continue;  // hidden — skip for now
        // Dedupe by SSID name (multi-AP setups advertise many BSSIDs).
        bool dup = false;
        for (int k = 0; k < s_result_count; k++) {
            if (strcmp(s_results[k].ssid, ssid.c_str()) == 0) { dup = true; break; }
        }
        if (dup) continue;
        strlcpy(s_results[s_result_count].ssid, ssid.c_str(),
                sizeof(s_results[s_result_count].ssid));
        s_results[s_result_count].rssi = WiFi.RSSI(idx[i]);
        s_results[s_result_count].enc  = (uint8_t)WiFi.encryptionType(idx[i]);
        s_result_count++;
        if (s_result_count >= WIFI_SETUP_MAX_RESULTS) break;
    }
    WiFi.scanDelete();
    Serial.printf("wifi_setup: scan returned %d networks (showing %d)\n", n, s_result_count);
}

static void start_scan(void) {
    // Make sure we're in STA mode and not currently associating to something
    // — scans during a connect attempt frequently fail.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    s_result_count = 0;
    s_state = WIFI_SETUP_SCAN_START;
    int rc = WiFi.scanNetworks(true /* async */, false /* show_hidden */);
    // -1 = WIFI_SCAN_RUNNING, -2 = WIFI_SCAN_FAILED. -1 is success here.
    if (rc == WIFI_SCAN_FAILED) {
        Serial.println("wifi_setup: scanNetworks returned WIFI_SCAN_FAILED");
        strlcpy(s_error, "Scan failed — try again", sizeof(s_error));
        s_state = WIFI_SETUP_FAILED;
        return;
    }
    s_state = WIFI_SETUP_SCANNING;
}

// ---- API ----
void wifi_setup_init(void) {
    s_state = WIFI_SETUP_IDLE;
    s_result_count = 0;
}

void wifi_setup_begin(void) {
    Serial.println("wifi_setup: begin");
    s_error[0] = '\0';
    s_selected = -1;
    s_last_pass[0] = '\0';
    start_scan();
}

void wifi_setup_cancel(void) {
    s_state = WIFI_SETUP_IDLE;
}

void wifi_setup_rescan(void) {
    s_error[0] = '\0';
    start_scan();
}

void wifi_setup_tick(void) {
    switch (s_state) {
    case WIFI_SETUP_SCANNING: {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        if (n == WIFI_SCAN_FAILED) {
            strlcpy(s_error, "Scan failed — try again", sizeof(s_error));
            s_state = WIFI_SETUP_FAILED;
            return;
        }
        copy_scan_results(n);
        s_state = WIFI_SETUP_PICK;
        return;
    }
    case WIFI_SETUP_CONNECTING: {
        wl_status_t s = WiFi.status();
        if (s == WL_CONNECTED) {
            Serial.printf("wifi_setup: connected to '%s', saving creds\n",
                          s_results[s_selected].ssid);
            // Persist + hand control back to net.cpp via the same call so
            // the live SSID/pass buffers match NVS.
            if (!net_apply_new_credentials(s_results[s_selected].ssid, s_last_pass)) {
                strlcpy(s_error, "Saved to memory but NVS write failed", sizeof(s_error));
            }
            s_state = WIFI_SETUP_DONE;
            return;
        }
        if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
            snprintf(s_error, sizeof(s_error),
                     (s == WL_NO_SSID_AVAIL) ? "Network not found" : "Wrong password?");
            s_state = WIFI_SETUP_FAILED;
            return;
        }
        if (millis() - s_connect_started_ms > CONNECT_TIMEOUT_MS) {
            strlcpy(s_error, "Connection timed out", sizeof(s_error));
            s_state = WIFI_SETUP_FAILED;
            return;
        }
        return;
    }
    default:
        return;
    }
}

wifi_setup_state_t wifi_setup_get_state(void) { return s_state; }

int          wifi_setup_scan_count(void)        { return s_result_count; }
const char*  wifi_setup_scan_ssid(int i)        {
    if (i < 0 || i >= s_result_count) return "";
    return s_results[i].ssid;
}
int8_t       wifi_setup_scan_rssi(int i) {
    if (i < 0 || i >= s_result_count) return -127;
    return s_results[i].rssi;
}
bool         wifi_setup_scan_open(int i) {
    if (i < 0 || i >= s_result_count) return false;
    return s_results[i].enc == WIFI_AUTH_OPEN;
}

void wifi_setup_pick_network(int i) {
    if (i < 0 || i >= s_result_count) return;
    s_selected = i;
    s_last_pass[0] = '\0';
    if (s_results[i].enc == WIFI_AUTH_OPEN) {
        // No password needed — jump straight to CONNECTING.
        s_connect_started_ms = millis();
        net_try_credentials(s_results[i].ssid, "");
        s_state = WIFI_SETUP_CONNECTING;
    } else {
        s_state = WIFI_SETUP_ENTER_PASS;
    }
}

void wifi_setup_submit_password(const char* pass) {
    if (s_selected < 0 || s_selected >= s_result_count) return;
    if (!pass) pass = "";
    strlcpy(s_last_pass, pass, sizeof(s_last_pass));
    s_connect_started_ms = millis();
    net_try_credentials(s_results[s_selected].ssid, s_last_pass);
    s_state = WIFI_SETUP_CONNECTING;
}

void wifi_setup_back_to_list(void) {
    s_error[0] = '\0';
    s_state = WIFI_SETUP_PICK;
}

void wifi_setup_retry_connect(void) {
    if (s_selected < 0) {
        s_state = WIFI_SETUP_PICK;
        return;
    }
    s_error[0] = '\0';
    s_connect_started_ms = millis();
    net_try_credentials(s_results[s_selected].ssid, s_last_pass);
    s_state = WIFI_SETUP_CONNECTING;
}

const char* wifi_setup_get_selected_ssid(void) {
    if (s_selected < 0 || s_selected >= s_result_count) return "";
    return s_results[s_selected].ssid;
}

bool wifi_setup_selected_is_open(void) {
    return (s_selected >= 0) && (s_results[s_selected].enc == WIFI_AUTH_OPEN);
}

const char* wifi_setup_get_error(void) { return s_error; }
