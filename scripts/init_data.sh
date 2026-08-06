#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="$ROOT/data/history.db"
mkdir -p "$ROOT/data"
python3 - <<PY
import sqlite3
db = sqlite3.connect(r"$DB")
db.execute("""
CREATE TABLE IF NOT EXISTS detections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    count INTEGER NOT NULL,
    timestamp INTEGER NOT NULL
)
""")
db.commit()
db.close()
print("Initialized", r"$DB")
PY
echo '{"count":0,"timestamp":0}' > "$ROOT/data/persons.json"
