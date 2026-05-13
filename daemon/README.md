# Clawdmeter daemon (Python, WiFi+HTTP)

Cross-platform replacement for the upstream bash daemon. Reads the Claude
Code OAuth token, polls Anthropic for rate-limit headers, and POSTs the
small usage payload to the Clawdmeter ESP32 over HTTP.

Runs on Windows (PowerShell), macOS, and Linux.

## Let Claude Code do it

If you have [Claude Code](https://claude.com/claude-code) installed, drop
this prompt into a session opened in the repo root — it'll handle the
venv, deps, board discovery, and first run for you:

> Set up the Clawdmeter daemon in `daemon/`. Create a Python venv inside
> `daemon/.venv`, install `daemon/requirements.txt` into it, then run
> `python discover.py --probe` to verify the ESP32 is reachable on the LAN.
> If a device is found, copy `config.example.json` to `config.json` (only
> if it doesn't already exist), set `esp32_name` to the discovered
> device's name, and start the daemon with `python clawdmeter_daemon.py -v`
> in the background. Show me the first payload it posts so I can confirm
> end-to-end. If discovery returns nothing, stop and tell me — the board
> is probably offline.

It'll also nudge you to `claude /login` if your credentials aren't where
the daemon expects them.

## Quick start — manual

### Windows (PowerShell)

```powershell
cd daemon
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt

# Test against Anthropic without an ESP32 in the loop:
python clawdmeter_daemon.py --dry-run --once -v

# Real run — auto-discovers the ESP32 via mDNS, no IP needed:
copy config.example.json config.json
python clawdmeter_daemon.py
```

### macOS / Linux

```bash
cd daemon
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# Test against Anthropic without an ESP32 in the loop:
python clawdmeter_daemon.py --dry-run --once -v

# Real run — auto-discovers the ESP32 via mDNS, no IP needed:
cp config.example.json config.json
python clawdmeter_daemon.py
```

macOS has Bonjour built in — `discover.py` will see the board with no
extra setup. On Linux, install `avahi-daemon` if `clawdmeter.local`
doesn't resolve (it's the standard mDNS responder; on Debian/Ubuntu
`sudo apt install avahi-daemon`, on Fedora `sudo dnf install avahi`).

## Pairing / discovery

Each board carries a per-device name baked in at flash time
(`CLAWDMETER_NAME` in `firmware/src/secrets.h`). That name becomes both
the mDNS hostname (`<name>.local`) and the service-instance string the
daemon matches against — so you can flash several boards as
`living-room`, `desk`, `kitchen`, etc., and pin the daemon to a specific
one.

Resolution order:

1. `--esp32-ip` CLI flag
2. `esp32_ip` in `config.json`
3. `CLAWDMETER_ESP32_IP` env var
4. `discovered.json` cache (skipped when a name filter is set)
5. Fresh mDNS scan, filtered by `--device` / `esp32_name` if provided
6. Fallback: resolve `<name>.local` via the OS resolver

If three POSTs in a row fail and the IP came from discovery (not an explicit
override), the cache is invalidated and another scan runs — covers DHCP renewal
and physically swapping boards.

### Scanning the network

Same commands on every platform:

```bash
python discover.py                          # list every clawdmeter on the LAN
python discover.py --device living-room     # find one specific board
python discover.py --probe                  # also GET /status from each hit
python discover.py --first                  # print only the first IP, exit 1 if none
```

Example output with two boards:

```
  living-room  192.168.50.48:80  living-room.local.
  desk         192.168.50.71:80  desk.local.
```

### Pinning the daemon to a named device

Either pass it on the CLI:

```bash
python clawdmeter_daemon.py --device living-room
```

…or set it once in `config.json`:

```json
{ "esp32_name": "living-room" }
```

With a name pinned, the daemon refuses to fall back to any other board
even if a different one answers first.

### Flashing a custom name

Edit `firmware/src/secrets.h` before flashing:

```c
#define CLAWDMETER_NAME "living-room"
```

Rules: lowercase letters, digits, hyphens. No spaces. Must be unique on
your LAN — two boards advertising the same name will collide on mDNS.

## Configuration

`config.json` (next to the script) is the primary source. CLI flags override
the file. Environment variable `CLAWDMETER_ESP32_IP` is consulted only when
neither flag nor file provides one.

| Key                   | Default                                | Notes |
| --------------------- | -------------------------------------- | ----- |
| `esp32_ip`            | `null` (auto-discover)                 | Pin an IP only if you want to bypass mDNS — useful across VLANs. |
| `esp32_name`          | `null` (first responder wins)          | Match a specific device by its `CLAWDMETER_NAME`. Required when running multiple boards on the same LAN. |
| `poll_interval_s`     | 60                                     | Seconds between Anthropic polls. |
| `request_timeout_s`   | 15                                     | HTTP timeout for both Anthropic and ESP32 calls. |
| `credentials_path`    | `~/.claude/.credentials.json` (auto)   | Override if your token lives elsewhere. |
| `discovery_enabled`   | `true`                                 | Set to `false` (or pass `--no-discovery`) to require an explicit IP. |
| `discovery_timeout_s` | 6                                      | mDNS browse window. |

Environment variable `CLAWDMETER_DEVICE` also sets `esp32_name` when neither
the flag nor the config provides one.

## Payload shape

What goes in `POST /usage` matches the upstream firmware contract:

```json
{"s": 12.5, "sr": 213, "w": 4.0, "wr": 7080, "st": "allowed", "ok": true}
```

- `s`  — 5-hour utilization, percent (rounded to 1 decimal).
- `sr` — minutes until the 5-hour window resets (clamped to >= 0).
- `w`  — 7-day utilization, percent.
- `wr` — minutes until the 7-day window resets.
- `st` — rate-limit status string (`allowed`, `limited`, ...).
- `ok` — always `true` when the daemon was able to talk to Anthropic.

## Troubleshooting

- **`accessToken not found`** — run `claude /login` once, or check that
  the credentials file exists and contains a valid token:
  - Windows: `%USERPROFILE%\.claude\.credentials.json`
  - macOS / Linux: `~/.claude/.credentials.json`
- **`no anthropic-ratelimit-unified-* headers`** — Anthropic may have
  renamed or dropped these headers since this code was written. Run with
  `-v` and inspect the logged header list; update `poll_anthropic` to
  match.
- **`esp32 POST failed`** — confirm the board is on the same LAN
  (`curl http://<ip>/status`) and your local firewall isn't blocking
  outbound HTTP to the LAN.
- **`could not locate the ESP32`** — run `python discover.py -v` to see
  what mDNS finds. Most-common causes by platform:
  - **All**: the board hasn't joined WiFi yet (check the Network screen
    on the device), or daemon and board are on different VLANs / SSIDs —
    mDNS doesn't cross subnets.
  - **Windows**: Defender Firewall may block incoming UDP 5353. Allow
    `python.exe` on private networks, or pin the IP via `--esp32-ip`.
  - **macOS**: usually just works (Bonjour is built in). If `dns-sd -B
    _clawdmeter._tcp` shows the board but the daemon doesn't, you're
    likely on a VPN that's routing multicast away.
  - **Linux**: install `avahi-daemon` if `<name>.local` won't resolve.

  As a last resort, read the IP off the board's Network screen and pin
  it via `--esp32-ip <ip>` or `esp32_ip` in `config.json`.

## Run on boot

### Windows — Task Scheduler

Run "Task Scheduler" → Create Basic Task → Trigger: At log on → Action:
Start a program. Point it at:

```
Program:    c:\path\to\daemon\.venv\Scripts\pythonw.exe
Arguments:  c:\path\to\daemon\clawdmeter_daemon.py
Start in:   c:\path\to\daemon
```

`pythonw.exe` (no console window) is the silent variant. For a real
service that runs without anyone logged in, wrap it with
[nssm](https://nssm.cc): `nssm install ClawdmeterDaemon
<path-to-python> clawdmeter_daemon.py`.

### macOS — launchd

Save the following as `~/Library/LaunchAgents/com.clawdmeter.daemon.plist`,
filling in the absolute paths:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.clawdmeter.daemon</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Users/you/path/to/daemon/.venv/bin/python</string>
    <string>/Users/you/path/to/daemon/clawdmeter_daemon.py</string>
  </array>
  <key>WorkingDirectory</key><string>/Users/you/path/to/daemon</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/clawdmeter.log</string>
  <key>StandardErrorPath</key><string>/tmp/clawdmeter.log</string>
</dict>
</plist>
```

Load it with `launchctl load ~/Library/LaunchAgents/com.clawdmeter.daemon.plist`.
Unload with `launchctl unload ...`.

### Linux — systemd --user

Save as `~/.config/systemd/user/clawdmeter.service`:

```ini
[Unit]
Description=Clawdmeter usage daemon
After=network-online.target

[Service]
ExecStart=/home/you/path/to/daemon/.venv/bin/python /home/you/path/to/daemon/clawdmeter_daemon.py
WorkingDirectory=/home/you/path/to/daemon
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
```

Then `systemctl --user daemon-reload && systemctl --user enable --now clawdmeter`.
Logs via `journalctl --user -u clawdmeter -f`.
