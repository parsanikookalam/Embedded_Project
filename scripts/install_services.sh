#!/usr/bin/env bash
# Alias: full Part 1–2 install + verify for WSL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/scripts/fix_part12.sh"
