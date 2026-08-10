#!/usr/bin/env bash
# Part 3 — Mosquitto with dedicated credentials; anonymous MUST fail.
# Reads MQTT_USER / MQTT_PASS from config.env (defaults: smartguard/smartguard).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MQTT_DIR="$ROOT/mqtt"
CFG="$ROOT/config.env"

MQTT_USER="smartguard"
MQTT_PASS="smartguard"
if [[ -f "$CFG" ]]; then
  u="$(grep -E '^MQTT_USER=' "$CFG" 2>/dev/null | head -n1 | cut -d= -f2- | tr -d '\r' | tr -d '"' | tr -d "'" || true)"
  p="$(grep -E '^MQTT_PASS=' "$CFG" 2>/dev/null | head -n1 | cut -d= -f2- | tr -d '\r' | tr -d '"' | tr -d "'" || true)"
  [[ -n "${u:-}" ]] && MQTT_USER="$u"
  [[ -n "${p:-}" ]] && MQTT_PASS="$p"
fi

PASSWD_FILE="$MQTT_DIR/passwd"
CONF_FILE="$MQTT_DIR/mosquitto.conf"
ACL_FILE="$MQTT_DIR/acl"

echo "==> Packages (mosquitto + clients)"
sudo apt-get install -y mosquitto mosquitto-clients libmosquitto-dev >/dev/null

mkdir -p "$MQTT_DIR"

echo "==> Dedicated MQTT user: $MQTT_USER (anonymous disabled)"
mosquitto_passwd -b -c "$PASSWD_FILE" "$MQTT_USER" "$MQTT_PASS"
chmod 600 "$PASSWD_FILE"

# Optional ACL: user may use home/# only (Part 3 topics)
cat >"$ACL_FILE" <<EOF
user $MQTT_USER
topic readwrite home/#
topic read \$SYS/#
EOF
chmod 644 "$ACL_FILE"

cat >"$CONF_FILE" <<EOF
# Smart Guard Part 3C — dedicated broker (no anonymous)
persistence false
log_dest stderr
connection_messages true
allow_anonymous false
password_file $PASSWD_FILE
acl_file $ACL_FILE
listener 1883 127.0.0.1
EOF

# Keep .in template in sync style for docs
cat >"$MQTT_DIR/mosquitto.conf.in" <<'EOF'
# Smart Guard Part 3C — project-local Mosquitto (runs as your user)
persistence false
log_dest stderr
connection_messages true
allow_anonymous false
password_file PASSWD_PATH_PLACEHOLDER
acl_file ACL_PATH_PLACEHOLDER
listener 1883 127.0.0.1
EOF

echo "==> Stop stock mosquitto if conflicting on :1883"
sudo systemctl stop mosquitto 2>/dev/null || true
sudo systemctl disable mosquitto 2>/dev/null || true
sudo systemctl reset-failed mosquitto 2>/dev/null || true

sudo cp "$ROOT/services/mosquitto_smartguard.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable mosquitto_smartguard.service
sudo systemctl restart mosquitto_smartguard.service
sleep 1
echo -n "broker: "; systemctl is-active mosquitto_smartguard

# Ensure config.env MQTT_* keys
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

echo
echo "==> Quick auth check"
if mosquitto_pub -h 127.0.0.1 -p 1883 -t 'test/anon' -m x 2>/tmp/mqtt_anon.err; then
  echo "FAIL: anonymous was accepted (must be false)"
  exit 1
fi
echo "OK: anonymous rejected"
if ! mosquitto_pub -h 127.0.0.1 -p 1883 -u "$MQTT_USER" -P "$MQTT_PASS" -t "home/$(grep -E '^STUDENT_ID=' "$CFG" | cut -d= -f2 | tr -d '\r')" -m '{"ok":1}' -q 1; then
  echo "FAIL: authorized publish failed"
  exit 1
fi
echo "OK: authorized user can publish"
echo
echo "Subscribe: mosquitto_sub -h 127.0.0.1 -u $MQTT_USER -P '$MQTT_PASS' -t 'home/#' -v"
echo "Done."
