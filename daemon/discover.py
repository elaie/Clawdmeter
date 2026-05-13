#!/usr/bin/env python3
"""Scan the local network for Clawdmeter devices.

Usage:
    python discover.py                       # 4s mDNS browse, list every device
    python discover.py --timeout 10          # longer scan
    python discover.py --probe               # also GET /status from each hit
    python discover.py --device living-room  # filter to a specific name
    python discover.py --first               # print just the first IP, exit 1 if none
"""
from __future__ import annotations

import argparse
import sys

import requests

from discovery import (
    DEFAULT_TIMEOUT_S,
    discover,
    discover_by_name,
    resolve_hostname,
)


def probe(ip: str, port: int = 80, timeout_s: float = 2.0) -> str:
    try:
        r = requests.get(f"http://{ip}:{port}/status", timeout=timeout_s)
        r.raise_for_status()
        return r.text.strip()
    except requests.RequestException as e:
        return f"<probe failed: {e}>"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S, help="seconds to browse for (default: 4)")
    p.add_argument("--probe", action="store_true", help="GET /status on each hit and print response")
    p.add_argument("--first", action="store_true", help="print only the first IP; exit 1 if none found")
    p.add_argument("--device", help="filter to a specific device name (CLAWDMETER_NAME from firmware)")
    args = p.parse_args()

    if args.device:
        print(f"scanning for clawdmeter '{args.device}' (mDNS, up to {args.timeout}s)...", file=sys.stderr)
        dev = discover_by_name(args.device, args.timeout)
        devices = [dev] if dev else []
    else:
        print(f"scanning for clawdmeter devices (mDNS, {args.timeout}s)...", file=sys.stderr)
        devices = discover(args.timeout)

    if not devices:
        # Fallback: try resolving <name>.local directly. Some networks block
        # service-type browsing but still pass through standard A-record mDNS.
        fallback_host = f"{args.device}.local" if args.device else "clawdmeter.local"
        print(f"no service-type responders; trying {fallback_host} A record...", file=sys.stderr)
        ip = resolve_hostname(fallback_host)
        if ip:
            if args.first:
                print(ip)
                return 0
            print(f"{fallback_host} -> {ip}")
            if args.probe:
                print(f"  /status: {probe(ip)}")
            return 0
        print("no devices found.", file=sys.stderr)
        return 1

    if args.first:
        print(devices[0].ip)
        return 0

    # Pretty-print: name first (that's the actionable handle for --device),
    # then IP, then hostname for confirmation.
    name_w = max((len(d.name) for d in devices), default=4)
    for d in devices:
        print(f"  {d.name:<{name_w}}  {d.ip}:{d.port}  {d.hostname}".rstrip())
        if args.probe:
            print(f"  {' ' * name_w}  /status: {probe(d.ip, d.port)}")
    if len(devices) > 1 and not args.device:
        print(file=sys.stderr)
        print(f"found {len(devices)} devices. Run the daemon with --device <name> "
              f"to pin to one, or run `python discover.py --device <name> --probe` "
              f"to verify.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
