#!/usr/bin/env bash
# Extra credit: connect the cockpit to the cloud node (run on the Pi as team1).
#
#   ./install_uplink.sh key                    # 1. make the tunnel key, print it
#   ./install_uplink.sh <VM IP> <redis pass>   # 2. after install_vm.sh on the VM
#
# Step 1 prints the public key to pass to install_vm.sh. Step 2 writes
# ~/lab5-uplink.env (the VM address, for the tunnel) and adds UPLINK_HOST /
# UPLINK_PORT / UPLINK_PASSWORD to ~/lab5-cockpit.env (mode 600), installs and
# starts lab5-uplink-tunnel.service, and restarts the cockpit. Asks for the
# sudo password once.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
KEY=~/.ssh/lab5_uplink

if [[ "${1:-}" == key ]]; then
    [[ -f $KEY ]] || ssh-keygen -t ed25519 -N "" -C lab5-uplink -f "$KEY" >/dev/null
    echo "Tunnel public key (give this to install_vm.sh on the VM):"
    cat "$KEY.pub"
    exit 0
fi

VM="${1:-}"
REDIS_PASS="${2:-}"
if [[ -z "$VM" || -z "$REDIS_PASS" ]]; then
    echo "usage: $0 key | $0 <VM public IP> <Redis password>" >&2
    exit 1
fi
[[ -f $KEY ]] || { echo "Run '$0 key' first." >&2; exit 1; }

umask 077
echo "UPLINK_VM=$VM" > ~/lab5-uplink.env

ENVF=~/lab5-cockpit.env
touch "$ENVF"
grep -v '^UPLINK_' "$ENVF" > "$ENVF.new" || true
cat >> "$ENVF.new" <<EOF
UPLINK_HOST=127.0.0.1
UPLINK_PORT=6380
UPLINK_PASSWORD=$REDIS_PASS
EOF
mv "$ENVF.new" "$ENVF"
chmod 600 "$ENVF" ~/lab5-uplink.env

make cockpit
sudo install -m 644 lab5-uplink-tunnel.service /etc/systemd/system/lab5-uplink-tunnel.service
sudo systemctl daemon-reload
sudo systemctl enable --now lab5-uplink-tunnel.service
sudo systemctl restart lab5-cockpit.service
sleep 4
systemctl --no-pager --lines=3 status lab5-uplink-tunnel.service || true
grep -a "uplink" ~/lab5-cockpit.log | tail -3
