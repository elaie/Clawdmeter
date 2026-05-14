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

from discovery import (
    discover_first,
    discover_by_name,
    resolve_hostname,
    load_cached_ip,
    save_cached_ip,
    clear_cached_ip,
)

DEFAULT_POLL_INTERVAL_S = 60
DEFAULT_REQUEST_TIMEOUT_S = 15
DEFAULT_DISCOVERY_TIMEOUT_S = 6
DEFAULT_ACTIVE_SESSION_WINDOW_S = 300  # JSONL mtime within last 5 min ⇒ "active"
HAIKU_MODEL = "claude-haiku-4-5-20251001"
ANTHROPIC_URL = "https://api.anthropic.com/v1/messages"

log = logging.getLogger("clawdmeter")


def default_credentials_path() -> Path:
    return Path.home() / ".claude" / ".credentials.json"


def default_projects_dir() -> Path:
    return Path.home() / ".claude" / "projects"


def count_active_sessions(projects_dir: Path, window_s: int) -> int:
    """Count Claude Code sessions touched within the last `window_s` seconds.

    Each conversation is a JSONL appended to as the session progresses, so a
    recent mtime means the user (or Claude Code) wrote to it recently. -1 if
    the projects dir doesn't exist yet (fresh install / never used)."""
    if not projects_dir.exists():
        return -1
    cutoff = time.time() - window_s
    n = 0
    try:
        for jsonl in projects_dir.glob("*/*.jsonl"):
            try:
                if jsonl.stat().st_mtime >= cutoff:
                    n += 1
            except OSError:
                continue
    except OSError as e:
        log.warning("active session scan failed: %s", e)
        return -1
    return n


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


def poll_anthropic(token: str, request_timeout_s: int,
                   projects_dir: Path, active_window_s: int) -> dict | None:
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
        "as": count_active_sessions(projects_dir, active_window_s),
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


def discover_esp32(timeout_s: float = DEFAULT_DISCOVERY_TIMEOUT_S,
                   device_name: str | None = None) -> str | None:
    """Find the ESP32 via mDNS. If `device_name` is given, only return a
    match for that specific device (so multi-clawdmeter LANs are unambiguous).

    Falls back to `<name>.local` (or `clawdmeter.local`) via the OS resolver
    if service-type browse turns up nothing."""
    if device_name:
        log.info("discovering clawdmeter '%s' on the LAN (mDNS, up to %.0fs)...",
                 device_name, timeout_s)
        dev = discover_by_name(device_name, timeout_s)
    else:
        log.info("discovering any clawdmeter on the LAN (mDNS, up to %.0fs)...", timeout_s)
        dev = discover_first(timeout_s)
    if dev:
        log.info("found '%s' at %s:%d (%s)",
                 dev.name, dev.ip, dev.port, dev.hostname or "no hostname")
        return dev.ip
    fallback_host = f"{device_name}.local" if device_name else "clawdmeter.local"
    ip = resolve_hostname(fallback_host)
    if ip:
        log.info("resolved %s -> %s", fallback_host, ip)
        return ip
    if device_name:
        log.warning("no clawdmeter named '%s' answered", device_name)
    else:
        log.warning("no clawdmeter responded to mDNS")
    return None


def resolve_esp32_ip(args_ip: str | None, cfg: dict, cache_dir: Path,
                     allow_discovery: bool, discovery_timeout_s: float,
                     device_name: str | None) -> str | None:
    """Order: explicit IP > cache (only when no name override) > mDNS discovery.

    `device_name` filters discovery to a specific board. When `device_name` is
    set, the cache is skipped — we always verify by name so we never POST to
    the wrong board after a board swap."""
    # 1) Explicit overrides win — user is being intentional.
    explicit = args_ip or cfg.get("esp32_ip") or os.environ.get("CLAWDMETER_ESP32_IP")
    if explicit:
        return explicit
    if not allow_discovery:
        return None
    # 2) Cached address from last successful discovery — but only when no
    #    name filter is in play. With a name filter, a stale cache could point
    #    at a different device that took the same IP.
    if not device_name:
        cached = load_cached_ip(cache_dir)
        if cached:
            log.info("using cached IP %s (from %s)", cached, cache_dir / "discovered.json")
            return cached
    # 3) Fresh scan.
    ip = discover_esp32(discovery_timeout_s, device_name)
    if ip and not device_name:
        save_cached_ip(cache_dir, ip)
    return ip


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
    p.add_argument("--device", help="match a specific device by name (the CLAWDMETER_NAME baked into the firmware)")
    p.add_argument("--poll-interval", type=int, help="override poll_interval_s from config")
    p.add_argument("--credentials-path", type=Path, help="override Claude credentials path")
    p.add_argument("--dry-run", action="store_true", help="skip POST to ESP32; just log the payload")
    p.add_argument("--once", action="store_true", help="poll once and exit (useful with --dry-run)")
    p.add_argument("--no-discovery", action="store_true",
                   help="disable mDNS auto-discovery (require explicit --esp32-ip / config)")
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
    cache_dir = args.config.parent
    poll_interval_s = args.poll_interval or cfg.get("poll_interval_s") or DEFAULT_POLL_INTERVAL_S
    creds_path = args.credentials_path or (Path(cfg["credentials_path"]) if cfg.get("credentials_path") else default_credentials_path())
    request_timeout_s = cfg.get("request_timeout_s", DEFAULT_REQUEST_TIMEOUT_S)
    discovery_timeout_s = cfg.get("discovery_timeout_s", DEFAULT_DISCOVERY_TIMEOUT_S)
    allow_discovery = not args.no_discovery and cfg.get("discovery_enabled", True)
    device_name = args.device or cfg.get("esp32_name") or os.environ.get("CLAWDMETER_DEVICE")
    projects_dir = Path(cfg["projects_dir"]) if cfg.get("projects_dir") else default_projects_dir()
    active_window_s = cfg.get("active_session_window_s", DEFAULT_ACTIVE_SESSION_WINDOW_S)

    esp32_ip = None
    if not args.dry_run:
        esp32_ip = resolve_esp32_ip(args.esp32_ip, cfg, cache_dir,
                                    allow_discovery, discovery_timeout_s,
                                    device_name)
        if not esp32_ip:
            hint = (f"named '{device_name}' " if device_name else "")
            log.error("could not locate the ESP32 %s— set --esp32-ip / config.json esp32_ip, "
                      "or run `python discover.py` to see what's on the LAN. "
                      "Pass --dry-run to skip the POST.", hint)
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

    # Re-discover after this many back-to-back POST failures. DHCP renewal /
    # board swap is the usual cause, so a 30+ second blip earns a fresh scan.
    REDISCOVER_AFTER_FAILS = 3
    consecutive_post_fails = 0
    esp_explicit = bool(args.esp32_ip or cfg.get("esp32_ip") or os.environ.get("CLAWDMETER_ESP32_IP"))

    backoff_s = 5
    while not _stop:
        payload = poll_anthropic(token, request_timeout_s, projects_dir, active_window_s)
        if payload is None:
            log.info("retry in %ds", backoff_s)
            _sleep_interruptible(backoff_s)
            backoff_s = min(backoff_s * 2, 60)
            continue
        backoff_s = 5

        log.info("payload: %s", json.dumps(payload))
        if not args.dry_run:
            ok = post_to_esp32(esp32_ip, payload, request_timeout_s)
            if ok:
                consecutive_post_fails = 0
            else:
                consecutive_post_fails += 1
                # If the user pinned an IP explicitly, respect it — don't
                # silently flip to a discovered one. Only auto-rediscover
                # when we picked the IP ourselves (cache or fresh scan).
                if (allow_discovery and not esp_explicit
                        and consecutive_post_fails >= REDISCOVER_AFTER_FAILS):
                    log.info("%d POSTs failed in a row — invalidating cache and rescanning",
                             consecutive_post_fails)
                    clear_cached_ip(cache_dir)
                    new_ip = discover_esp32(discovery_timeout_s, device_name)
                    if new_ip:
                        esp32_ip = new_ip
                        # Only cache when we're not name-pinned — the cache is
                        # only consulted in the no-name path anyway.
                        if not device_name:
                            save_cached_ip(cache_dir, new_ip)
                        consecutive_post_fails = 0

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
