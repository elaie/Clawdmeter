#pragma once

// Template for secrets.h. Copy this file to secrets.h (gitignored) and
// fill in your local WiFi network. The firmware compiles against secrets.h,
// not this file.
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASSWORD  "YOUR_PASSWORD"

// Per-device name. Becomes `<name>.local` on the network and the value the
// daemon matches with `--device <name>`. Rules: DNS-safe (a-z, 0-9, hyphen),
// lowercase recommended, max ~32 chars, no spaces. Two clawdmeters on the
// same LAN MUST use different names.
#define CLAWDMETER_NAME "clawdmeter"
