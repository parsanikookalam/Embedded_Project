#!/usr/bin/env bash
# Project venv + deps. On Python 3.14 use opencv-contrib (HOG lives there).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV="$ROOT/.venv"

sudo apt-get install -y python3-venv python3-full >/dev/null || true
sudo apt-get install -y python3.12-venv python3.12 2>/dev/null || true

PY=python3
for c in python3.12 python3.11 python3; do
  command -v "$c" >/dev/null 2>&1 && { PY=$c; break; }
done
echo "Using interpreter: $PY ($($PY --version 2>&1))"

if [[ -d "$VENV" ]]; then
  CUR="$("$VENV/bin/python" -c 'import sys; print("%d.%d"%sys.version_info[:2])' 2>/dev/null || echo none)"
  WANT="$($PY -c 'import sys; print("%d.%d"%sys.version_info[:2])')"
  [[ "$CUR" == "$WANT" ]] || { echo "Recreating venv $CUR -> $WANT"; rm -rf "$VENV"; }
fi
[[ -d "$VENV" ]] || "$PY" -m venv "$VENV"

"$VENV/bin/pip" install --upgrade pip
"$VENV/bin/pip" uninstall -y opencv-python opencv-python-headless \
  opencv-contrib-python opencv-contrib-python-headless 2>/dev/null || true

"$VENV/bin/pip" install \
  "python-dotenv>=1.0.0" "paho-mqtt>=2.0.0" "fastapi>=0.110.0" \
  "uvicorn[standard]>=0.27.0" "numpy>=1.24.0" "httpx>=0.27.0" "pydantic>=2.0.0"

PY_MM="$("$VENV/bin/python" -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
if [[ "$PY_MM" == "3.14" ]]; then
  echo "Python 3.14: opencv-contrib-python-headless (HOG/Haar)"
  "$VENV/bin/pip" install "opencv-contrib-python-headless>=5.0.0"
else
  "$VENV/bin/pip" install "opencv-python-headless>=4.8.0,<5"
fi

"$VENV/bin/python" - <<'PY'
import cv2
print("OpenCV", cv2.__version__)
print("HOGDescriptor:", hasattr(cv2, "HOGDescriptor"))
assert hasattr(cv2, "HOGDescriptor") or hasattr(cv2, "CascadeClassifier")
print("detector APIs ok")
PY
