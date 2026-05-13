# First-flash bring-up for the Waveshare 1.75 port

This is the checklist for taking the firmware from "compiles cleanly" to
"running on hardware". Status: tasks 1-7 and 9 are done. The build at
`fe45d5b` / `92ee89f` is flashable. Tasks 8 (round UI rework) and 10 (nssm
service install) are still open and best done *after* first-light.

## 0. Prerequisites

- Waveshare ESP32-S3-Touch-AMOLED-1.75 + USB-C cable
- PlatformIO is already installed at `%USERPROFILE%\.platformio` — no
  re-install needed.
- Daemon venv already exists at
  `Clawdmeter-1.75\daemon\.venv\` with `requests` installed.

## 1. Fill in WiFi credentials

Edit `firmware/src/secrets.h` (gitignored, only on this machine):

```c
#define WIFI_SSID      "YourSSID"
#define WIFI_PASSWORD  "YourPassword"
```

Leave `secrets.example.h` alone — it's the committed template.

## 2. Plug in and detect the COM port

```powershell
# Plug in the board, then:
[System.IO.Ports.SerialPort]::GetPortNames()
```

The Waveshare 1.75 enumerates as an Espressif USB JTAG/serial debug unit.
On Windows it'll show up as e.g. `COM5` or `COM7`. If nothing shows up:

- Check Device Manager for a yellow-bang on "USB Serial Device". Install
  the WCH or Espressif USB driver as prompted by Windows Update.
- Some early boards need a one-time double-press of BOOT before the chip
  enumerates. After flashing, this is no longer needed.

## 3. Flash the firmware

Run from any shell. Replace `COM3` with whatever port you found:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run `
    -d "c:\Users\SanskarShrestha\source\repos\Clawdmeter\Clawdmeter-1.75\firmware" `
    -t upload --upload-port COM3
```

Successful upload ends with `Hard resetting via RTS pin...` and the board
reboots.

## 4. Watch the boot log

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -p COM3 -b 115200
```

You should see, in order:

```
{"ready":true}
AXP2101 init OK
Touch init OK                  ← if "Touch init failed", see §7
WiFi: connecting to 'YourSSID'...
WiFi: connected, ip=192.168.x.y rssi=-NN
HTTP: listening on :80 (POST /usage, GET /status)
Dashboard ready, waiting for usage POSTs on WiFi...
```

Note the IP — you'll need it for the daemon and the curl test. Ctrl+C to
detach (the monitor doesn't reset the chip on exit).

## 5. Verify the HTTP server from this machine

In a separate shell:

```powershell
# Status check (the firmware echoes wifi state, ip, rssi, uptime):
curl.exe http://192.168.50.48/status

# Hand-craft a usage POST to verify the LVGL UI updates:
# (assign to variable first — PowerShell mangles single-quoted JSON passed directly to curl.exe)
$body = '{"s":42,"sr":120,"w":18,"wr":7200,"st":"allowed","ok":true}'
curl.exe -X POST http://192.168.50.48/usage -H "Content-Type: application/json" -d $body
```

The board should respond `{"ok":true}` to the POST. On the device, the
Usage screen's session bar should jump to 42%. If the splash is showing,
the mood-group animation should also switch — usage_rate.cpp picks a new
splash group when the session % crosses certain thresholds.

## 6. Start the daemon for real

Edit `daemon/config.json` (copy from `config.example.json`) and set
`esp32_ip` to the IP from step 4:

```powershell
copy "c:\Users\SanskarShrestha\source\repos\Clawdmeter\Clawdmeter-1.75\daemon\config.example.json" `
     "c:\Users\SanskarShrestha\source\repos\Clawdmeter\Clawdmeter-1.75\daemon\config.json"
notepad "c:\Users\SanskarShrestha\source\repos\Clawdmeter\Clawdmeter-1.75\daemon\config.json"

# Run it (Ctrl+C to stop):
& "c:\Users\SanskarShrestha\source\repos\Clawdmeter\Clawdmeter-1.75\daemon\.venv\Scripts\python.exe" `
  "c:\Users\SanskarShrestha\source\repos\Clawdmeter\Clawdmeter-1.75\daemon\clawdmeter_daemon.py" -v
```

Every 60 s you should see `payload: {...}` followed by either nothing
(success — 200 OK from the board) or `esp32 POST failed: ...` if the
board went off the LAN.

## 7. If something is wrong

### Panel stays black
- Check serial for `AXP2101 init OK` — if missing, the I2C bus is dead.
  Reseat the cable, try a different USB port.
- If AXP2101 init says OK but display is still black: pinout mismatch.
  The 1.75 pins in `display_cfg.h` are copied from the 2.16 reference
  schematic. Cross-check against the Waveshare 1.75 demo's
  `display_cfg.h` (or equivalent) at
  https://github.com/waveshareteam/Touch_AMOLED — if any of
  LCD_CS/SCLK/SDIO0..3/RESET disagree, update `firmware/src/display_cfg.h`
  to match and reflash.

### `Touch init failed` in serial
- The CST9217 I2C address in `display_cfg.h` is `0x5A`, the same as the
  CST9220 family. If `Touch init failed` appears, the alternate value to
  try is `0x55`. Change `CST9217_ADDR` in `display_cfg.h`, reflash, and
  watch the log.
- If both addresses fail, do an I2C scan to confirm what's actually on
  the bus. Quick scanner sketch lives in any Wire example; or temporarily
  patch `power_init()` to walk addrs 0x00..0x7F and log responses.

### WiFi never connects
- Wrong SSID/password in `secrets.h`. Re-edit and reflash.
- 5 GHz-only network — the ESP32-S3 is 2.4 GHz-only.
- Captive portal / WPA Enterprise — neither is supported by the simple
  `WiFi.begin(ssid, password)` call. v1 assumes home-router WPA2-PSK.

### Daemon prints `no anthropic-ratelimit-unified-* headers`
- Anthropic changed the header names since this code was written. Run the
  daemon with `-v` and inspect the raw header list it logs. Update
  `poll_anthropic` in `daemon/clawdmeter_daemon.py` to match. The
  payload-building math (utilization × 100, reset-time minus now in
  minutes) doesn't need changes.

### Daemon prints `anthropic 401`
- The Claude Code OAuth token has expired. Run `claude /login` once;
  the daemon will pick up the new token on its next iteration.

## 8. What's left after first-light

- **Task 8 — round UI rework.** Once the device boots and we can call
  the `screenshot` serial command (see upstream `CLAUDE.md` §"QA your
  own UI changes"), iterating the layout for 466 round is fast: replace
  the rectangular usage bars with `lv_arc` gauges, re-center the
  network screen, possibly trim splash `CELL` from 24 to 23 so the
  canvas fits 460×460 inside the 466 active area.

  `screenshot.sh` at repo root assumes 480×480. Before the first
  screenshot will render correctly, update the script's `WIDTH` /
  `HEIGHT` to 466. (Or wait — task 8 will fold this in.)

- **Task 9 — long-press power-off verification.** Already coded
  (`92ee89f`). Bench test: hold PWR for ~2 s; serial should print
  `power: shutdown via AXP2101` and the panel should go dark. Press
  PWR briefly again to wake. If shutdown doesn't trigger, the LONG_IRQ
  may need a different mask bit — log raw IRQ status in `power_tick`
  to debug.

- **Task 10 — nssm Windows service.** Install `nssm` from
  https://nssm.cc/download , then:
  ```powershell
  nssm install Clawdmeter "<path-to-.venv\Scripts\python.exe>" `
       "<path-to-clawdmeter_daemon.py>"
  nssm set Clawdmeter AppDirectory "<path-to-daemon>"
  nssm set Clawdmeter Start SERVICE_AUTO_START
  nssm start Clawdmeter
  ```
  A scripted version of this lands when we get to task 10.

## 9. Reporting back

When you've worked through this list (or hit something that doesn't
match what's described), paste the relevant serial output and the step
number — I'll pick up from wherever it broke.
