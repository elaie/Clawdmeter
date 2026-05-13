#include "net.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#define NET_PORT          80
#define NET_HOSTNAME      "clawdmeter"
#define NET_DATA_BUF_SIZE 512
#define NET_RECONNECT_MS  5000

static net_state_t state = NET_STATE_DISCONNECTED;
static WebServer   server(NET_PORT);

static char ssid_buf[33] = {0};
static char ip_buf[16]   = "0.0.0.0";
static int8_t rssi_cache = 0;

static char data_buf[NET_DATA_BUF_SIZE];
static volatile bool has_data = false;

static uint32_t last_reconnect_ms = 0;
static bool     server_started   = false;

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
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(NET_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    strlcpy(ssid_buf, WIFI_SSID, sizeof(ssid_buf));
    state = NET_STATE_CONNECTING;
    last_reconnect_ms = millis();
    Serial.printf("WiFi: connecting to '%s'...\n", WIFI_SSID);
}

void net_init(void) {
    server.on("/usage", HTTP_POST, handle_post_usage);
    server.on("/status", HTTP_GET, handle_get_status);
    server.onNotFound(handle_not_found);
    start_wifi();
}

void net_tick(void) {
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
        }
        server.handleClient();
    } else {
        if (state == NET_STATE_CONNECTED) {
            state = NET_STATE_CONNECTING;
            strlcpy(ip_buf, "0.0.0.0", sizeof(ip_buf));
            Serial.println("WiFi: link lost, reconnecting...");
        }
        uint32_t now = millis();
        if (now - last_reconnect_ms > NET_RECONNECT_MS) {
            last_reconnect_ms = now;
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
