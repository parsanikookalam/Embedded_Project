#!/usr/bin/env bash
# Quick MQTT broker self-check for Smart Guard.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST=127.0.0.1
PORT=1883
USER=smartguard
PASS=smartguard
ID="$(grep -E '^STUDENT_ID=' "$ROOT/config.env" 2>/dev/null | cut -d= -f2- | tr -d '\r' || echo 402102657)"
TOPIC="home/${ID}/alarm"

echo "=== 1) services ==="
systemctl is-active mosquitto_smartguard 2>/dev/null || echo "mosquitto_smartguard: inactive"
systemctl is-active web_server 2>/dev/null || echo "web_server: inactive"

echo
echo "=== 2) port 1883 ==="
ss -ltnp 2>/dev/null | grep 1883 || echo "NOT listening on 1883"

echo
echo "=== 3) auth reject (anonymous should FAIL) ==="
if mosquitto_pub -h "$HOST" -p "$PORT" -t test -m x 2>/tmp/mqtt_anon.err; then
  echo "WARN: anonymous publish succeeded (auth may be off)"
else
  echo "OK: anonymous rejected"
  head -n 2 /tmp/mqtt_anon.err 2>/dev/null || true
fi

echo
echo "=== 4) loopback pub/sub (must print TEST_OK) ==="
OUT=/tmp/mqtt_sub_out.$$
rm -f "$OUT"
mosquitto_sub -h "$HOST" -p "$PORT" -u "$USER" -P "$PASS" -t "$TOPIC" -C 1 -W 5 -v >"$OUT" 2>/tmp/mqtt_sub.err &
SPID=$!
sleep 0.7
if mosquitto_pub -h "$HOST" -p "$PORT" -u "$USER" -P "$PASS" -t "$TOPIC" -q 1 -m '{"alarm":true,"test":1}'; then
  echo "pub: OK"
else
  echo "pub: FAIL"
fi
wait "$SPID" 2>/dev/null || true
if grep -q 'alarm' "$OUT" 2>/dev/null; then
  echo "TEST_OK — received:"
  cat "$OUT"
else
  echo "TEST_FAIL — no message received"
  echo "sub stderr:"; cat /tmp/mqtt_sub.err 2>/dev/null || true
  echo "sub stdout:"; cat "$OUT" 2>/dev/null || true
fi

echo
echo "=== 5) web_server mqtt logs ==="
journalctl -u web_server -n 50 --no-pager 2>/dev/null | grep -iE 'mqtt|alarm|connect' || echo "(no mqtt lines in last 50)"

echo
echo "=== how to watch live ==="
echo "mosquitto_sub -h 127.0.0.1 -u smartguard -P smartguard -t 'home/${ID}/#' -v"
echo "Topics: persons telemetry status alarm watchdog thermal"
echo "Then ARM Guard / enable Watchdog / Thermal and trigger events."
