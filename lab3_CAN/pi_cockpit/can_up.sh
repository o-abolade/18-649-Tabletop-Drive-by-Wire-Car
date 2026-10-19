#!/usr/bin/env bash
# Bring up can0 (bus A) and can1 (bus B) at 500 kbit/s and print their state.
# Requires these lines in /boot/firmware/config.txt (then a reboot):
#   dtparam=spi=on
#   dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25
#   dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=24
set -euo pipefail

BITRATE=500000
CONFIG=/boot/firmware/config.txt

for line in "dtparam=spi=on" \
            "dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25" \
            "dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=24"; do
    if ! grep -qxF "$line" "$CONFIG"; then
        echo "MISSING in $CONFIG: $line  (add it and reboot)" >&2
    fi
done

for ifc in can0 can1; do
    if ! ip link show "$ifc" >/dev/null 2>&1; then
        echo "$ifc: interface does not exist -> check overlay / wiring, then: dmesg | grep -i mcp251" >&2
        continue
    fi
    sudo ip link set "$ifc" down
    # restart-ms: automatic bus-off recovery (R4.8 wants recovery within 100 ms)
    sudo ip link set "$ifc" type can bitrate "$BITRATE" restart-ms 100
    sudo ip link set "$ifc" up
    echo "== $ifc =="
    ip -details link show "$ifc" | grep -E "state|bitrate|clock"
done
