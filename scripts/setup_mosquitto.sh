#!/usr/bin/env bash
# Project-local Mosquitto for Smart Guard Part 3C (WSL).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MQTT_DIR="$ROOT/mqtt"
MQTT_USER="${MQTT_USER:-smartguard}"
MQTT_PASS="${MQTT_PASS:-smartguard}"
PASSWD_FILE="$MQTT_DIR/passwd"
CONF_FILE="$MQTT_DIR/mosquitto.conf"

echo "==> Packages"
sudo apt-get install -y mosquitto mosquitto-clients libmosquitto-dev >/dev/null

mkdir -p "$MQTT_DIR"
mosquitto_passwd -b -c "$PASSWD_FILE" "$MQTT_USER" "$MQTT_PASS"
chmod 600 "$PASSWD_FILE"

sed "s|PASSWD_PATH_PLACEHOLDER|$PASSWD_FILE|g" \
  "$MQTT_DIR/mosquitto.conf.in" >"$CONF_FILE"

sudo systemctl stop mosquitto 2>/dev/null || true
sudo systemctl disable mosquitto 2>/dev/null || true
sudo systemctl reset-failed mosquitto 2>/dev/null || true
if command -v fuser >/dev/null 2>&1; then
  sudo fuser -k 1883/tcp 2>/dev/null || true
fi

sudo cp "$ROOT/services/mosquitto_smartguard.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable mosquitto_smartguard.service
sudo systemctl restart mosquitto_smartguard.service
sleep 1
systemctl is-active mosquitto_smartguard

CFG="$ROOT/config.env"
upsert() {
  local key="$1" val="$2"
  if grep -q "^${key}=" "$CFG" 2>/dev/null; then
    sed -i "s|^${key}=.*|${key}=${val}|" "$CFG"
  else
    printf '%s=%s\n' "$key" "$val" >>"$CFG"
  fi
}
upsert MQTT_ENABLED 1
upsert MQTT_HOST 127.0.0.1
upsert MQTT_PORT 1883
upsert MQTT_USER "$MQTT_USER"
upsert MQTT_PASS "$MQTT_PASS"
upsert MQTT_INTERVAL_SEC 2

echo "OK. mosquitto_sub -h 127.0.0.1 -u $MQTT_USER -P '$MQTT_PASS' -t 'home/#' -v"
