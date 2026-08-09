#!/usr/bin/env bash
# One-shot: remove leftover network_gate systemd unit (Wi-Fi gate feature deleted).
set -euo pipefail
sudo systemctl stop network_gate.service 2>/dev/null || true
sudo systemctl disable network_gate.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/network_gate.service
sudo systemctl daemon-reload
sudo systemctl reset-failed 2>/dev/null || true
echo "network_gate removed from systemd."
systemctl is-active web_server human_detector api_gateway || true
