#!/usr/bin/env bash
# Part 2-4 — Disconnect WSL ↔ Windows link for ~2 minutes, then reconnect.
# TA meaning: break the virtual NIC between WSL and Windows (not only Wi‑Fi).
#
# Usage (inside WSL):
#   bash scripts/demo_part2_network_disconnect.sh
#
# Before running:
#   1) web_server + human_detector active
#   2) Camera ON, open the live stream from *Windows* browser
#      (https://127.0.0.1:8443/ or https://<WSL-IP>:8443/)
#   3) Leave that browser tab open
#
# During DOWN (~2 min): Windows→WSL stream/API should stall or fail.
# After UP: stream recovers; capture journal + this script's log for fig/06.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIG="$ROOT/report/part 2/fig"
mkdir -p "$FIG"
LOG="$FIG/06_network_disconnect_session.log"
WAIT_SEC="${WAIT_SEC:-120}"

IFACE="${IFACE:-}"
if [[ -z "$IFACE" ]]; then
  IFACE="$(ip route show default 2>/dev/null | awk '{print $5; exit}')"
fi
if [[ -z "${IFACE:-}" ]]; then
  for c in eth0 eth1; do
    if ip link show "$c" &>/dev/null; then IFACE=$c; break; fi
  done
fi
if [[ -z "${IFACE:-}" ]]; then
  echo "ERROR: could not detect uplink interface. Set IFACE=eth0 manually."
  exit 1
fi

ts() { date '+%Y-%m-%d %H:%M:%S%z'; }

{
  echo "===== Part 2-4 WSL↔Windows disconnect demo ====="
  echo "start: $(ts)"
  echo "iface: $IFACE"
  echo "wait:  ${WAIT_SEC}s"
  echo
  echo "-- interfaces BEFORE --"
  ip -br addr || true
  echo
  echo "-- ping Windows host (gateway) BEFORE --"
  GW="$(ip route show default 2>/dev/null | awk '{print $3; exit}')"
  echo "gateway: ${GW:-none}"
  if [[ -n "${GW:-}" ]]; then ping -c 2 -W 2 "$GW" 2>&1 || true; fi
  echo
} | tee "$LOG"

echo
echo ">>> Open/confirm stream from WINDOWS browser now."
echo ">>> Press Enter to disconnect $IFACE (WSL↔Windows link)..."
read -r _

{
  echo
  echo "== DISCONNECT at $(ts) =="
  echo "command: sudo ip link set $IFACE down"
} | tee -a "$LOG"

sudo ip link set "$IFACE" down

{
  echo "link state after down:"
  ip -br link show "$IFACE" || true
  echo
  echo "Local loopback check (should still work inside WSL):"
  curl -sk --max-time 3 https://127.0.0.1:8443/api/v1/telemetry | head -c 200 || echo "(curl failed)"
  echo
  echo "Sleeping ${WAIT_SEC}s — Windows browser should lose / freeze the stream..."
} | tee -a "$LOG"

elapsed=0
while [[ "$elapsed" -lt "$WAIT_SEC" ]]; do
  echo "[down +${elapsed}s] $(ts) still disconnected" | tee -a "$LOG"
  logger -t smartguard_part2_4 "network_down iface=$IFACE elapsed=${elapsed}s" || true
  step=30
  if [[ $((elapsed + step)) -gt "$WAIT_SEC" ]]; then
    step=$((WAIT_SEC - elapsed))
  fi
  sleep "$step"
  elapsed=$((elapsed + step))
done

{
  echo
  echo "== RECONNECT at $(ts) =="
  echo "command: sudo ip link set $IFACE up"
} | tee -a "$LOG"

sudo ip link set "$IFACE" up
sleep 2
if ! ip -4 addr show "$IFACE" | grep -q 'inet '; then
  sudo dhclient -v "$IFACE" 2>&1 | tee -a "$LOG" || true
fi
sleep 2

{
  echo
  echo "-- interfaces AFTER --"
  ip -br addr || true
  echo
  echo "-- ping gateway AFTER --"
  GW="$(ip route show default 2>/dev/null | awk '{print $3; exit}')"
  echo "gateway: ${GW:-none}"
  if [[ -n "${GW:-}" ]]; then ping -c 2 -W 2 "$GW" 2>&1 || true; fi
  echo
  echo "Local telemetry after reconnect:"
  curl -sk --max-time 5 https://127.0.0.1:8443/api/v1/telemetry | head -c 300 || true
  echo
  echo "end: $(ts)"
} | tee -a "$LOG"

JTXT="$FIG/06_network_disconnect_journal.txt"
{
  echo "===== journal web_server / human_detector (last 5 min) ====="
  journalctl -u web_server -u human_detector --since "5 min ago" --no-pager || true
  echo
  echo "===== journal smartguard_part2_4 markers ====="
  journalctl -t smartguard_part2_4 --since "5 min ago" --no-pager || true
} | tee "$JTXT"

echo
echo "===== Done ====="
echo "Session log: $LOG"
echo "Journal dump: $JTXT"
echo
echo "Screenshots:"
echo "  fig/06_network_disconnect_logs.png  ← terminal with this log / journalctl"
echo "  fig/07_network_recovery.png         ← Windows browser stream after reconnect"
