"""
Smart Guard vision worker (Python — image processing only).

Camera policy:
  - OFF by default
  - Explicit on/off via C API/dashboard (`data/camera_state.json`)
  - Webpage stream does NOT start the camera; it only displays frames
  - When OFF, webcam is released (LED off) and idle placeholder is shown

Detector: YOLOv8n ONNX (body) + YuNet/Haar (face).
Counting: face inside a body box = same person (no double-count).
Part 3A overlay: student ID, live datetime, person count, real FPS, boxes.
Note: OpenCV 5 often has no Caffe — we use ONNX only for DNN.
"""

from __future__ import annotations

import asyncio
import json
import os
import sqlite3
import threading
import time
from collections import deque
from datetime import datetime
from typing import AsyncIterator, List, Optional, Tuple

import cv2
import numpy as np
import uvicorn
from dotenv import load_dotenv
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse

Box = Tuple[int, int, int, int]
# (box, kind) kind is "body" or "face"
Det = Tuple[Box, str]

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
DET_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA_DIR = os.path.join(BASE_DIR, "data")
MODELS_DIR = os.path.join(DET_DIR, "models")
PERSONS_JSON = os.path.join(DATA_DIR, "persons.json")
HISTORY_DB = os.path.join(DATA_DIR, "history.db")
HEARTBEAT_JSON = os.path.join(DATA_DIR, "vision_heartbeat.json")
THERMAL_CTRL_JSON = os.path.join(DATA_DIR, "thermal_control.json")
CAMERA_STATE_JSON = os.path.join(DATA_DIR, "camera_state.json")
DETECTION_STATE_JSON = os.path.join(DATA_DIR, "detection_state.json")
VISION_CONTROL_JSON = os.path.join(DATA_DIR, "vision_control.json")
BLACKBOX_CAPACITY = 500
os.makedirs(DATA_DIR, exist_ok=True)

# In-memory flag (file can lag / stream can look “stuck” if only file is used)
_camera_enabled = False
_camera_lock = threading.Lock()
_detection_enabled = True
_detection_lock = threading.Lock()

load_dotenv(os.path.join(BASE_DIR, "config.env"))
STUDENT_ID = os.getenv("STUDENT_ID", "402102657").strip('"').strip("'") or "402102657"
CAMERA_INDEX = int(os.getenv("CAMERA_INDEX", "0"))
DETECTOR_PORT = int(os.getenv("DETECTOR_PORT", "5000"))
TARGET = os.getenv("TARGET", "wsl").strip().lower()
DNN_CONF = float(os.getenv("DNN_CONF", "0.35"))
FACE_CONF = float(os.getenv("FACE_CONF", "0.55"))
YOLO_INPUT = int(os.getenv("YOLO_INPUT", "640"))
YOLO_NMS = float(os.getenv("YOLO_NMS", "0.45"))
# COCO class 0 = person
YOLO_PERSON_CLASS = 0
YOLO_NET_SIZE = 640
_yolo_sizes_ok: dict = {}

# MQTT is published by the C web_server (Part 3C) — not Python.

# --- Body detector (YOLOv8 ONNX preferred, HOG fallback) --------------------
BODY_KIND = "none"
yolo_net = None
hog = None

yolo_path = os.path.join(MODELS_DIR, "yolov8n.onnx")
if os.path.isfile(yolo_path) and hasattr(cv2.dnn, "readNetFromONNX"):
    try:
        yolo_net = cv2.dnn.readNetFromONNX(yolo_path)
        BODY_KIND = "yolo"
        print(f"OpenCV {cv2.__version__}: body=YOLOv8n-ONNX")
    except Exception as e:
        print(f"WARN: YOLOv8 ONNX load failed ({e})")
        yolo_net = None

if BODY_KIND == "none" and hasattr(cv2, "HOGDescriptor"):
    hog = cv2.HOGDescriptor()
    hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
    BODY_KIND = "hog"
    print(f"OpenCV {cv2.__version__}: body=HOG (run scripts/download_detector_models.sh for YOLO)")
elif BODY_KIND == "none":
    raise RuntimeError("No body detector available")

# --- Face detector (YuNet preferred, Haar fallback) ------------------------
FACE_KIND = "none"
face_yunet = None
face_haar = None

yunet_path = os.path.join(MODELS_DIR, "face_detection_yunet_2023mar.onnx")
if hasattr(cv2, "FaceDetectorYN") and os.path.isfile(yunet_path):
    try:
        face_yunet = cv2.FaceDetectorYN.create(
            yunet_path, "", (320, 320), FACE_CONF, 0.3, 5000
        )
        FACE_KIND = "yunet"
        print("face=YuNet")
    except TypeError:
        face_yunet = cv2.FaceDetectorYN.create(yunet_path, "", (320, 320), FACE_CONF, 0.3)
        FACE_KIND = "yunet"
        print("face=YuNet")
    except Exception as e:
        print(f"WARN: YuNet load failed ({e})")
        face_yunet = None

if FACE_KIND == "none":
    haar_names = (
        "haarcascade_frontalface_default.xml",
        "haarcascade_frontalface_alt2.xml",
    )
    candidates = []
    if hasattr(cv2, "data") and hasattr(cv2.data, "haarcascades"):
        for n in haar_names:
            candidates.append(os.path.join(cv2.data.haarcascades, n))
    for n in haar_names:
        candidates.append(os.path.join(MODELS_DIR, n))
    for path in candidates:
        if os.path.isfile(path):
            clf = cv2.CascadeClassifier(path)
            if not clf.empty():
                face_haar = clf
                FACE_KIND = "haar"
                print(f"face=Haar ({path})")
                break
    if FACE_KIND == "none":
        print("WARN: no face detector — body-only counting")


def _nms_boxes(boxes: List[Box], overlap_thresh: float = 0.4) -> List[Box]:
    if not boxes:
        return []
    rects = np.array([[x, y, x + w, y + h] for (x, y, w, h) in boxes], dtype=np.float32)
    x1, y1, x2, y2 = rects[:, 0], rects[:, 1], rects[:, 2], rects[:, 3]
    areas = (x2 - x1 + 1) * (y2 - y1 + 1)
    order = areas.argsort()[::-1]
    keep: List[int] = []
    while order.size > 0:
        i = int(order[0])
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1 + 1)
        h = np.maximum(0.0, yy2 - yy1 + 1)
        inter = w * h
        iou = inter / (areas[i] + areas[order[1:]] - inter + 1e-6)
        order = order[np.where(iou <= overlap_thresh)[0] + 1]
    return [
        (
            int(rects[i, 0]),
            int(rects[i, 1]),
            int(rects[i, 2] - rects[i, 0]),
            int(rects[i, 3] - rects[i, 1]),
        )
        for i in keep
    ]


def _letterbox_bgr(frame: np.ndarray, size: int) -> tuple:
    h0, w0 = frame.shape[:2]
    scale = min(size / h0, size / w0)
    nw, nh = int(round(w0 * scale)), int(round(h0 * scale))
    resized = cv2.resize(frame, (nw, nh), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), 114, dtype=np.uint8)
    pad_x = (size - nw) // 2
    pad_y = (size - nh) // 2
    canvas[pad_y : pad_y + nh, pad_x : pad_x + nw] = resized
    return canvas, scale, pad_x, pad_y


def _detect_bodies_yolo(frame: np.ndarray, input_size: Optional[int] = None) -> List[Box]:
    """YOLOv8 ONNX: output [1, 84, N] → person boxes (class 0)."""
    assert yolo_net is not None
    h0, w0 = frame.shape[:2]
    want = snap_yolo_input(int(input_size or YOLO_INPUT))

    # Shrink source when user picks 320/480 so lower res is cheaper / less accurate.
    proc = frame
    back_scale = 1.0
    if want < YOLO_NET_SIZE:
        s = want / float(max(h0, w0))
        if s < 1.0:
            proc = cv2.resize(frame, (max(1, int(w0 * s)), max(1, int(h0 * s))))
            back_scale = 1.0 / s

    net_size = YOLO_NET_SIZE
    used_dynamic = False
    if _yolo_sizes_ok.get(want, True) and want != YOLO_NET_SIZE:
        try:
            canvas, scale, pad_x, pad_y = _letterbox_bgr(proc, want)
            blob = cv2.dnn.blobFromImage(
                canvas, scalefactor=1 / 255.0, size=(want, want), swapRB=True, crop=False
            )
            yolo_net.setInput(blob)
            out = yolo_net.forward()
            net_size = want
            used_dynamic = True
            _yolo_sizes_ok[want] = True
        except Exception as exc:
            _yolo_sizes_ok[want] = False
            print(f"[yolo] input {want} unsupported ({exc}); shrink+{YOLO_NET_SIZE}")

    if not used_dynamic:
        canvas, scale, pad_x, pad_y = _letterbox_bgr(proc, net_size)
        blob = cv2.dnn.blobFromImage(
            canvas, scalefactor=1 / 255.0, size=(net_size, net_size), swapRB=True, crop=False
        )
        yolo_net.setInput(blob)
        out = yolo_net.forward()

    preds = np.squeeze(out)
    if preds.ndim != 2:
        return []
    if preds.shape[0] < preds.shape[1] and preds.shape[0] <= 128:
        preds = preds.T  # → (N, 84)

    cls_scores = preds[:, 4:]
    cls_ids = np.argmax(cls_scores, axis=1)
    confs = cls_scores[np.arange(cls_scores.shape[0]), cls_ids]
    keep = (cls_ids == YOLO_PERSON_CLASS) & (confs >= DNN_CONF)
    if not np.any(keep):
        return []

    sel = preds[keep]
    confidences = confs[keep].astype(float).tolist()
    boxes_xywh: List[List[float]] = []
    for row in sel:
        cx, cy, bw, bh = map(float, row[:4])
        x = (cx - bw / 2.0 - pad_x) / scale * back_scale
        y = (cy - bh / 2.0 - pad_y) / scale * back_scale
        w = bw / scale * back_scale
        h = bh / scale * back_scale
        boxes_xywh.append([x, y, w, h])

    indices = cv2.dnn.NMSBoxes(boxes_xywh, confidences, DNN_CONF, YOLO_NMS)
    if indices is None or len(indices) == 0:
        return []
    idxs = np.array(indices).reshape(-1)

    result: List[Box] = []
    for i in idxs:
        x, y, w, h = boxes_xywh[int(i)]
        x1 = max(0, int(round(x)))
        y1 = max(0, int(round(y)))
        x2 = min(w0 - 1, int(round(x + w)))
        y2 = min(h0 - 1, int(round(y + h)))
        bw, bh = x2 - x1, y2 - y1
        if bw >= 18 and bh >= 36:
            result.append((x1, y1, bw, bh))
    return _nms_boxes(result, overlap_thresh=0.45)


def detect_bodies(frame: np.ndarray, input_size: Optional[int] = None) -> List[Box]:
    if BODY_KIND == "yolo" and yolo_net is not None:
        return _detect_bodies_yolo(frame, input_size=input_size)

    gray = cv2.equalizeHist(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY))
    boxes, weights = hog.detectMultiScale(
        gray, winStride=(4, 4), padding=(8, 8), scale=1.03, hitThreshold=0.0
    )
    out: List[Box] = []
    for i, (x, y, bw, bh) in enumerate(boxes):
        score = float(weights[i]) if len(weights) > i else 0.0
        if score < 0.15:
            continue
        aspect = bh / float(max(bw, 1))
        if 1.15 <= aspect <= 5.5 and bh >= 70:
            out.append((int(x), int(y), int(bw), int(bh)))
    return _nms_boxes(out)


def detect_faces(frame: np.ndarray) -> List[Box]:
    h, w = frame.shape[:2]
    boxes: List[Box] = []

    if FACE_KIND == "yunet" and face_yunet is not None:
        face_yunet.setInputSize((w, h))
        _, faces = face_yunet.detect(frame)
        if faces is not None:
            for row in faces:
                # x, y, w, h, ..., score
                x, y, fw, fh = int(row[0]), int(row[1]), int(row[2]), int(row[3])
                score = float(row[-1]) if len(row) > 4 else 1.0
                if score < FACE_CONF:
                    continue
                if fw < 24 or fh < 24:
                    continue
                boxes.append((max(0, x), max(0, y), fw, fh))
        return _nms_boxes(boxes, overlap_thresh=0.3)

    if FACE_KIND == "haar" and face_haar is not None:
        gray = cv2.equalizeHist(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY))
        found = face_haar.detectMultiScale(
            gray, scaleFactor=1.08, minNeighbors=4, minSize=(32, 32)
        )
        for (x, y, fw, fh) in found:
            boxes.append((int(x), int(y), int(fw), int(fh)))
        return _nms_boxes(boxes, overlap_thresh=0.3)

    return []


def face_belongs_to_body(face: Box, body: Box) -> bool:
    """True if this face is the head of that body → do NOT count twice."""
    fx, fy, fw, fh = face
    bx, by, bw, bh = body
    cx, cy = fx + fw // 2, fy + fh // 2
    # Head sits in the upper ~75% of a person box (loose — avoids double-count)
    top = by - int(bh * 0.08)
    bot = by + int(bh * 0.75)
    left, right = bx - int(bw * 0.12), bx + bw + int(bw * 0.12)
    if not (left <= cx <= right and top <= cy <= bot):
        return False
    if fw * fh > bw * bh * 0.60:
        return False
    return True


def merge_unique_persons(bodies: List[Box], faces: List[Box]) -> List[Det]:
    """
    Unique people = all bodies
                 + faces that are NOT already covered by a body.
    """
    dets: List[Det] = [(b, "body") for b in bodies]
    for face in faces:
        if any(face_belongs_to_body(face, b) for b in bodies):
            continue
        dets.append((face, "face"))
    return dets


def detect_people(frame: np.ndarray, input_size: Optional[int] = None) -> List[Det]:
    bodies = detect_bodies(frame, input_size=input_size)
    faces = detect_faces(frame)
    return merge_unique_persons(bodies, faces)


# --- Shared state -----------------------------------------------------------
def init_history_db() -> None:
    conn = sqlite3.connect(HISTORY_DB)
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS detections (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            count INTEGER NOT NULL,
            timestamp INTEGER NOT NULL
        )
        """
    )
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value INTEGER NOT NULL
        )
        """
    )
    conn.execute(
        "INSERT OR IGNORE INTO meta(key, value) VALUES('total_human_events', 0)"
    )
    conn.commit()
    conn.close()


def write_persons_snapshot(count: int, ts: int) -> None:
    payload = {"count": int(count), "timestamp": int(ts)}
    tmp = PERSONS_JSON + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(payload, f)
    os.replace(tmp, PERSONS_JSON)


def append_history(count: int, ts: int, *, bump_total: bool = False) -> None:
    """Part 4.2 black-box: circular buffer + total human-event counter."""
    conn = sqlite3.connect(HISTORY_DB)
    try:
        conn.execute(
            "INSERT INTO detections (count, timestamp) VALUES (?, ?)",
            (int(count), int(ts)),
        )
        if bump_total and int(count) > 0:
            conn.execute(
                "UPDATE meta SET value = value + 1 WHERE key='total_human_events'"
            )
        conn.execute(
            f"""
            DELETE FROM detections WHERE id NOT IN (
                SELECT id FROM detections ORDER BY id DESC LIMIT {BLACKBOX_CAPACITY}
            )
            """
        )
        conn.commit()
    finally:
        conn.close()


def write_heartbeat(mode: str, *, touch_ts: bool = True) -> None:
    """touch_ts=False keeps previous timestamp (watchdog can detect stalled frames)."""
    ts = int(time.time())
    if not touch_ts:
        try:
            with open(HEARTBEAT_JSON, "r", encoding="utf-8") as f:
                prev = json.load(f)
            if isinstance(prev.get("ts"), (int, float)) and int(prev["ts"]) > 0:
                ts = int(prev["ts"])
        except Exception:
            pass
    payload = {"ts": ts, "mode": mode}
    tmp = HEARTBEAT_JSON + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(payload, f)
    os.replace(tmp, HEARTBEAT_JSON)


def read_thermal_control() -> dict:
    try:
        with open(THERMAL_CTRL_JSON, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {
            "throttle_level": 0,
            "yolo_input": YOLO_INPUT,
            "detect_every": 1,
            "frame_sleep_ms": 0,
        }


def snap_yolo_input(size: int) -> int:
    s = int(size or YOLO_INPUT)
    if s <= 160:
        return 160
    if s <= 256:
        return 256
    if s <= 320:
        return 320
    if s <= 480:
        return 480
    return 640


def vision_detect_stride(yolo_in: int) -> int:
    """
    Our yolov8n.onnx is exported at 640×640. Smaller UI resolutions shrink the
    frame (accuracy↓) and also run YOLO less often so FPS↑ is visible.
    """
    if yolo_in <= 160:
        return 4
    if yolo_in <= 256:
        return 3
    if yolo_in <= 320:
        return 2
    if yolo_in <= 480:
        return 1
    return 1


def read_vision_control() -> dict:
    """Part 3-3: dynamic YOLO size + target FPS from C API / dashboard cmds."""
    out = {"yolo_input": YOLO_INPUT, "target_fps": 24}
    try:
        with open(VISION_CONTROL_JSON, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            if data.get("yolo_input") is not None:
                out["yolo_input"] = snap_yolo_input(int(data["yolo_input"]))
            if data.get("target_fps") is not None:
                out["target_fps"] = max(1, min(60, int(data["target_fps"])))
    except Exception:
        pass
    return out


def _parse_enabled(data: dict) -> bool:
    v = data.get("enabled", False)
    if isinstance(v, str):
        return v.strip().lower() in ("1", "true", "yes", "on")
    return bool(v)


def load_camera_state_from_file() -> bool:
    """Read data/camera_state.json written by C web_server."""
    try:
        with open(CAMERA_STATE_JSON, "r", encoding="utf-8") as f:
            data = json.load(f)
        return _parse_enabled(data)
    except Exception:
        return False


def load_detection_state_from_file() -> bool:
    """Read data/detection_state.json — default ON if missing."""
    try:
        with open(DETECTION_STATE_JSON, "r", encoding="utf-8") as f:
            data = json.load(f)
        return _parse_enabled(data)
    except Exception:
        return True


def set_camera_enabled(enabled: bool, *, persist: bool = True) -> bool:
    global _camera_enabled
    val = bool(enabled)
    with _camera_lock:
        prev = _camera_enabled
        _camera_enabled = val
    if persist:
        try:
            tmp = CAMERA_STATE_JSON + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump({"enabled": 1 if val else 0}, f)
                f.write("\n")
            os.replace(tmp, CAMERA_STATE_JSON)
        except Exception as exc:
            print(f"[cam] failed to persist state: {exc}")
    if prev != val:
        print(f"[cam] state → {'ON' if val else 'OFF'} (path={CAMERA_STATE_JSON})")
    return val


def camera_enabled() -> bool:
    """Always prefer the shared file (C writes; Python re-reads)."""
    val = load_camera_state_from_file()
    with _camera_lock:
        _camera_enabled = val
        return _camera_enabled


def detection_enabled() -> bool:
    """When False, skip YOLO/face entirely (stream may still show raw camera)."""
    global _detection_enabled
    val = load_detection_state_from_file()
    with _detection_lock:
        _detection_enabled = val
        return _detection_enabled


def open_local_device(index: int = 0) -> Optional[cv2.VideoCapture]:
    """Open only the configured index (and one neighbor). Do not scan 0..2 —
    opening /dev/video1 metadata nodes then releasing causes rapid LED blink."""
    candidates = []
    for idx in (index, index + 1 if index == 0 else 0):
        if idx not in candidates:
            candidates.append(idx)

    for idx in candidates:
        path = f"/dev/video{idx}"
        if not os.path.exists(path):
            continue
        cap = None
        try:
            cap = cv2.VideoCapture(idx, cv2.CAP_V4L2)
            if not cap.isOpened():
                cap.release()
                cap = cv2.VideoCapture(idx)
            if not cap.isOpened():
                continue
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            cap.set(cv2.CAP_PROP_FPS, 24)
            try:
                cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc("M", "J", "P", "G"))
            except Exception:
                pass
            ok, frame = cap.read()
            if ok and frame is not None:
                print(f"[cam] opened {path}")
                return cap
        except Exception as exc:
            print(f"[cam] open {path} failed: {exc}")
        if cap is not None:
            try:
                cap.release()
            except Exception:
                pass
        time.sleep(0.3)  # let USB settle before next try
    return None


init_history_db()
write_persons_snapshot(0, int(time.time()))
load_camera_state_from_file()
load_detection_state_from_file()
print(f"[cam] initial enabled={camera_enabled()} file={CAMERA_STATE_JSON}")
print(f"[det] initial enabled={detection_enabled()} file={DETECTION_STATE_JSON}")

app = FastAPI(title="Smart Guard Detector", docs_url=None, redoc_url=None)

output_frame: Optional[np.ndarray] = None
frame_lock = threading.Lock()

# Active MJPEG clients (dashboard tabs / stream consumers)
viewers = 0
viewers_lock = threading.Lock()
last_chunk_sent_at = 0.0

last_recorded_count = None
last_history_ts = 0
_last_logged_vision = None


def add_viewer() -> int:
    global viewers, last_chunk_sent_at
    with viewers_lock:
        viewers += 1
        n = viewers
        last_chunk_sent_at = time.time()  # start grace so camera can open immediately
    print(f"[cam] viewer+ → {n}")
    return n


def remove_viewer() -> int:
    global viewers
    with viewers_lock:
        viewers = max(0, viewers - 1)
        n = viewers
    print(f"[cam] viewer- → {n}")
    return n


def viewer_count() -> int:
    with viewers_lock:
        return viewers


def make_idle_frame(msg: str) -> np.ndarray:
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    frame[:] = (28, 28, 32)
    cv2.putText(frame, msg, (30, 230), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 200, 255), 2)
    cv2.putText(
        frame,
        datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        (30, 270),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (160, 160, 160),
        1,
    )
    return frame


def release_cap(cap: Optional[cv2.VideoCapture]) -> None:
    if cap is None:
        return
    try:
        cap.release()
    except Exception:
        pass
    print("[cam] released /dev/video*")


def detect_humans() -> None:
    """Open webcam only while camera_state.json enabled=1 (API/dashboard)."""
    global output_frame, last_recorded_count, last_history_ts

    cap: Optional[cv2.VideoCapture] = None
    count_buffer: deque = deque(maxlen=9)
    stable = 0
    fps = 0.0
    frame_counter = 0
    fps_timer = time.time()
    was_enabled = False
    last_open_try = 0.0
    last_file_check = 0.0
    enabled_cached = False
    off_streak = 0
    OPEN_COOLDOWN_SEC = 2.0
    # Only release after MANY consecutive camera_off file reads (user pressed OFF).
    OFF_CONFIRM_READS = 6
    frame_i = 0
    last_dets: list = []
    read_fail_streak = 0
    # Recycle dead V4L2 handle after usbipd detach (Camera stays ON)
    READ_FAILS_BEFORE_REOPEN = 8

    print("[cam] service up — ON/OFF only via API. Dead /dev/video* is reopened automatically.")

    while True:
        try:
            now = time.time()
            ts = int(now)

            if now - last_file_check >= 0.5:
                raw_on = camera_enabled()
                last_file_check = now
                if raw_on:
                    off_streak = 0
                    enabled_cached = True
                else:
                    off_streak += 1
                    if off_streak >= OFF_CONFIRM_READS:
                        enabled_cached = False
            enabled = enabled_cached

            if not enabled:
                if cap is not None:
                    release_cap(cap)
                    cap = None
                    count_buffer.clear()
                    write_persons_snapshot(0, ts)
                if was_enabled:
                    print("[cam] OFF — user camera_off only")
                was_enabled = False
                write_heartbeat("idle")
                idle = make_idle_frame("Camera OFF — press Camera ON or POST camera_on")
                with frame_lock:
                    output_frame = idle
                time.sleep(0.25)
                continue

            if not was_enabled:
                print("[cam] ON — opening webcam (stays on until camera_off)")
                was_enabled = True
                # Intent to capture — if no real frame arrives, ts ages → watchdog email
                write_heartbeat("capturing", touch_ts=True)

            if cap is None:
                # Do NOT refresh heartbeat ts while stuck opening (watchdog must fire)
                write_heartbeat("capturing", touch_ts=False)
                if now - last_open_try < OPEN_COOLDOWN_SEC:
                    with frame_lock:
                        output_frame = make_idle_frame("Starting camera...")
                    time.sleep(0.25)
                    continue
                last_open_try = now
                cap = open_local_device(CAMERA_INDEX)
                if cap is None:
                    write_heartbeat("capturing", touch_ts=False)
                    with frame_lock:
                        output_frame = make_idle_frame("Waiting for usbipd camera...")
                    write_persons_snapshot(0, ts)
                    time.sleep(1.0)
                    continue

            ret, frame = cap.read()
            if not ret or frame is None:
                read_fail_streak += 1
                print(
                    f"[cam] read failed ({read_fail_streak}/{READ_FAILS_BEFORE_REOPEN}) "
                    "— heartbeat not refreshed"
                )
                write_heartbeat("capturing", touch_ts=False)
                with frame_lock:
                    output_frame = make_idle_frame(
                        "Camera ON — no frame (will reopen /dev/video*)"
                    )
                # usbipd detach leaves a dead handle — drop it and reopen when device returns
                if read_fail_streak >= READ_FAILS_BEFORE_REOPEN:
                    print("[cam] recycling VideoCapture (usbipd reattach path)")
                    release_cap(cap)
                    cap = None
                    read_fail_streak = 0
                    last_open_try = 0.0  # allow immediate reopen attempt
                time.sleep(0.25)
                continue
            read_fail_streak = 0

            loop_t0 = time.time()
            thermal = read_thermal_control()
            vision = read_vision_control()
            thr_lvl = int(thermal.get("throttle_level") or 0)
            yolo_in = snap_yolo_input(int(vision.get("yolo_input") or YOLO_INPUT))
            target_fps = float(vision.get("target_fps") or 24)
            # Resolution stride makes lower sizes actually faster (ONNX is fixed 640).
            detect_every = vision_detect_stride(yolo_in)
            if thr_lvl > 0:
                yolo_in = snap_yolo_input(int(thermal.get("yolo_input") or yolo_in))
                detect_every = max(detect_every, int(thermal.get("detect_every") or 1))

            global _last_logged_vision
            vkey = (yolo_in, int(target_fps), detect_every, thr_lvl)
            if vkey != _last_logged_vision:
                print(
                    f"[vision] file={VISION_CONTROL_JSON} yolo_input={yolo_in} "
                    f"stride={detect_every} target_fps={target_fps} thermal={thr_lvl}"
                )
                _last_logged_vision = vkey

            # Fresh frame arrived — this is what resets the watchdog timer
            write_heartbeat("capturing", touch_ts=True)

            det_on = detection_enabled()
            detect_ms = 0.0
            if not det_on:
                last_dets = []
                stable = 0
                count_buffer.clear()
                thr_tag = " | DET OFF"
                dets = []
            else:
                run_detect = (frame_i % detect_every) == 0
                frame_i += 1
                if run_detect:
                    t0 = time.time()
                    try:
                        last_dets = detect_people(frame, input_size=yolo_in)
                    except Exception as det_exc:
                        print(f"[det] frame error (kept stream): {det_exc}")
                        last_dets = last_dets if last_dets else []
                    detect_ms = (time.time() - t0) * 1000.0
                    count_buffer.append(len(last_dets))
                    stable = sorted(count_buffer)[len(count_buffer) // 2]
                thr_tag = ""
                if thr_lvl:
                    thr_tag += f" | THR{thr_lvl}"
                thr_tag += f" | in={yolo_in} stride={detect_every} capFps={target_fps:.0f}"
                dets = last_dets

            for (box, kind) in dets:
                x, y, w, h = box
                color = (0, 230, 118) if kind == "body" else (255, 220, 0)
                cv2.rectangle(frame, (x, y), (x + w, y + h), color, 2)
                cv2.putText(
                    frame,
                    kind,
                    (x, max(15, y - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.45,
                    color,
                    1,
                )

            frame_counter += 1
            now = time.time()
            if now - fps_timer >= 1.0:
                fps = frame_counter / (now - fps_timer)
                frame_counter = 0
                fps_timer = now

            stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            bar = np.zeros((78, frame.shape[1], 3), dtype=np.uint8)
            bar[:] = (20, 20, 24)
            frame[0:78, :] = cv2.addWeighted(frame[0:78, :], 0.35, bar, 0.65, 0)
            cv2.putText(
                frame,
                f"Smart Guard | ID: {STUDENT_ID}",
                (10, 24),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.65,
                (0, 255, 0),
                2,
            )
            cv2.putText(
                frame,
                f"{stamp}  |  Persons: {stable}  |  FPS: {fps:.1f}  |  det {detect_ms:.0f}ms{thr_tag}",
                (10, 52),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 255, 0),
                2,
            )
            body_face = f"{BODY_KIND}+{FACE_KIND}" if det_on else "OFF (no AI)"
            cv2.putText(
                frame,
                f"detector: {body_face}  pipe={yolo_in if det_on else 0} stride={detect_every if det_on else 0}",
                (10, 72),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.45,
                (180, 180, 180),
                1,
            )

            write_persons_snapshot(stable, ts)
            if (
                last_recorded_count is None
                or stable != last_recorded_count
                or (stable > 0 and (ts - last_history_ts) >= 2)
            ):
                bump = stable > 0 and (
                    last_recorded_count is None
                    or last_recorded_count == 0
                    or stable != last_recorded_count
                )
                append_history(stable, ts, bump_total=bump)
                last_recorded_count = stable
                last_history_ts = ts

            try:
                snap_path = os.path.join(DATA_DIR, "latest_detection.jpg")
                cv2.imwrite(snap_path, frame, [int(cv2.IMWRITE_JPEG_QUALITY), 85])
            except Exception:
                pass

            # Always publish camera frame → stream stays smooth under thermal load
            with frame_lock:
                output_frame = frame.copy()

            # Dynamic FPS cap (Part 3) — paced after work, does not block MQTT/heartbeat logic long
            period = 1.0 / max(1.0, float(target_fps))
            spent = time.time() - loop_t0
            if spent < period:
                time.sleep(period - spent)
        except Exception as exc:
            print(f"[cam] loop error (recovering, camera kept if ON): {exc}")
            # Keep old frame timestamp so watchdog can still fire
            write_heartbeat("capturing" if enabled_cached else "idle", touch_ts=False)
            time.sleep(0.5)


async def mjpeg_generator(request: Request) -> AsyncIterator[bytes]:
    """Async MJPEG — reliably drops viewer count when the browser closes."""
    add_viewer()
    global last_chunk_sent_at
    try:
        while True:
            if await request.is_disconnected():
                break
            with frame_lock:
                frame = None if output_frame is None else output_frame.copy()
            if frame is None:
                await asyncio.sleep(0.05)
                continue
            ok, encoded = await asyncio.to_thread(
                cv2.imencode, ".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 80]
            )
            if not ok:
                await asyncio.sleep(0.03)
                continue
            last_chunk_sent_at = time.time()
            yield (
                b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
                + bytearray(encoded)
                + b"\r\n"
            )
            await asyncio.sleep(0.04)
    finally:
        remove_viewer()


@app.get("/video_feed")
async def video_feed(request: Request):
    return StreamingResponse(
        mjpeg_generator(request),
        media_type="multipart/x-mixed-replace; boundary=frame",
    )


@app.get("/camera")
async def get_camera():
    return {"enabled": camera_enabled(), "file": CAMERA_STATE_JSON}


@app.post("/camera")
async def post_camera(request: Request):
    try:
        data = await request.json()
    except Exception:
        return {"error": "invalid_json", "enabled": camera_enabled()}
    if not isinstance(data, dict) or "enabled" not in data:
        return {"error": "missing_enabled", "enabled": camera_enabled()}
    set_camera_enabled(_parse_enabled(data), persist=True)
    return {"enabled": camera_enabled()}


@app.get("/health")
async def health():
    return {
        "ok": True,
        "body": BODY_KIND,
        "face": FACE_KIND,
        "camera_enabled": camera_enabled(),
        "camera_file": CAMERA_STATE_JSON,
        "viewers": viewer_count(),
        "camera_policy": "api_camera_on_off",
        "count_rule": "face_inside_body_not_double_counted",
        "target": TARGET,
    }


if __name__ == "__main__":
    threading.Thread(target=detect_humans, daemon=True).start()
    uvicorn.run(app, host="0.0.0.0", port=DETECTOR_PORT, log_level="info")
