#!/usr/bin/env bash
# Self-signed cert with CN = STUDENT_ID (+ SAN for localhost / 127.0.0.1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

STUDENT_ID="$(grep -E '^STUDENT_ID=' "$ROOT/config.env" | head -n1 | cut -d= -f2- | tr -d '"' | tr -d "'" | tr -d '\r')"
[[ -n "$STUDENT_ID" ]] || { echo "STUDENT_ID missing"; exit 1; }

OUT="$ROOT/web/www"
mkdir -p "$OUT"
CFG="$(mktemp)"
cat >"$CFG" <<EOF
[req]
default_bits = 2048
prompt = no
default_md = sha256
distinguished_name = dn
x509_extensions = v3_req

[dn]
CN = ${STUDENT_ID}
O = Smart Guard System
C = IR

[v3_req]
subjectAltName = @alt_names
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[alt_names]
DNS.1 = localhost
DNS.2 = ${STUDENT_ID}
IP.1 = 127.0.0.1
EOF

openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout "$OUT/server.key" \
  -out "$OUT/server.crt" \
  -config "$CFG"
rm -f "$CFG"
chmod 600 "$OUT/server.key"

echo "Generated $OUT/server.crt"
openssl x509 -in "$OUT/server.crt" -noout -subject
openssl x509 -in "$OUT/server.crt" -noout -ext subjectAltName 2>/dev/null || true
