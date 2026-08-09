#!/usr/bin/env bash
# Download YOLOv8n (body, ONNX) + YuNet (face) models.
# OpenCV 5 builds often have no Caffe — do NOT rely on MobileNet-SSD .caffemodel.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/detection/models"
mkdir -p "$DIR"

YOLO="$DIR/yolov8n.onnx"
YUNET="$DIR/face_detection_yunet_2023mar.onnx"
HAAR="$DIR/haarcascade_frontalface_default.xml"

download() {
  local url="$1" out="$2"
  echo "→ $out"
  curl -fsSL -L --retry 3 --retry-delay 2 -o "$out" "$url"
}

if [[ ! -f "$YOLO" ]] || [[ "$(wc -c <"$YOLO" | tr -d ' ')" -lt 5000000 ]]; then
  echo "Downloading YOLOv8n ONNX (~12MB)..."
  rm -f "$YOLO"
  ok=0
  for url in \
    "https://github.com/CVHub520/X-AnyLabeling/releases/download/v0.1.0/yolov8n.onnx" \
    "https://github.com/ultralytics/assets/releases/download/v8.3.0/yolov8n.onnx"
  do
    echo "try: $url"
    if curl -fsSL -L --retry 3 --retry-delay 2 -o "$YOLO" "$url"; then
      local_size=$(wc -c <"$YOLO" | tr -d ' ')
      if [[ "$local_size" -ge 5000000 ]]; then
        ok=1
        echo "OK ($local_size bytes)"
        break
      fi
      echo "too small ($local_size) — next mirror"
      rm -f "$YOLO"
    fi
  done
  if [[ "$ok" -ne 1 ]]; then
    echo "WARN: YOLOv8n unavailable — body will use HOG" >&2
  fi
fi

if [[ ! -f "$YUNET" ]]; then
  echo "Downloading YuNet face model..."
  download \
    "https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx" \
    "$YUNET" \
  || download \
    "https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx" \
    "$YUNET"
fi

if [[ ! -f "$HAAR" ]]; then
  echo "Downloading Haar face cascade (fallback)..."
  download \
    "https://raw.githubusercontent.com/opencv/opencv/4.x/data/haarcascades/haarcascade_frontalface_default.xml" \
    "$HAAR" || true
fi

echo "---"
ls -lh "$YOLO" "$YUNET" "$HAAR" 2>/dev/null || true
echo "Models ready under $DIR"
