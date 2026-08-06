#!/usr/bin/env bash
# Rebuild + restart + verify Part 1–2 on WSL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1090
source <(grep -E '^(HTTP_PORT|HTTPS_PORT|GATEWAY_PORT)=' "$ROOT/config.env" | sed 's/\r$//')
HTTP_PORT="${HTTP_PORT:-8080}"
HTTPS_PORT="${HTTPS_PORT:-8443}"
GATEWAY_PORT="${GATEWAY_PORT:-8000}"
REPORT="$ROOT/data/verify_part12.txt"
mkdir -p "$ROOT/data"

{
  echo "=== Part 1–2 verify ($(date -Is)) ==="
  echo "HTTPS_PORT=$HTTPS_PORT TARGET=wsl CAMERA=usbipd/device"
} >"$REPORT"

echo "[1/7] detector models (MobileNet-SSD)"
bash "$ROOT/scripts/download_detector_models.sh" | tee -a "$REPORT" || true

echo "[2/7] venv"; bash "$ROOT/scripts/setup_venv.sh" | tee -a "$REPORT"
echo "[3/7] TLS"; bash "$ROOT/scripts/gen_ssl.sh" | tee -a "$REPORT"
echo "[4/7] build"; make -C "$ROOT/web" clean all | tee -a "$REPORT"
echo "[5/7] data"; bash "$ROOT/scripts/init_data.sh" | tee -a "$REPORT"

echo "[6/7] services"
sudo cp "$ROOT/services/"*.service /etc/systemd/system/
sudo chown -R parsa:parsa "$ROOT/data" "$ROOT/web/www" || true
sudo chmod 600 "$ROOT/web/www/server.key" || true
sudo usermod -aG video parsa || true
sudo systemctl daemon-reload
sudo systemctl enable human_detector web_server api_gateway
sudo systemctl restart human_detector.service
sleep 1
sudo systemctl restart web_server.service
sleep 1
sudo systemctl restart api_gateway.service
sleep 3

echo "[7/7] checks"
{
  echo "--- systemd ---"; systemctl is-active human_detector web_server api_gateway || true
  echo "--- video ---"; ls -l /dev/video* 2>/dev/null || echo "no /dev/video* (attach usbipd)"
  echo "--- listen ---"; ss -lntp 2>/dev/null | grep -E ":${HTTP_PORT}|:${HTTPS_PORT}|:5000|:${GATEWAY_PORT}" || true
  echo "--- redirect ---"; curl -sI "http://127.0.0.1:${HTTP_PORT}/" | head -n 4 || true
  echo "--- telemetry ---"; curl -sk "https://127.0.0.1:${HTTPS_PORT}/api/v1/telemetry"; echo
  echo "--- persons ---"; curl -sk "https://127.0.0.1:${HTTPS_PORT}/api/v1/persons"; echo
  echo "--- history ---"; curl -sk "https://127.0.0.1:${HTTPS_PORT}/api/v1/history"; echo
  echo "--- config ---"; curl -sk "https://127.0.0.1:${HTTPS_PORT}/api/v1/config"; echo
  echo "--- command ---"; curl -sk -X POST "https://127.0.0.1:${HTTPS_PORT}/api/v1/command" \
    -H 'Content-Type: application/json' -d '{"cmd":"reboot"}'; echo
  echo "--- stream HEAD ---"; curl -skI "https://127.0.0.1:${HTTPS_PORT}/api/v1/stream" | head -n 6 || true
  echo "--- swagger ---"; curl -s -o /dev/null -w "swagger=%{http_code}\n" "http://127.0.0.1:${GATEWAY_PORT}/docs"
  echo "--- cert ---"; openssl x509 -in "$ROOT/web/www/server.crt" -noout -subject
  echo "--- detector ---"; journalctl -u human_detector -n 15 --no-pager || true
} | tee -a "$REPORT"

echo
echo "Report: $REPORT"
echo "Dashboard: https://127.0.0.1:${HTTPS_PORT}/"
echo "Swagger:   http://127.0.0.1:${GATEWAY_PORT}/docs"
