#!/usr/bin/env bash
# One-time setup on the Pi (asks for the sudo password once):
#  - installs and enables lab5-cockpit.service: brings up both CAN buses and
#    starts the cockpit at boot (checkoff steps 6-7 power-cycle the Pi), run
#    from this folder, log in ~/lab5-cockpit.log, flags in ~/lab5-cockpit.env;
#  - lets team1 start/stop/restart only this service without a password, so
#    the cockpit can be redeployed over SSH.
# Undo: sudo systemctl disable --now lab5-cockpit;
#       sudo rm /etc/systemd/system/lab5-cockpit.service /etc/sudoers.d/lab5-cockpit
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

if [[ "$(pwd)" != /home/team1/18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/pi_cockpit ]]; then
    echo "Run from ~/18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/pi_cockpit (the unit file uses that path)." >&2
    exit 1
fi
make cockpit
[[ -f ~/lab5-cockpit.env ]] || echo "COCKPIT_ARGS=--rt --leader=cockpit" > ~/lab5-cockpit.env

sudo install -m 644 lab5-cockpit.service /etc/systemd/system/lab5-cockpit.service
rule=$(mktemp)
cat > "$rule" <<'EOF'
team1 ALL=(root) NOPASSWD: /usr/bin/systemctl start lab5-cockpit, /usr/bin/systemctl stop lab5-cockpit, /usr/bin/systemctl restart lab5-cockpit
EOF
sudo visudo -cf "$rule"
sudo install -m 440 "$rule" /etc/sudoers.d/lab5-cockpit
rm -f "$rule"
# The Lab 3 service must not hold the CAN buses at the same time
sudo systemctl disable --now lab3-cockpit.service 2>/dev/null || true
sudo systemctl daemon-reload
sudo systemctl enable lab5-cockpit.service
sudo systemctl restart lab5-cockpit.service
sleep 3
tail -n 20 ~/lab5-cockpit.log
