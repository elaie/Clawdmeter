# Clawdmeter daemon (Python, WiFi+HTTP)

Cross-platform replacement for the upstream bash daemon. Reads the Claude
Code OAuth token, polls Anthropic for rate-limit headers, and POSTs the
small usage payload to the Clawdmeter ESP32 over HTTP.

## Quick start

```powershell
cd daemon
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt

# Test against Anthropic without an ESP32 in the loop:
python clawdmeter_daemon.py --dry-run --once -v

# Real run (creates config.json from the template first):
copy config.example.json config.json
notepad config.json   # set esp32_ip to your board's address
python clawdmeter_daemon.py
```

## Configuration

`config.json` (next to the script) is the primary source. CLI flags override
the file. Environment variable `CLAWDMETER_ESP32_IP` overrides nothing
unless neither flag nor file provides it.

| Key                 | Default                                | Notes |
| ------------------- | -------------------------------------- | ----- |
| `esp32_ip`          | (required)                             | Board IP. The board prints it to the Network screen on connect. |
| `poll_interval_s`   | 60                                     | Seconds between Anthropic polls. |
| `request_timeout_s` | 15                                     | HTTP timeout for both Anthropic and ESP32 calls. |
| `credentials_path`  | `~/.claude/.credentials.json` (auto)   | Override if your token lives elsewhere. |

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

- **`accessToken not found`** — run `claude /login` once, or check
  `%USERPROFILE%\.claude\.credentials.json` exists and contains a valid
  token.
- **`no anthropic-ratelimit-unified-* headers`** — Anthropic may have
  renamed or dropped these headers since this code was written. Run with
  `-v` and inspect the logged header list; update `poll_anthropic` to
  match.
- **`esp32 POST failed`** — confirm the board is on the same LAN
  (`curl http://<ip>/status`) and Windows Firewall isn't blocking
  outbound HTTP to the LAN. The board is the server, so the daemon's
  outbound connection should just work.

## Windows service install

See task 10 in the port plan — `nssm` wraps `python clawdmeter_daemon.py`
into a Windows service that survives logout and restarts on crash. Not
yet scripted; instructions will land in `install_windows.md`.
