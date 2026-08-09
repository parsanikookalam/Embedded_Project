#!/usr/bin/env bash
# Print host Celsius for WSL. Prefers the Windows-updated cache file; else calls .ps1 once.
set -u

CACHE_CANDIDATES=(
  "${HOST_CPU_TEMP_FILE:-}"
  "/mnt/c/Users/${WIN_USER:-}/AppData/Local/SmartGuard/cpu_temp.txt"
  "/home/parsa/embedded_project/data/host_cpu_temp.txt"
)

# Discover Windows user SmartGuard cache
if [[ -d /mnt/c/Users ]]; then
  for u in /mnt/c/Users/*/AppData/Local/SmartGuard/cpu_temp.txt; do
    [[ -f "$u" ]] && CACHE_CANDIDATES+=("$u")
  done
fi

read_cache() {
  local f="$1"
  [[ -n "$f" && -f "$f" ]] || return 1
  # Reject stale (>90s)
  local now mtime age
  now=$(date +%s)
  mtime=$(stat -c %Y "$f" 2>/dev/null || echo 0)
  age=$((now - mtime))
  [[ "$age" -lt 90 ]] || return 1
  local v
  v=$(tr -d '\r' <"$f" | head -1 | tr ',' '.' | grep -Eo '[0-9]+([.][0-9]+)?')
  [[ -n "$v" ]] || return 1
  awk -v c="$v" 'BEGIN{ if (c>5 && c<115) { printf "%.2f\n", c; exit 0 } exit 1 }'
}

for f in "${CACHE_CANDIDATES[@]}"; do
  [[ -z "$f" ]] && continue
  if out=$(read_cache "$f"); then
    echo "$out"
    exit 0
  fi
done

PS="/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
SCRIPT="/home/parsa/embedded_project/scripts/windows/host_cpu_temp.ps1"
[[ -x "$PS" || -f "$PS" ]] || PS="powershell.exe"
[[ -f "$SCRIPT" ]] || exit 1

# Translate WSL path → Windows path for -File
WIN_SCRIPT=$("$PS" -NoProfile -Command \
  "Write-Output ((Get-Item -LiteralPath '//wsl$/Ubuntu/home/parsa/embedded_project/scripts/windows/host_cpu_temp.ps1').FullName)" \
  2>/dev/null | tr -d '\r' | head -1)

if [[ -z "${WIN_SCRIPT:-}" ]]; then
  WIN_SCRIPT="\\\\wsl\$\\Ubuntu\\home\\parsa\\embedded_project\\scripts\\windows\\host_cpu_temp.ps1"
fi

out="$("$PS" -NoProfile -ExecutionPolicy Bypass -File "$WIN_SCRIPT" 2>/dev/null | tr -d '\r' | \
  grep -Eo '[0-9]+([.][0-9]+)?' | head -1)"

if [[ -n "${out:-}" ]]; then
  echo "$out"
  exit 0
fi
exit 1
