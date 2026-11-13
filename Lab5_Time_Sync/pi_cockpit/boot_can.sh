#!/usr/bin/env bash
# Wait for both MCP2515 devices, bring them up (can_up.sh), and give the CAN
# driver's threads real-time priority, before the cockpit opens its sockets.
# Runs as root from lab5-cockpit.service (ExecStartPre).
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# Lab 5 time sync uses the kernel's receive and TX-echo timestamps, taken in
# the mcp251x interrupt threads (irq/NN-spi0.0, irq/NN-spi0.1), which do
# their SPI transfers through the spi0 message-pump thread. By default the
# pump runs as an ordinary task, so other work delayed the stamps by up to
# a few hundred us (seen as outliers on the STM32 followers). FIFO 61 for
# the pump, 60 for the interrupt threads: below nothing else that matters,
# above the default 50 of other IRQ threads.
raise_can_threads() {
    local pid
    for pid in $(pgrep '^spi0$' || true); do
        chrt -f -p 61 "$pid" || true
    done
    for pid in $(pgrep '^irq/[0-9]+-spi0\.[01]$' || true); do
        chrt -f -p 60 "$pid" || true
    done
}

for ((attempt = 0; attempt < 15; attempt++)); do
    if ip link show can0 >/dev/null 2>&1 && ip link show can1 >/dev/null 2>&1; then
        /bin/bash ./can_up.sh
        raise_can_threads
        exit 0
    fi
    sleep 1
done

echo "Both CAN interfaces are required at startup; check MCP2515 overlays and wiring." >&2
exit 1
