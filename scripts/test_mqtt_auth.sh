#!/usr/bin/env bash
# Part 3-6 — prove anonymous / wrong password MQTT fails.
set -eu
HOST=127.0.0.1
PORT=1883
USER="${MQTT_USER:-smartguard}"

echo "=== MQTT auth test (expect FAIL then FAIL then OK) ==="
echo
echo "[1] Anonymous publish (must FAIL):"
if mosquitto_pub -h "$HOST" -p "$PORT" -t 'test/anon' -m 'x' -d 2>&1; then
  echo "UNEXPECTED SUCCESS"
  exit 1
else
  echo "→ rejected (good for experiment 3-6)"
fi

echo
echo "[2] Wrong password (must FAIL):"
if mosquitto_pub -h "$HOST" -p "$PORT" -u "$USER" -P 'wrongpass_not_real' \
  -t 'test/bad' -m 'x' -d 2>&1; then
  echo "UNEXPECTED SUCCESS"
  exit 1
else
  echo "→ rejected (good)"
fi

echo
echo "[3] Correct credentials (must OK):"
PASS="${MQTT_PASS:-smartguard}"
if [[ -f "$(cd "$(dirname "$0")/.." && pwd)/config.env" ]]; then
  CFG="$(cd "$(dirname "$0")/.." && pwd)/config.env"
  p="$(grep -E '^MQTT_PASS=' "$CFG" | head -n1 | cut -d= -f2- | tr -d '\r' | tr -d '"' | tr -d "'" || true)"
  [[ -n "${p:-}" ]] && PASS="$p"
  u="$(grep -E '^MQTT_USER=' "$CFG" | head -n1 | cut -d= -f2- | tr -d '\r' | tr -d '"' | tr -d "'" || true)"
  [[ -n "${u:-}" ]] && USER="$u"
fi
mosquitto_pub -h "$HOST" -p "$PORT" -u "$USER" -P "$PASS" -t "home/402102657/status" -m 'probe' -q 1
echo "→ authorized OK"
echo
echo "Screenshot [1] or [2] failure → report/part 3/fig/09_mqtt_auth_fail.png"
