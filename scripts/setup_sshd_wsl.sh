#!/usr/bin/env bash
# Part 3-7 — OpenSSH in WSL: root login OFF; password OR pubkey allowed.
# Also prints how to SSH from Windows → WSL for the lab.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SSHD_PORT="${SSHD_PORT:-2222}"
DROPIN="/etc/ssh/sshd_config.d/99-smartguard.conf"

echo "==> Install OpenSSH server"
sudo apt-get install -y openssh-server >/dev/null

echo "==> Write hardened drop-in ($DROPIN) port=$SSHD_PORT"
sudo tee "$DROPIN" >/dev/null <<EOF
# Smart Guard Part 3-7
Port $SSHD_PORT
ListenAddress 0.0.0.0
PermitRootLogin no
PubkeyAuthentication yes
PasswordAuthentication yes
ChallengeResponseAuthentication no
UsePAM yes
X11Forwarding no
AllowTcpForwarding yes
EOF

# Ensure main config does not force root yes (drop-in overrides when Include is used)
if ! grep -qE '^\s*Include\s+/etc/ssh/sshd_config.d/\*' /etc/ssh/sshd_config 2>/dev/null; then
  echo "Include /etc/ssh/sshd_config.d/*.conf" | sudo tee -a /etc/ssh/sshd_config >/dev/null
fi

sudo mkdir -p /run/sshd
sudo ssh-keygen -A >/dev/null 2>&1 || true

echo "==> Enable / restart ssh"
sudo systemctl enable ssh >/dev/null 2>&1 || sudo systemctl enable sshd >/dev/null 2>&1 || true
sudo systemctl restart ssh 2>/dev/null || sudo systemctl restart sshd

sleep 1
echo -n "sshd: "
systemctl is-active ssh 2>/dev/null || systemctl is-active sshd 2>/dev/null || echo "unknown"

WSL_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
USER_NAME="$(whoami)"

echo
echo "========== WSL SSH ready =========="
echo "User:     $USER_NAME"
echo "Port:     $SSHD_PORT"
echo "WSL IP:   ${WSL_IP:-unknown}"
echo "Root:     PermitRootLogin=no"
echo "Auth:     PasswordAuthentication=yes  +  PubkeyAuthentication=yes"
echo
echo "From Windows PowerShell (authorized — use YOUR password):"
echo "  ssh -p $SSHD_PORT ${USER_NAME}@${WSL_IP}"
echo "  # or often: ssh -p $SSHD_PORT ${USER_NAME}@127.0.0.1"
echo "  # (if localhostForwarding is on in .wslconfig)"
echo
echo "Unauthorized test (for fig/10_ssh_auth_fail.png):"
echo "  ssh -p $SSHD_PORT -o PreferredAuthentications=password -o PubkeyAuthentication=no \\"
echo "    fakeuser@${WSL_IP:-127.0.0.1}"
echo "  # expect: Permission denied"
echo
echo "Root must fail:"
echo "  ssh -p $SSHD_PORT root@${WSL_IP:-127.0.0.1}"
echo "===================================="

# Save helper for Windows
mkdir -p "$ROOT/scripts/windows"
cat >"$ROOT/scripts/windows/ssh_to_wsl.ps1" <<EOF
# Connect Windows → WSL OpenSSH (Smart Guard Part 3-7)
param(
  [string]\$User = "$USER_NAME",
  [int]\$Port = $SSHD_PORT
)
\$ip = (wsl -d Ubuntu -- hostname -I).Trim().Split(" ")[0]
Write-Host "SSH to WSL: \$User@\$ip :\$Port"
ssh -p \$Port "\$User@\$ip"
EOF

echo "Wrote scripts/windows/ssh_to_wsl.ps1"
echo "Done."
