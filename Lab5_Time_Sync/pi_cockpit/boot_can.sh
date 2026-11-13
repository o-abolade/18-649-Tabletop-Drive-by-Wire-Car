#!/usr/bin/env bash
# Wait for both MCP2515 devices and bring them up (can_up.sh) before the
# cockpit opens its sockets. Runs as root from lab5-cockpit.service
# (ExecStartPre).
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# The CAN driver's threads stay at the kernel defaults: interrupt threads
# FIFO 50, the spi0 pump an ordinary task. Raising them (FIFO 60/61, to steady
# the kernel timestamps) coincided with "mcp251x: hard_xmit called while tx
# busy" bursts and frames with a Raft ID and another frame's data reaching
# the cockpit (2026-11-13). Priorities survive a service restart, so set the
# defaults explicitly rather than just not raising them.
restore_can_threads() {
    local pid
    for pid in $(pgrep '^spi0$' || true); do
        chrt -o -p 0 "$pid" || true
    done
    for pid in $(pgrep '^irq/[0-9]+-spi0\.[01]$' || true); do
        chrt -f -p 50 "$pid" || true
    done
}

for ((attempt = 0; attempt < 15; attempt++)); do
    if ip link show can0 >/dev/null 2>&1 && ip link show can1 >/dev/null 2>&1; then
        /bin/bash ./can_up.sh
        restore_can_threads
        exit 0
    fi
    sleep 1
done

echo "Both CAN interfaces are required at startup; check MCP2515 overlays and wiring." >&2
exit 1
