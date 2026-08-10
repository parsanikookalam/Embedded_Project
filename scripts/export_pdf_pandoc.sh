#!/usr/bin/env bash
# Export each part's report.md + explain.md → PDF (pandoc only).
#
# Install once:
#   sudo apt update
#   sudo apt install -y pandoc texlive-xetex texlive-fonts-recommended
#
# Run:
#   cd ~/embedded_project
#   bash scripts/export_pdf_pandoc.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPORT="$ROOT/report"

if ! command -v pandoc >/dev/null 2>&1; then
  echo "ERROR: pandoc not found. Install it first:"
  echo "  sudo apt update && sudo apt install -y pandoc texlive-xetex texlive-fonts-recommended"
  exit 1
fi

# Pick a PDF engine that exists
ENGINE=""
for e in xelatex pdflatex lualatex weasyprint wkhtmltopdf; do
  if command -v "$e" >/dev/null 2>&1; then
    ENGINE="$e"
    break
  fi
done
if [[ -z "$ENGINE" ]]; then
  echo "ERROR: no PDF engine found for pandoc."
  echo "  sudo apt install -y texlive-xetex texlive-fonts-recommended"
  exit 1
fi
echo "==> pandoc engine: $ENGINE"

common_opts=(
  --from=gfm
  --standalone
  --toc
  --toc-depth=2
  -V geometry:margin=20mm
  -V colorlinks=true
  -V linkcolor=blue
  -V urlcolor=blue
)

# LaTeX engines need a font that has the arrows / degree signs used in the docs
if [[ "$ENGINE" == "xelatex" || "$ENGINE" == "lualatex" ]]; then
  common_opts+=(-V mainfont="DejaVu Sans" -V monofont="DejaVu Sans Mono")
fi

ok=0
fail=0

for part in "part 1" "part 2" "part 3" "part 4"; do
  dir="$REPORT/$part"
  [[ -d "$dir" ]] || continue
  for name in report explain; do
    md="$dir/$name.md"
    pdf="$dir/$name.pdf"
    [[ -f "$md" ]] || continue

    echo "==> $md"
    if (cd "$dir" && pandoc "$name.md" \
          "${common_opts[@]}" \
          --pdf-engine="$ENGINE" \
          --resource-path=".:fig" \
          --metadata title="Smart Guard — $(tr '[:lower:]' '[:upper:]' <<<"${part:0:1}")${part:1} ${name}" \
          -o "$name.pdf"); then
      echo "    [ok] $pdf"
      ok=$((ok + 1))
    else
      echo "    [FAIL] $pdf"
      fail=$((fail + 1))
    fi
  done
done

echo
echo "==> Done: $ok ok, $fail failed"
find "$REPORT" -maxdepth 2 -name '*.pdf' -printf '%p  (%s bytes)\n' | sort
