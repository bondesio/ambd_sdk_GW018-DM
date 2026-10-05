#!/usr/bin/env python3
"""Generate the ignored local OTA host configuration for the GW018 build."""

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "component/common/example/ota_http/gw018_ota_config.h"
TEMPLATE = ROOT / "component/common/example/ota_http/gw018_ota_config.example.h"


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} HOSTNAME_OR_IPV4", file=sys.stderr)
        return 2
    host = sys.argv[1]
    if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?", host):
        print("Host must be a DNS name or IPv4 literal, without scheme, port, or path.", file=sys.stderr)
        return 2
    if CONFIG.exists() and "GW018_OTA_CONFIG_GENERATED" not in CONFIG.read_text():
        print(f"Refusing to overwrite custom config: {CONFIG}", file=sys.stderr)
        return 1
    template = TEMPLATE.read_text()
    if template.count('"ota.example.invalid"') != 1:
        print("OTA config template is unexpected; no file was changed.", file=sys.stderr)
        return 1
    CONFIG.write_text(template.replace('"ota.example.invalid"', f'"{host}"'))
    print(f"Configured OTA host {host}. Serve OTA_All.bin on TCP port 8080.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
