#!/usr/bin/env bash
# Wait for both MCP2515 devices before opening the cockpit sockets at boot.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

for ((attempt = 0; attempt < 15; attempt++)); do
    if ip link show can0 >/dev/null 2>&1 && ip link show can1 >/dev/null 2>&1; then
        exec /bin/bash ./can_up.sh
    fi
    sleep 1
done

echo "Both CAN interfaces are required at startup; check MCP2515 overlays and wiring." >&2
exit 1
