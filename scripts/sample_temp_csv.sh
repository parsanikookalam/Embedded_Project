#!/usr/bin/env bash
# Sample CPU telemetry → report/part 2/fig/temp_<state>.csv
# Usage (from project root or anywhere):
#   bash scripts/sample_temp_csv.sh idle
#   bash scripts/sample_temp_csv.sh stream
#   bash scripts/sample_temp_csv.sh detect
#
# Each run: 11 samples, every 30 s, total ~5 minutes.
# Needs: web_server up on https://127.0.0.1:8443
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIG="$ROOT/report/part 2/fig"
URL="${TELEMETRY_URL:-https://127.0.0.1:8443/api/v1/telemetry}"
CMD_URL="${CMD_URL:-https://127.0.0.1:8443/api/v1/command}"
INTERVAL="${SAMPLE_INTERVAL_SEC:-30}"
COUNT="${SAMPLE_COUNT:-11}"   # 0..10 inclusive = 11 points over 5 min

STATE="${1:-}"
case "$STATE" in
  idle|stream|detect) ;;
  *)
    echo "Usage: $0 idle|stream|detect"
    echo "  idle   → camera_off, no stream → temp_idle.csv"
    echo "  stream → camera_on (open dashboard stream yourself, stay out of view)"
    echo "  detect → camera_on (open stream, stand in front of camera)"
    exit 1
    ;;
esac

mkdir -p "$FIG"
OUT="$FIG/temp_${STATE}.csv"

send_cmd() {
  local cmd="$1"
  curl -sk -X POST "$CMD_URL" \
    -H 'Content-Type: application/json' \
    -d "{\"cmd\":\"$cmd\"}" >/dev/null || true
}

echo "== Smart Guard temp sample =="
echo "state=$STATE  out=$OUT"
echo "url=$URL  interval=${INTERVAL}s  samples=$COUNT"

case "$STATE" in
  idle)
    echo "→ camera_off (do NOT open the live stream)"
    send_cmd camera_off
    ;;
  stream)
    echo "→ camera_on — NOW open https://127.0.0.1:8443/ stream and stay OUT of camera view"
    send_cmd camera_on
    echo "Press Enter when stream is open and nobody is in view..."
    read -r _
    ;;
  detect)
    echo "→ camera_on — open stream and stand IN FRONT of camera (persons ≥ 1)"
    send_cmd camera_on
    send_cmd detection_on
    echo "Press Enter when stream shows you / persons ≥ 1..."
    read -r _
    ;;
esac

# Header
printf '%s\n' 't_sec,cpu_temp,cpu_usage_percent,mem_used_percent' >"$OUT"

i=0
while [ "$i" -lt "$COUNT" ]; do
  t_sec=$((i * INTERVAL))
  json="$(curl -sk --max-time 10 "$URL" || true)"
  if [ -z "$json" ]; then
    echo "[$t_sec s] ERROR: empty response from $URL (is web_server running?)"
    echo "$t_sec,,,," >>"$OUT"
  else
    # Parse with python (always available; no jq required)
    line="$(python3 - "$json" "$t_sec" <<'PY'
import json, sys
raw = sys.argv[1]
t = sys.argv[2]
try:
    d = json.loads(raw)
    print("{},{},{},{}".format(
        t,
        d.get("cpu_temp", ""),
        d.get("cpu_usage_percent", ""),
        d.get("mem_used_percent", ""),
    ))
except Exception as e:
    print("{},,,,  # bad json: {}".format(t, e), file=sys.stderr)
    print("{},,,,".format(t))
PY
)"
    echo "$line" >>"$OUT"
    echo "[$t_sec s] $line"
  fi

  i=$((i + 1))
  if [ "$i" -lt "$COUNT" ]; then
    sleep "$INTERVAL"
  fi
done

echo
echo "Saved: $OUT"
echo "Rows:"
wc -l "$OUT"
cat "$OUT"
echo
echo "When idle+stream+detect CSVs are all filled, plot with:"
echo "  .venv/bin/python scripts/plot_part2_figs.py"
