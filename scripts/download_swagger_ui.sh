#!/usr/bin/env bash
# Download Swagger UI assets locally (fixes blank /docs when CDNs are blocked).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/gateway/static"
mkdir -p "$DIR"

VER="5.17.14"
BASE="https://cdn.jsdelivr.net/npm/swagger-ui-dist@${VER}"

download() {
  local url="$1" out="$2"
  echo "→ $out"
  curl -fsSL -L --retry 3 --retry-delay 2 -o "$out" "$url"
}

download "$BASE/swagger-ui-bundle.js" "$DIR/swagger-ui-bundle.js"
download "$BASE/swagger-ui.css" "$DIR/swagger-ui.css"

# Optional small favicon placeholder skip
ls -lh "$DIR/swagger-ui-bundle.js" "$DIR/swagger-ui.css"
echo "OK — restart: sudo systemctl restart api_gateway"
