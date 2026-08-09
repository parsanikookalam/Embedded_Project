#!/usr/bin/env bash
# Pretty black-box view for Part 4-2 screenshot.
# Usage: bash scripts/show_blackbox.sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="$ROOT/data/history.db"
URL="${BLACKBOX_URL:-https://127.0.0.1:8443/api/v1/blackbox}"

echo "========== BLACK BOX STATS =========="
curl -sk "$URL" | python3 -m json.tool
echo
echo "========== LAST 20 EVENTS (SQLite) =========="
printf '%-8s %-8s %-22s\n' "id" "count" "time_utc"
printf '%-8s %-8s %-22s\n' "--------" "--------" "----------------------"
sqlite3 -separator '|' "$DB" \
  "SELECT id, count, datetime(timestamp,'unixepoch') FROM detections ORDER BY id DESC LIMIT 20;" \
| while IFS='|' read -r id count ts; do
    printf '%-8s %-8s %-22s\n' "$id" "$count" "$ts"
  done
echo
echo "========== DONE =========="
