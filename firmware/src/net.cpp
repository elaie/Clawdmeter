#include "net.h"
#include "secrets.h"
#include "wifi_creds.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <string.h>

// CLAWDMETER_NAME comes from secrets.h. It's the per-device identifier baked
// in at flash time, used as both the mDNS hostname (<name>.local) and the
// service instance name the daemon matches with --device. Must be DNS-safe:
// letters, digits, hyphen, no spaces, lowercase recommended. Two devices on
// the same LAN MUST have different names — mDNS hostnames must be unique.
#ifndef CLAWDMETER_NAME
#define CLAWDMETER_NAME   "clawdmeter"
#endif

#define NET_PORT          80
#define NET_HOSTNAME      CLAWDMETER_NAME
// mDNS service-type the daemon browses for. The instance name (what the user
// sees in `discover.py`) is the device name; the service-type stays constant
// so the daemon can find every clawdmeter regardless of what it's called.
#define NET_MDNS_SERVICE  "clawdmeter"
#define NET_MDNS_PROTO    "tcp"
#define NET_DATA_BUF_SIZE 512
#define NET_RECONNECT_MS  5000

static net_state_t state = NET_STATE_DISCONNECTED;
static WebServer   server(NET_PORT);
static bool        mdns_started = false;

static char ssid_buf[WIFI_CREDS_MAX_SSID] = {0};
static char pass_buf[WIFI_CREDS_MAX_PASS] = {0};
static char ip_buf[16]   = "0.0.0.0";
static int8_t rssi_cache = 0;

static char data_buf[NET_DATA_BUF_SIZE];
static volatile bool has_data = false;

static uint32_t last_reconnect_ms = 0;
static bool     server_started   = false;
static bool     have_creds       = false;

// True when the value matches the placeholder shipped in secrets.example.h.
// If the user never replaced these (e.g. they're using the new on-device
// setup flow exclusively), we must NOT auto-connect to them.
static bool is_placeholder_secret(const char* ssid) {
    return (ssid == NULL) || (ssid[0] == '\0') || (strcmp(ssid, "YOUR_SSID") == 0);
}

// Loads the best available credentials into ssid_buf / pass_buf.
// Order: NVS > secrets.h (only if not placeholder).
static bool load_active_creds(void) {
    if (wifi_creds_load(ssid_buf, sizeof(ssid_buf), pass_buf, sizeof(pass_buf))) {
        Serial.printf("net: using credentials from NVS (ssid='%s')\n", ssid_buf);
        return true;
    }
    if (!is_placeholder_secret(WIFI_SSID)) {
        strlcpy(ssid_buf, WIFI_SSID, sizeof(ssid_buf));
        strlcpy(pass_buf, WIFI_PASSWORD, sizeof(pass_buf));
        Serial.printf("net: using credentials from secrets.h (ssid='%s')\n", ssid_buf);
        return true;
    }
    ssid_buf[0] = '\0';
    pass_buf[0] = '\0';
    return false;
}

static void handle_post_usage(void) {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "missing body");
        return;
    }
    const String& body = server.arg("plain");
    size_t n = body.length();
    if (n == 0 || n >= NET_DATA_BUF_SIZE) {
        server.send(400, "text/plain", "bad size");
        return;
    }
    memcpy(data_buf, body.c_str(), n);
    data_buf[n] = '\0';
    has_data = true;
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_get_status(void) {
    rssi_cache = WiFi.RSSI();
    char body[160];
    snprintf(body, sizeof(body),
             "{\"state\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"uptime_ms\":%lu}",
             (int)state, ssid_buf, ip_buf, (int)rssi_cache, (unsigned long)millis());
    server.send(200, "application/json", body);
}

static void handle_not_found(void) {
    server.send(404, "text/plain", "not found");
}

static void start_wifi(void) {
    if (ssid_buf[0] == '\0') {
        Serial.println("WiFi: no credentials — staying idle");
        state = NET_STATE_DISCONNECTED;
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(NET_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid_buf, pass_buf);
    state = NET_STATE_CONNECTING;
    last_reconnect_ms = millis();
    Serial.printf("WiFi: connecting to '%s'...\n", ssid_buf);
}

void net_init(void) {
    wifi_creds_init();
    have_creds = load_active_creds();

    server.on("/usage", HTTP_POST, handle_post_usage);
    server.on("/status", HTTP_GET, handle_get_status);
    server.onNotFound(handle_not_found);

    if (have_creds) {
        start_wifi();
    } else {
        Serial.println("net_init: no stored credentials, deferring WiFi until setup completes");
        state = NET_STATE_DISCONNECTED;
    }
}

bool net_has_credentials(void) {
    return have_creds;
}

void net_try_credentials(const char* ssid, const char* pass) {
    if (!ssid || ssid[0] == '\0') return;
    // Update buffers and re-arm the connect attempt.
    strlcpy(ssid_buf, ssid, sizeof(ssid_buf));
    strlcpy(pass_buf, pass ? pass : "", sizeof(pass_buf));
    have_creds = true;

    if (mdns_started) {
        MDNS.end();
        mdns_started = false;
    }
    WiFi.disconnect(true /* wifi_off */, true /* eraseConfig */);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(NET_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid_buf, pass_buf);
    state = NET_STATE_CONNECTING;
    last_reconnect_ms = millis();
    Serial.printf("WiFi: trying '%s'...\n", ssid_buf);
}

bool net_apply_new_credentials(const char* ssid, const char* pass) {
    if (!ssid || ssid[0] == '\0') return false;
    if (!wifi_creds_save(ssid, pass ? pass : "")) {
        Serial.println("net: failed to persist credentials to NVS");
        return false;
    }
    net_try_credentials(ssid, pass);
    return true;
}

void net_tick(void) {
    if (!have_creds) return;   // setup flow controls WiFi until creds exist
    wl_status_t s = WiFi.status();

    if (s == WL_CONNECTED) {
        if (state != NET_STATE_CONNECTED) {
            state = NET_STATE_CONNECTED;
            strlcpy(ip_buf, WiFi.localIP().toString().c_str(), sizeof(ip_buf));
            Serial.printf("WiFi: connected, ip=%s rssi=%d\n", ip_buf, (int)WiFi.RSSI());
            if (!server_started) {
                server.begin();
                server_started = true;
                Serial.printf("HTTP: listening on :%d (POST /usage, GET /status)\n", NET_PORT);
            }
            // Advertise over mDNS so the daemon can find us without a hardcoded IP.
            // begin() registers the A record (`clawdmeter.local`); addService registers
            // the SRV/TXT records the daemon browses for.
            if (!mdns_started) {
                if (MDNS.begin(NET_HOSTNAME)) {
                    // Instance name = hostname by default. Make it explicit so
                    // the daemon's --device matcher sees a stable value.
                    MDNS.setInstanceName(NET_HOSTNAME);
                    MDNS.addService(NET_MDNS_SERVICE, NET_MDNS_PROTO, NET_PORT);
                    MDNS.addServiceTxt(NET_MDNS_SERVICE, NET_MDNS_PROTO, "path", "/usage");
                    // TXT `name=` duplicates the instance name but is cheap and
                    // makes daemon matching robust against zeroconf naming quirks.
                    MDNS.addServiceTxt(NET_MDNS_SERVICE, NET_MDNS_PROTO, "name", NET_HOSTNAME);
                    mdns_started = true;
                    Serial.printf("mDNS: %s.local advertising _%s._%s on :%d\n",
                                  NET_HOSTNAME, NET_MDNS_SERVICE, NET_MDNS_PROTO, NET_PORT);
                } else {
                    Serial.println("mDNS: begin() failed");
                }
            }
        }
        server.handleClient();
    } else {
        if (state == NET_STATE_CONNECTED) {
            state = NET_STATE_CONNECTING;
            strlcpy(ip_buf, "0.0.0.0", sizeof(ip_buf));
            Serial.println("WiFi: link lost, reconnecting...");
            if (mdns_started) {
                MDNS.end();
                mdns_started = false;
            }
        }
        uint32_t now = millis();
        if (now - last_reconnect_ms > NET_RECONNECT_MS) {
            last_reconnect_ms = now;
            WiFi.disconnect();
            WiFi.begin(ssid_buf, pass_buf);
        }
    }
}

net_state_t net_get_state(void)   { return state; }
const char* net_get_ssid(void)    { return ssid_buf; }
const char* net_get_ip(void)      { return ip_buf; }
int8_t      net_get_rssi(void)    {
    if (state == NET_STATE_CONNECTED) rssi_cache = WiFi.RSSI();
    return rssi_cache;
}

bool        net_has_data(void)    { return has_data; }
const char* net_get_data(void)    { return data_buf; }
void        net_consume_data(void){ has_data = false; }
