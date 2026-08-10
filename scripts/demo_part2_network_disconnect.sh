#!/usr/bin/env bash
# Part 2-4 — Disconnect WSL ↔ Windows link for ~2 minutes, then reconnect.
#
# Saves screenshot-ready text (open these and screenshot the window):
#   report/part 2/fig/06_network_disconnect_session.log
#   report/part 2/fig/06_network_disconnect_journal.txt
#
# Usage:
#   cd ~/embedded_project
#   make -C web && sudo systemctl restart web_server human_detector
#   # open stream from Windows browser first
#   bash scripts/demo_part2_network_disconnect.sh
#   # then screenshot the two files:
#   less -S "report/part 2/fig/06_network_disconnect_session.log"
#   less -S "report/part 2/fig/06_network_disconnect_journal.txt"
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIG="$ROOT/report/part 2/fig"
mkdir -p "$FIG"
LOG="$FIG/06_network_disconnect_session.log"
JTXT="$FIG/06_network_disconnect_journal.txt"
WAIT_SEC="${WAIT_SEC:-120}"
SINCE_FILE="$FIG/.part24_since"

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

# Fresh files every run (easy to screenshot from start)
: >"$LOG"
date -Iseconds >"$SINCE_FILE"
SINCE="$(cat "$SINCE_FILE")"

append_log() { tee -a "$LOG"; }

{
  echo "============================================================"
  echo " Part 2-4 — WSL <-> Windows network disconnect / reconnect"
  echo "============================================================"
  echo "start:     $(ts)"
  echo "iface:     $IFACE"
  echo "down_for:  ${WAIT_SEC}s"
  echo "session:   $LOG"
  echo "journal:   $JTXT"
  echo
  echo "---------- BEFORE (link UP) ----------"
  ip -br addr || true
  echo
  GW="$(ip route show default 2>/dev/null | awk '{print $3; exit}')"
  echo "gateway: ${GW:-none}"
  if [[ -n "${GW:-}" ]]; then
    echo "ping gateway BEFORE:"
    ping -c 2 -W 2 "$GW" 2>&1 || true
  fi
  echo
  echo "local telemetry BEFORE:"
  curl -sk --max-time 3 https://127.0.0.1:8443/api/v1/telemetry || echo "(curl failed)"
  echo
} | append_log

echo
echo ">>> Open the live stream from the WINDOWS browser now."
echo ">>> Press Enter to disconnect $IFACE ..."
read -r _

{
  echo
  echo "---------- DISCONNECT ----------"
  echo "time:    $(ts)"
  echo "command: sudo ip link set $IFACE down"
} | append_log

sudo ip link set "$IFACE" down

{
  echo "link after down:"
  ip -br link show "$IFACE" || true
  echo "operstate: $(cat /sys/class/net/$IFACE/operstate 2>/dev/null || echo unknown)"
  echo
  echo "local loopback during outage (should still work inside WSL):"
  curl -sk --max-time 3 https://127.0.0.1:8443/api/v1/telemetry || echo "(curl failed)"
  echo
  echo "NOTE: Windows browser stream should freeze / fail while $IFACE is DOWN."
  echo
} | append_log

elapsed=0
while [[ "$elapsed" -lt "$WAIT_SEC" ]]; do
  {
    echo "[DOWN +${elapsed}s] $(ts)  iface=$IFACE still disconnected"
  } | append_log
  logger -t smartguard_part2_4 "STREAM_NETWORK_DOWN iface=$IFACE elapsed=${elapsed}s WSL_Windows_link_lost" || true
  logger -t web_server "[network] WSL uplink DOWN ($IFACE) elapsed=${elapsed}s — remote stream clients DISCONNECT" || true
  step=30
  if [[ $((elapsed + step)) -gt "$WAIT_SEC" ]]; then
    step=$((WAIT_SEC - elapsed))
  fi
  sleep "$step"
  elapsed=$((elapsed + step))
done

{
  echo
  echo "---------- RECONNECT ----------"
  echo "time:    $(ts)"
  echo "command: sudo ip link set $IFACE up"
} | append_log

sudo ip link set "$IFACE" up
sleep 2
if ! ip -4 addr show "$IFACE" | grep -q 'inet '; then
  sudo dhclient -v "$IFACE" 2>&1 | append_log || true
fi
sleep 2
logger -t smartguard_part2_4 "STREAM_NETWORK_UP iface=$IFACE WSL_Windows_link_restored" || true
logger -t web_server "[network] WSL uplink UP ($IFACE) — interconnect restored; HTTPS stream available again" || true

{
  echo "link after up:"
  ip -br addr || true
  echo "operstate: $(cat /sys/class/net/$IFACE/operstate 2>/dev/null || echo unknown)"
  echo
  GW="$(ip route show default 2>/dev/null | awk '{print $3; exit}')"
  echo "gateway: ${GW:-none}"
  if [[ -n "${GW:-}" ]]; then
    echo "ping gateway AFTER:"
    ping -c 2 -W 2 "$GW" 2>&1 || true
  fi
  echo
  echo "local telemetry AFTER reconnect:"
  curl -sk --max-time 5 https://127.0.0.1:8443/api/v1/telemetry || echo "(curl failed)"
  echo
  echo "end: $(ts)"
  echo "============================================================"
  echo " RESULT: $IFACE was DOWN ~${WAIT_SEC}s then restored."
  echo " Screenshot this file for Figure 2-4a (session evidence)."
  echo "============================================================"
} | append_log

# ----- journal file (screenshot-ready, focused) -----
{
  echo "============================================================"
  echo " Part 2-4 — journal evidence (stream / network disconnect)"
  echo "============================================================"
  echo "captured: $(ts)"
  echo "since:    $SINCE"
  echo
  echo "---------- A) Clear disconnect / reconnect markers ----------"
  journalctl -t smartguard_part2_4 --since "$SINCE" --no-pager 2>/dev/null || true
  echo
  echo "---------- B) network / stream disconnect lines ----------"
  journalctl --since "$SINCE" --no-pager 2>/dev/null \
    | grep -E 'smartguard_part2_4|\[network\]|\[stream\]|DISCONNECT|STREAM_NETWORK|uplink|Could not connect' \
    || echo "(no grep hits — see section A)"
  echo
  echo "---------- C) web_server / human_detector (tail) ----------"
  journalctl -u web_server -u human_detector --since "$SINCE" --no-pager 2>/dev/null | tail -n 80 || true
  echo
  echo "============================================================"
  echo " Screenshot this file for Figure 2-4a (journal evidence)."
  echo " Also screenshot YOUR real dashboard after reconnect → fig/07"
  echo "============================================================"
} >"$JTXT"

echo
echo "################################################################"
echo "# Saved for screenshot:"
echo "#   $LOG"
echo "#   $JTXT"
echo "#"
echo "# Open and screenshot:"
echo "#   less -S \"$LOG\""
echo "#   less -S \"$JTXT\""
echo "# Save PNGs as:"
echo "#   report/part 2/fig/06_network_disconnect_logs.png"
echo "#   report/part 2/fig/07_network_recovery.png  (your browser)"
echo "################################################################"
