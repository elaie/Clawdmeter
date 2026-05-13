#include "wifi_creds.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// NVS namespace + key names. Keep short — NVS keys are capped at 15 chars.
#define NS_NAME    "wifi"
#define KEY_SSID   "ssid"
#define KEY_PASS   "pass"

static Preferences prefs;

void wifi_creds_init(void) {
    // Lazy: prefs.begin() is called per-op so we don't hold the namespace open.
    // NVS itself is initialized automatically by the WiFi stack at boot.
}

bool wifi_creds_present(void) {
    if (!prefs.begin(NS_NAME, true /* read-only */)) return false;
    bool has_ssid = prefs.isKey(KEY_SSID);
    bool has_pass = prefs.isKey(KEY_PASS);
    bool ok = has_ssid && has_pass;
    if (ok) {
        // An empty stored SSID counts as "not present".
        String s = prefs.getString(KEY_SSID, "");
        ok = (s.length() > 0);
    }
    prefs.end();
    return ok;
}

bool wifi_creds_load(char* ssid_out, size_t ssid_cap,
                     char* pass_out, size_t pass_cap) {
    if (ssid_out && ssid_cap) ssid_out[0] = '\0';
    if (pass_out && pass_cap) pass_out[0] = '\0';

    if (!prefs.begin(NS_NAME, true)) return false;
    String s = prefs.getString(KEY_SSID, "");
    String p = prefs.getString(KEY_PASS, "");
    prefs.end();

    if (s.length() == 0) return false;
    if (ssid_out && ssid_cap) strlcpy(ssid_out, s.c_str(), ssid_cap);
    if (pass_out && pass_cap) strlcpy(pass_out, p.c_str(), pass_cap);
    return true;
}

bool wifi_creds_save(const char* ssid, const char* pass) {
    if (!ssid) return false;
    if (!prefs.begin(NS_NAME, false /* read-write */)) return false;
    bool ok;
    if (ssid[0] == '\0') {
        prefs.remove(KEY_SSID);
        prefs.remove(KEY_PASS);
        ok = true;
    } else {
        size_t s_wrote = prefs.putString(KEY_SSID, ssid);
        size_t p_wrote = prefs.putString(KEY_PASS, pass ? pass : "");
        ok = (s_wrote > 0) && (p_wrote >= 0);
    }
    prefs.end();
    return ok;
}

void wifi_creds_clear(void) {
    if (!prefs.begin(NS_NAME, false)) return;
    prefs.remove(KEY_SSID);
    prefs.remove(KEY_PASS);
    prefs.end();
}
