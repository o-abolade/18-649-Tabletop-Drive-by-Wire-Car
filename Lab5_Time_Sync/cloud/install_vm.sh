#!/usr/bin/env bash
# Set up the Lab 5 cloud node on a fresh Ubuntu VM (22.04 or 24.04).
#
#   git clone https://github.com/o-abolade/18-649-Tabletop-Drive-by-Wire-Car.git
#   cd 18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/cloud
#   ./install_vm.sh "ssh-ed25519 AAAA... lab5-uplink"     # the Pi's tunnel key
#
# It installs Docker, writes .env with fresh random secrets (mode 600),
# creates the user `vtunnel` whose only right is to forward to Redis on the
# VM's 127.0.0.1:6379, opens the firewall for 22/80/443 only, and starts
# Redis + web app + Caddy. At the end it prints the web login and the values
# for the Pi (install_uplink.sh). Re-running keeps the existing secrets.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

PI_KEY="${1:-}"
if [[ -z "$PI_KEY" || "$PI_KEY" != ssh-* ]]; then
    echo "usage: $0 \"<the Pi's tunnel public key: ssh-ed25519 AAAA...>\"" >&2
    exit 1
fi

# ---- Docker ----
if ! command -v docker >/dev/null || ! docker compose version >/dev/null 2>&1; then
    sudo apt-get update -y
    sudo apt-get install -y docker.io docker-compose-v2 ||
        sudo apt-get install -y docker.io docker-compose-plugin
    sudo systemctl enable --now docker
fi

# ---- secrets (.env) ----
PUBLIC_IP=$(curl -fsS https://api.ipify.org || curl -fsS https://ifconfig.me)
SITE="$(echo "$PUBLIC_IP" | tr . -).sslip.io"
if [[ ! -f .env ]]; then
    umask 077
    cat > .env <<EOF
SITE=$SITE
REDIS_PASSWORD=$(openssl rand -hex 24)
SECRET_KEY=$(openssl rand -hex 32)
GUI_USER=team
GUI_PASSWORD=$(openssl rand -base64 12 | tr -d '/+=')
EOF
fi
chmod 600 .env
# shellcheck disable=SC1091
source .env

# ---- tunnel user: port forwarding to Redis only ----
if ! id vtunnel >/dev/null 2>&1; then
    sudo useradd --create-home --shell /usr/sbin/nologin vtunnel
fi
sudo install -d -m 700 -o vtunnel -g vtunnel /home/vtunnel/.ssh
echo "restrict,port-forwarding,permitopen=\"127.0.0.1:6379\",command=\"/bin/false\" $PI_KEY" |
    sudo tee /home/vtunnel/.ssh/authorized_keys >/dev/null
sudo chown vtunnel:vtunnel /home/vtunnel/.ssh/authorized_keys
sudo chmod 600 /home/vtunnel/.ssh/authorized_keys

# ---- firewall: SSH and HTTPS (80 is needed for the certificate) ----
if command -v ufw >/dev/null; then
    sudo ufw allow OpenSSH
    sudo ufw allow 80/tcp
    sudo ufw allow 443/tcp
    sudo ufw --force enable
fi

# ---- start ----
sudo docker compose up -d --build
sleep 5
sudo docker compose ps

cat <<EOF

=== Cloud node is up ===
Monitor:   https://$SITE   (the certificate can take a minute on first start)
Login:     user $GUI_USER   password $GUI_PASSWORD
Make sure the cloud firewall / network security group also allows TCP 80 and 443.

For the Pi (run there: ./install_uplink.sh):
  VM address:      $PUBLIC_IP
  Redis password:  $REDIS_PASSWORD
EOF
