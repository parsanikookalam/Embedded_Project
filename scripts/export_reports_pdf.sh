#!/usr/bin/env bash
# Export each part's report.md + explain.md to PDF in the same folder.
# Prefers: pandoc -> weasyprint -> reportlab fallback HTML->PDF via chromium.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPORT="$ROOT/report"

export_one() {
  local md="$1"
  local pdf="${md%.md}.pdf"
  echo "==> $md -> $pdf"

  if command -v pandoc >/dev/null 2>&1; then
    if pandoc "$md" -o "$pdf" --pdf-engine=xelatex -V geometry:margin=1in 2>/dev/null; then
      return 0
    fi
    if pandoc "$md" -o "$pdf" --pdf-engine=pdflatex -V geometry:margin=1in 2>/dev/null; then
      return 0
    fi
    if pandoc "$md" -o "$pdf" --pdf-engine=weasyprint 2>/dev/null; then
      return 0
    fi
    if pandoc "$md" -o "$pdf" --pdf-engine=wkhtmltopdf 2>/dev/null; then
      return 0
    fi
    local html="${md%.md}.__tmp.html"
    pandoc "$md" -o "$html" --standalone --metadata title="$(basename "$md" .md)"
    if command -v google-chrome >/dev/null 2>&1; then
      google-chrome --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="$pdf" "file://$html" && rm -f "$html" && return 0
    fi
    if command -v chromium-browser >/dev/null 2>&1; then
      chromium-browser --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="$pdf" "file://$html" && rm -f "$html" && return 0
    fi
    if command -v chromium >/dev/null 2>&1; then
      chromium --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="$pdf" "file://$html" && rm -f "$html" && return 0
    fi
    rm -f "$html"
  fi

  if [[ -x "$ROOT/.venv/bin/python" ]]; then
    PY="$ROOT/.venv/bin/python"
  else
    PY=python3
  fi
  "$PY" "$ROOT/scripts/md_to_pdf.py" "$md" "$pdf"
}

for part in "part 1" "part 2" "part 3" "part 4"; do
  dir="$REPORT/$part"
  [[ -d "$dir" ]] || continue
  for name in report.md explain.md; do
    [[ -f "$dir/$name" ]] || continue
    export_one "$dir/$name"
  done
done

echo
echo "Done. PDFs:"
find "$REPORT" -name 'report.pdf' -o -name 'explain.pdf' | sort
