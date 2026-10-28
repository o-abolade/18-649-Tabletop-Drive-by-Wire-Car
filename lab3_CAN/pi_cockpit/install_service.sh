#!/usr/bin/env bash
# Run on the Raspberry Pi after configuring the MCP2515 boot overlays.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

if [[ $(uname -s) != Linux ]] || ! command -v systemctl >/dev/null 2>&1; then
    echo "Run this installer on the Raspberry Pi running systemd." >&2
    exit 1
fi

make cockpit
sudo install -d /opt/lab3-cockpit
sudo install -m 755 cockpit can_up.sh boot_can.sh /opt/lab3-cockpit/
sudo install -m 644 lab3-cockpit.service /etc/systemd/system/lab3-cockpit.service
sudo systemctl daemon-reload
sudo systemctl enable lab3-cockpit.service
sudo systemctl restart lab3-cockpit.service
sudo systemctl --no-pager status lab3-cockpit.service
