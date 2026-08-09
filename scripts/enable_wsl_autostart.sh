#!/usr/bin/env bash
# Make Smart Guard start automatically whenever THIS WSL distro boots.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> 1) Enable systemd in WSL"
sudo tee /etc/wsl.conf >/dev/null <<'EOF'
[boot]
systemd=true
EOF
echo "Wrote /etc/wsl.conf:"
cat /etc/wsl.conf

echo
echo "==> 2) Ensure binary + venv exist"
[[ -x "$ROOT/web/web_server" ]] || make -C "$ROOT/web"
[[ -x "$ROOT/.venv/bin/python" ]] || bash "$ROOT/scripts/setup_venv.sh"

echo
echo "==> 3) Install + enable units"
sudo usermod -aG video parsa 2>/dev/null || true
sudo cp "$ROOT/services/"*.service "$ROOT/services/"*.target /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable smart-guard.target
sudo systemctl enable human_detector.service web_server.service api_gateway.service

echo
echo "==> 4) Start now"
sudo systemctl restart smart-guard.target
sleep 2
systemctl is-enabled smart-guard human_detector web_server api_gateway || true
systemctl is-active human_detector web_server api_gateway || true
systemctl --no-pager --full status human_detector web_server api_gateway | sed -n '1,40p' || true

echo
echo "============================================================"
echo "REQUIRED once so systemd becomes PID1 on next boot:"
echo
echo "  1) Close all Ubuntu / WSL terminals"
echo "  2) In Windows PowerShell:"
echo "       wsl --shutdown"
echo "  3) Open Ubuntu again"
echo "  4) Run:"
echo "       systemctl is-system-running"
echo "       systemctl is-active human_detector web_server api_gateway"
echo
echo "Expected: running / active active active"
echo "Camera still only turns on when you open https://127.0.0.1:8443/"
echo "============================================================"
