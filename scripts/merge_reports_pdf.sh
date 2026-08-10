#!/usr/bin/env bash
# Merge all part PDFs into one file, in cascade order:
#   part 1 explain → part 1 report → part 2 explain → part 2 report → … part 4
#
# Install once:
#   sudo apt install -y poppler-utils
#
# Run (after scripts/export_pdf_pandoc.sh):
#   cd ~/embedded_project
#   bash scripts/merge_reports_pdf.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPORT="$ROOT/report"
OUT="$REPORT/SmartGuard_402102657_full_report.pdf"

if ! command -v pdfunite >/dev/null 2>&1; then
  echo "ERROR: pdfunite not found (poppler-utils)."
  echo "  sudo apt install -y poppler-utils"
  exit 1
fi

files=()
missing=()

for part in "part 1" "part 2" "part 3" "part 4"; do
  for name in explain report; do
    pdf="$REPORT/$part/$name.pdf"
    if [[ -f "$pdf" ]]; then
      files+=("$pdf")
      echo "  + $part/$name.pdf"
    else
      missing+=("$part/$name.pdf")
    fi
  done
done

if ((${#missing[@]})); then
  echo
  echo "WARNING: missing PDFs (run scripts/export_pdf_pandoc.sh first):"
  printf '  - %s\n' "${missing[@]}"
fi

if ((${#files[@]} < 2)); then
  echo "ERROR: need at least 2 PDFs to merge (found ${#files[@]})."
  exit 1
fi

echo
echo "==> merging ${#files[@]} files"
pdfunite "${files[@]}" "$OUT"

echo "[ok] $OUT"
pdfinfo "$OUT" 2>/dev/null | grep -E '^(Pages|File size)' || true
ls -lh "$OUT"
