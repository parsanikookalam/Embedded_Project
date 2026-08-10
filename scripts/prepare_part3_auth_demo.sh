#!/usr/bin/env bash
# One-shot prepare broker + sshd for Windows demos (Part 3-6 / 3-7).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Fix CRLF on scripts"
sed -i 's/\r$//' scripts/*.sh scripts/windows/*.ps1 2>/dev/null || sed -i 's/\r$//' scripts/*.sh

echo "==> MQTT broker"
bash scripts/setup_mosquitto.sh

echo "==> SSHD (port 2222, root OFF)"
bash scripts/setup_sshd_wsl.sh

IP="$(hostname -I | awk '{print $1}')"
echo
echo "================ READY ================"
echo "WSL IP: $IP"
echo
echo "--- Windows PowerShell #1 : SSH SUCCESS ---"
echo "  powershell -ExecutionPolicy Bypass -File \\\\wsl\$\\Ubuntu\\home\\parsa\\embedded_project\\scripts\\windows\\ssh_to_wsl.ps1"
echo "  Screenshot shell → report/part 3/fig/10a_ssh_auth_ok.png"
echo
echo "--- Windows PowerShell #1b : SSH FAIL ---"
echo "  powershell -ExecutionPolicy Bypass -File \\\\wsl\$\\Ubuntu\\home\\parsa\\embedded_project\\scripts\\windows\\ssh_to_wsl.ps1 -FailDemo"
echo "  Screenshot → report/part 3/fig/10b_ssh_auth_fail.png"
echo
echo "--- Windows PowerShell #2 : MQTT LISTEN (leave open) ---"
echo "  powershell -ExecutionPolicy Bypass -File \\\\wsl\$\\Ubuntu\\home\\parsa\\embedded_project\\scripts\\windows\\mqtt_listen.ps1"
echo
echo "--- Windows PowerShell #3 : MQTT OK then FAIL ---"
echo "  powershell -ExecutionPolicy Bypass -File \\\\wsl\$\\Ubuntu\\home\\parsa\\embedded_project\\scripts\\windows\\mqtt_test_auth.ps1 -Mode ok"
echo "  Screenshot → report/part 3/fig/09a_mqtt_auth_ok.png"
echo "  powershell -ExecutionPolicy Bypass -File \\\\wsl\$\\Ubuntu\\home\\parsa\\embedded_project\\scripts\\windows\\mqtt_test_auth.ps1 -Mode fail"
echo "  Screenshot → report/part 3/fig/09b_mqtt_auth_fail.png"
echo "========================================"
