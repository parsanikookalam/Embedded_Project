#!/usr/bin/env bash
# Part 3-4 helper — LWT offline/online demo.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ID="$(grep -E '^STUDENT_ID=' "$ROOT/config.env" 2>/dev/null | cut -d= -f2- | tr -d '\r' || echo 402102657)"
USER="$(grep -E '^MQTT_USER=' "$ROOT/config.env" 2>/dev/null | cut -d= -f2- | tr -d '\r' || echo smartguard)"
PASS="$(grep -E '^MQTT_PASS=' "$ROOT/config.env" 2>/dev/null | cut -d= -f2- | tr -d '\r' || echo smartguard)"
TOPIC="home/${ID}/status"

echo "Subscribe in another terminal:"
echo "  mosquitto_sub -h 127.0.0.1 -u $USER -P '$PASS' -t '$TOPIC' -v"
echo
echo "Press Enter to STOP broker for 3 minutes (LWT → offline)..."
read -r _
sudo systemctl stop mosquitto_smartguard
echo "Broker stopped at $(date). Wait 3 minutes (or Ctrl+C to skip wait)..."
sleep 180 || true
echo "Starting broker again..."
sudo systemctl start mosquitto_smartguard
sudo systemctl restart web_server
echo "Done — subscriber should show offline then online. Screenshot → fig/07_mqtt_lwt.png"
