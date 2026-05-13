#!/usr/bin/env python3
"""Clawdmeter usage daemon (WiFi+HTTP variant).

Reads the Claude Code OAuth token, polls Anthropic for rate-limit headers,
and POSTs a small JSON payload to the Clawdmeter ESP32 over HTTP.

This is the cross-platform Python port of the upstream bash daemon. It runs
on Windows native (no WSL/BLE required) and Linux/macOS.

Usage:
    python clawdmeter_daemon.py --config config.json
    python clawdmeter_daemon.py --esp32-ip 192.168.1.42
    python clawdmeter_daemon.py --dry-run         # poll Anthropic, log payload, skip POST
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import signal
import sys
import time
from pathlib import Path
from typing import Any

import requests

DEFAULT_POLL_INTERVAL_S = 60
DEFAULT_REQUEST_TIMEOUT_S = 15
HAIKU_MODEL = "claude-haiku-4-5-20251001"
ANTHROPIC_URL = "https://api.anthropic.com/v1/messages"

log = logging.getLogger("clawdmeter")


def default_credentials_path() -> Path:
    return Path.home() / ".claude" / ".credentials.json"


def load_credentials(path: Path) -> str:
    if not path.exists():
        raise FileNotFoundError(f"credentials file not found: {path}")
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    # Upstream's bash daemon looked for "accessToken" anywhere in the file.
    # Walk the JSON tree defensively in case the schema is nested.
    token = _find_key(data, "accessToken")
    if not token:
        raise RuntimeError(
            f"no accessToken in {path}. Run 'claude /login' or inspect the file manually."
        )
    return token


def _find_key(node: Any, key: str) -> Any:
    if isinstance(node, dict):
        if key in node and isinstance(node[key], str):
            return node[key]
        for v in node.values():
            found = _find_key(v, key)
            if found:
                return found
    elif isinstance(node, list):
        for item in node:
            found = _find_key(item, key)
            if found:
                return found
    return None


def poll_anthropic(token: str, request_timeout_s: int) -> dict | None:
    """Probe Anthropic with a 1-token Haiku call. Returns the payload dict to
    POST to the ESP32, or None on transport failure."""
    headers = {
        "Authorization": f"Bearer {token}",
        "anthropic-version": "2023-06-01",
        "anthropic-beta": "oauth-2025-04-20",
        "Content-Type": "application/json",
        "User-Agent": "claude-code/2.1.5",
    }
    body = {
        "model": HAIKU_MODEL,
        "max_tokens": 1,
        "messages": [{"role": "user", "content": "hi"}],
    }
    try:
        resp = requests.post(ANTHROPIC_URL, headers=headers, json=body, timeout=request_timeout_s)
    except requests.RequestException as e:
        log.warning("anthropic request failed: %s", e)
        return None

    if resp.status_code >= 500:
        log.warning("anthropic %d: %s", resp.status_code, resp.text[:200])
        return None
    if resp.status_code == 401:
        log.error("anthropic 401 — token is invalid or expired. Run 'claude /login'.")
        return None
    # 4xx other than 401 is still informative — the rate-limit headers usually
    # arrive even on 429. Fall through and try to parse.

    h = {k.lower(): v for k, v in resp.headers.items()}

    def _f(name: str, default: float = 0.0) -> float:
        try:
            return float(h.get(name, default))
        except (TypeError, ValueError):
            return default

    s5h_util = _f("anthropic-ratelimit-unified-5h-utilization")
    s5h_reset = _f("anthropic-ratelimit-unified-5h-reset")
    s7d_util = _f("anthropic-ratelimit-unified-7d-utilization")
    s7d_reset = _f("anthropic-ratelimit-unified-7d-reset")
    status = h.get("anthropic-ratelimit-unified-5h-status", "unknown")

    if not any(k.startswith("anthropic-ratelimit-unified-") for k in h):
        log.warning(
            "no anthropic-ratelimit-unified-* headers in response — header names "
            "may have changed. Raw response headers: %s",
            list(h.keys()),
        )

    now = time.time()
    sr_min = max(0, int(round((s5h_reset - now) / 60.0))) if s5h_reset else 0
    wr_min = max(0, int(round((s7d_reset - now) / 60.0))) if s7d_reset else 0

    return {
        "s": round(s5h_util * 100, 1),
        "sr": sr_min,
        "w": round(s7d_util * 100, 1),
        "wr": wr_min,
        "st": status,
        "ok": True,
    }


def post_to_esp32(esp32_ip: str, payload: dict, request_timeout_s: int) -> bool:
    url = f"http://{esp32_ip}/usage"
    try:
        resp = requests.post(url, json=payload, timeout=request_timeout_s)
    except requests.RequestException as e:
        log.warning("esp32 POST failed: %s", e)
        return False
    if resp.status_code >= 300:
        log.warning("esp32 %d: %s", resp.status_code, resp.text[:200])
        return False
    return True


def load_config(path: Path | None) -> dict:
    if not path or not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", type=Path, default=Path(__file__).with_name("config.json"),
                   help="path to config.json (default: alongside this script)")
    p.add_argument("--esp32-ip", help="override esp32_ip from config")
    p.add_argument("--poll-interval", type=int, help="override poll_interval_s from config")
    p.add_argument("--credentials-path", type=Path, help="override Claude credentials path")
    p.add_argument("--dry-run", action="store_true", help="skip POST to ESP32; just log the payload")
    p.add_argument("--once", action="store_true", help="poll once and exit (useful with --dry-run)")
    p.add_argument("--verbose", "-v", action="store_true")
    return p.parse_args()


_stop = False


def _on_signal(signum, frame):  # noqa: ARG001
    global _stop
    _stop = True
    log.info("signal %d received; shutting down at end of next tick", signum)


def main() -> int:
    args = parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        datefmt="%H:%M:%S",
    )

    cfg = load_config(args.config)
    esp32_ip = args.esp32_ip or cfg.get("esp32_ip") or os.environ.get("CLAWDMETER_ESP32_IP")
    poll_interval_s = args.poll_interval or cfg.get("poll_interval_s") or DEFAULT_POLL_INTERVAL_S
    creds_path = args.credentials_path or (Path(cfg["credentials_path"]) if "credentials_path" in cfg else default_credentials_path())
    request_timeout_s = cfg.get("request_timeout_s", DEFAULT_REQUEST_TIMEOUT_S)

    if not args.dry_run and not esp32_ip:
        log.error("esp32_ip is required (set via --esp32-ip, config.json, or CLAWDMETER_ESP32_IP). Pass --dry-run to skip the POST.")
        return 2

    try:
        token = load_credentials(creds_path)
    except (FileNotFoundError, RuntimeError) as e:
        log.error("%s", e)
        return 2
    log.info("loaded token from %s (len=%d)", creds_path, len(token))
    if args.dry_run:
        log.info("DRY RUN — payloads will be logged, no POST will be sent")
    else:
        log.info("posting to http://%s/usage every %ds", esp32_ip, poll_interval_s)

    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)

    backoff_s = 5
    while not _stop:
        payload = poll_anthropic(token, request_timeout_s)
        if payload is None:
            log.info("retry in %ds", backoff_s)
            _sleep_interruptible(backoff_s)
            backoff_s = min(backoff_s * 2, 60)
            continue
        backoff_s = 5

        log.info("payload: %s", json.dumps(payload))
        if not args.dry_run:
            ok = post_to_esp32(esp32_ip, payload, request_timeout_s)
            if not ok:
                # Don't reset poll interval on POST failure — the next poll
                # will pick up fresh data anyway. Just log and continue.
                pass

        if args.once:
            return 0
        _sleep_interruptible(poll_interval_s)

    return 0


def _sleep_interruptible(seconds: int) -> None:
    """Sleep in 1s slices so SIGINT/SIGTERM are responsive."""
    end = time.monotonic() + seconds
    while not _stop and time.monotonic() < end:
        time.sleep(min(1.0, end - time.monotonic()))


if __name__ == "__main__":
    sys.exit(main())
