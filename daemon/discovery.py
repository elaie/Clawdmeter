"""Zeroconf/mDNS discovery for the Clawdmeter ESP32.

The firmware advertises itself as `_clawdmeter._tcp` on port 80 (see
firmware/src/net.cpp). This module browses the local subnet for that
service-type and returns the first responder.

Used by both `discover.py` (one-shot CLI) and `clawdmeter_daemon.py`
(auto-discover when no IP is configured, or when a cached IP stops
responding).
"""

from __future__ import annotations

import json
import logging
import socket
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

from zeroconf import ServiceBrowser, ServiceListener, Zeroconf

# Keep in sync with NET_MDNS_SERVICE / NET_MDNS_PROTO in firmware/src/net.cpp.
SERVICE_TYPE = "_clawdmeter._tcp.local."
DEFAULT_TIMEOUT_S = 4.0

log = logging.getLogger("clawdmeter.discovery")


@dataclass
class Device:
    name: str         # Friendly device name (matches CLAWDMETER_NAME in firmware)
    ip: str
    port: int
    hostname: str     # e.g. "living-room.local."
    raw_service: str  # Full zeroconf service name, for debugging

    @property
    def base_url(self) -> str:
        return f"http://{self.ip}:{self.port}"


def _friendly_name(raw_service: str, txt_properties: dict) -> str:
    """Extract the device name. Prefer the TXT `name=` record (the firmware
    sets this explicitly); fall back to the service-instance prefix
    (`<name>._clawdmeter._tcp.local.`)."""
    raw = txt_properties.get(b"name") if txt_properties else None
    if isinstance(raw, bytes):
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            pass
    # Service name shape: "<instance>._clawdmeter._tcp.local."
    return raw_service.split("._clawdmeter._tcp")[0].rstrip(".")


class _Collector(ServiceListener):
    def __init__(self) -> None:
        self.devices: List[Device] = []

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        info = zc.get_service_info(type_, name, timeout=2000)
        if not info:
            return
        for addr in info.parsed_addresses():
            # Skip IPv6 link-local clutter; the firmware speaks IPv4.
            if ":" in addr:
                continue
            friendly = _friendly_name(name, info.properties or {})
            self.devices.append(Device(
                name=friendly,
                ip=addr,
                port=info.port or 80,
                hostname=info.server or "",
                raw_service=name,
            ))
            return

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        pass

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        pass


def discover(timeout_s: float = DEFAULT_TIMEOUT_S,
             match_name: Optional[str] = None) -> List[Device]:
    """Browse the local network for clawdmeter devices.

    - `match_name=None` — wait the full window (or until first responder +
      a 0.5s grace) and return everyone who answered.
    - `match_name="foo"` — return as soon as a device named `foo` shows up.
      Returns an empty list if it never appears within the timeout.
    """
    zc = Zeroconf()
    collector = _Collector()
    browser = ServiceBrowser(zc, SERVICE_TYPE, collector)
    try:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if match_name is not None:
                for d in collector.devices:
                    if d.name == match_name:
                        return [d]
            elif collector.devices:
                # Found at least one — short grace period in case more are
                # announcing, then return everything.
                time.sleep(0.5)
                break
            time.sleep(0.1)
    finally:
        browser.cancel()
        zc.close()
    if match_name is not None:
        return [d for d in collector.devices if d.name == match_name]
    return collector.devices


def discover_first(timeout_s: float = DEFAULT_TIMEOUT_S) -> Optional[Device]:
    devices = discover(timeout_s)
    if not devices:
        return None
    return devices[0]


def discover_by_name(name: str, timeout_s: float = DEFAULT_TIMEOUT_S) -> Optional[Device]:
    devices = discover(timeout_s, match_name=name)
    return devices[0] if devices else None


def resolve_hostname(hostname: str = "clawdmeter.local", timeout_s: float = 2.0) -> Optional[str]:
    """Resolve `clawdmeter.local` via the OS resolver as a fallback when
    zeroconf service browsing is blocked (some corporate networks).

    Works on Win10 1803+ (built-in), macOS (Bonjour), and Linux with
    nss-mdns/avahi installed. Returns None if resolution fails or times out.
    """
    socket.setdefaulttimeout(timeout_s)
    try:
        return socket.gethostbyname(hostname)
    except (socket.gaierror, socket.timeout, OSError):
        return None
    finally:
        socket.setdefaulttimeout(None)


# ---- IP cache (separate from human-edited config.json) ----

def cache_path(base_dir: Path) -> Path:
    return base_dir / "discovered.json"


def load_cached_ip(base_dir: Path) -> Optional[str]:
    p = cache_path(base_dir)
    if not p.exists():
        return None
    try:
        with p.open("r", encoding="utf-8") as f:
            data = json.load(f)
        ip = data.get("ip")
        return ip if isinstance(ip, str) and ip else None
    except (OSError, json.JSONDecodeError):
        return None


def save_cached_ip(base_dir: Path, ip: str) -> None:
    p = cache_path(base_dir)
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        with p.open("w", encoding="utf-8") as f:
            json.dump({"ip": ip, "saved_at": int(time.time())}, f)
    except OSError as e:
        log.warning("could not write cache %s: %s", p, e)


def clear_cached_ip(base_dir: Path) -> None:
    p = cache_path(base_dir)
    try:
        p.unlink(missing_ok=True)
    except OSError:
        pass
