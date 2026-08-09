# Smart Guard System

**Course:** Final Project — Embedded Systems  
**Student:** Parsa Nikookalam · **ID:** `402102657`  
**Platform:** WSL Ubuntu Linux (accepted Linux / VM path; same design as Orange Pi)  
**Webcam:** Windows laptop camera via **usbipd** → `/dev/video0` in WSL  

Live camera → human detection → report via **web / email / MQTT**, with Guard mode, black box, software watchdog, and adaptive thermal control. **Core application logic is in C**; Python is used for vision (+ a thin FastAPI/Swagger gateway).

---

## Table of contents

1. [Architecture](#architecture)  
2. [Repository layout](#repository-layout)  
3. [Parts overview (1–4)](#parts-overview-1--4)  
4. [Ports & services](#ports--services)  
5. [Quick start](#quick-start)  
6. [Windows helpers (camera + CPU temp)](#windows-helpers-camera--cpu-temp)  
7. [Configuration (`config.env`)](#configuration-configenv)  
8. [REST API reference](#rest-api-reference)  
9. [MQTT](#mqtt)  
10. [Email alerts](#email-alerts)  
11. [Dashboard controls](#dashboard-controls)  
12. [Reports](#reports)  
13. [Daily use / smoke checks](#daily-use--smoke-checks)  
14. [Troubleshooting](#troubleshooting)  
15. [Before submitting to TAs](#before-submitting-to-tas)  

---

## Architecture

```
Windows (host)                              WSL Ubuntu (systemd)
┌─────────────────────────────┐             ┌──────────────────────────────────┐
│ usbipd + logon task         │  webcam     │ human_detector :5000 (Python)    │
│   → attach BusId to WSL     ├────────────►│   YOLO + face, MJPEG, SQLite     │
│ HostCpuTemp loop            │  temp file  │ web_server :8080 → :8443 (C)     │
│   → cpu_temp.txt            ├────────────►│   HTTPS, REST, stream proxy,     │
└─────────────────────────────┘             │   email, MQTT, Part 4 logic      │
                                            │ api_gateway :8000/docs (Swagger) │
                                            │ mosquitto_smartguard :1883       │
                                            └──────────────────────────────────┘
                                                      ▲
Browser ──http://127.0.0.1:8080──301──► https://127.0.0.1:8443/
```

**Data flow (simplified):**

1. Detector captures frames, runs YOLO/face, writes `data/persons.json`, heartbeat, black-box SQLite, MJPEG on `:5000`.  
2. C `web_server` serves the dashboard, proxies `/api/v1/stream`, reads telemetry / persons, sends email & MQTT, runs Guard / watchdog / thermal.  
3. FastAPI gateway only **proxies** to C for Swagger UI (`:8000/docs`).  

On a physical Orange Pi the same design uses ports **80/443**; on WSL we use **8080/8443** because low ports are often blocked by Windows.

---

## Repository layout

```
embedded_project/
├── config.env                 # Local settings + secrets (do not publish passwords)
├── config.example.env         # Safe template
├── README.md                  # This file
├── web/                       # C HTTPS server + email + MQTT + Part 4
│   ├── Makefile
│   ├── src/                   # main, server, telemetry, email, mqtt, part4, …
│   ├── include/
│   └── www/                   # index.html, server.crt, server.key
├── detection/                 # Python vision (YOLO + face + MJPEG)
│   ├── requirements.txt
│   └── src/human_detector.py
├── gateway/                   # FastAPI Swagger proxy only
│   ├── requirements.txt
│   └── main.py
├── mqtt/                      # Mosquitto conf + password file
├── services/                  # systemd unit files
├── scripts/                   # setup, SSL, Mosquitto, Windows helpers
│   └── windows/               # usbipd camera + HostCpuTemp (keep for TAs)
├── data/                      # Runtime JSON / SQLite / snapshots (generated)
└── report/                    # Course experiment reports (parts 1–4)
    ├── part 1/report.md
    ├── part 2/report.md
    ├── part 3/report.md
    └── part 4/report.md
```

| Path | Role |
|------|------|
| `web/` | **C:** TLS web server, REST, stream proxy, email, MQTT, Guard/watchdog/thermal |
| `detection/` | **Python:** camera, YOLO+face, overlay, MJPEG, shared JSON/SQLite |
| `gateway/` | Thin Swagger UI proxy → C HTTPS |
| `services/` | `human_detector`, `web_server`, `api_gateway`, `mosquitto_smartguard` |
| `scripts/windows/` | TA-facing Windows automation for webcam + CPU temp |
| `report/` | Mandatory experiment write-ups |

---

## Parts overview (1–4)

| Part | Weight (course) | What this repo delivers |
|------|-----------------|-------------------------|
| **1** | Web + HTTPS | C server, HTML dashboard, self-signed cert **CN = student ID**, HTTP→HTTPS 301, systemd autostart |
| **2** | REST + telemetry | C APIs (`telemetry`, `persons`, `stream`, `command`, `history`), live dashboard widgets, Swagger gateway |
| **3** | Vision + email + MQTT | YOLO+face overlay, C SMTP alerts (debounce), C MQTT QoS1 + LWT, authenticated Mosquitto |
| **4** | Guard features | Guard alarms on count **increase**, SQLite black box, software watchdog (30 s), adaptive thermal (≥85 °C / &lt;78 °C) |

---

## Ports & services

| Service | Port | Unit |
|---------|------|------|
| HTTP → HTTPS redirect | `8080` | `web_server.service` |
| HTTPS dashboard + API | `8443` | `web_server.service` |
| Vision MJPEG (internal) | `5000` | `human_detector.service` |
| Swagger UI | `8000` | `api_gateway.service` |
| MQTT broker | `1883` | `mosquitto_smartguard.service` |

```bash
systemctl status human_detector web_server api_gateway mosquitto_smartguard --no-pager
```

---

## Quick start

### Dependencies (WSL)

```bash
sudo apt update
sudo apt install -y build-essential libssl-dev libsqlite3-dev libcurl4-openssl-dev \
  libmosquitto-dev openssl python3-pip python3-venv mosquitto mosquitto-clients sqlite3
```

### Clone / enter project

```bash
cd ~/embedded_project
cp -n config.example.env config.env   # if you do not already have config.env
# Edit config.env: STUDENT_ID, email SMTP_*, MQTT_*, ports
```

### Python venv + models

```bash
chmod +x scripts/*.sh
bash scripts/setup_venv.sh
bash scripts/download_detector_models.sh   # YOLO / YuNet ONNX (recommended)
bash scripts/init_data.sh
```

### TLS certificate (CN = student ID)

```bash
bash scripts/gen_ssl.sh
# Verify:
openssl x509 -in web/www/server.crt -noout -subject
# subject=CN = 402102657, …
```

### Build C server + install systemd

```bash
make -C web
bash scripts/setup_mosquitto.sh
bash scripts/enable_wsl_autostart.sh      # optional: WSL systemd / autostart notes
bash scripts/install_services.sh         # venv, build, units, enable
```

### Open the system

| URL | Purpose |
|-----|---------|
| http://127.0.0.1:8080/ | Redirects **301** → HTTPS |
| https://127.0.0.1:8443/ | Dashboard (accept self-signed warning) |
| http://127.0.0.1:8000/docs | Swagger |

Camera does **not** start just because you open the page — use **Camera ON** on the dashboard (or `POST` `camera_on`).

---

## Windows helpers (camera + CPU temp)

All under `scripts/windows/` (include these in the TA zip).

| File | Purpose |
|------|---------|
| `setup_usbipd_camera.ps1` | Bind webcam + create logon attach task |
| `attach_camera_to_wsl.ps1` | Attach BusId to WSL now |
| `camera.busid` | Saved BusId (e.g. `1-7`) |
| `host_cpu_temp.ps1` | Write host CPU °C to a file |
| `setup_host_temp_task.ps1` | Hidden logon loop (~every 2 s) |
| `run_hidden.vbs` | Run helper without a console flash |

### One-time (Admin PowerShell)

```powershell
cd \\wsl$\Ubuntu\home\parsa\embedded_project

# Camera — replace BusId with yours: usbipd list
powershell -ExecutionPolicy Bypass -File .\scripts\windows\setup_usbipd_camera.ps1 -BusId 1-7

# Host CPU temperature file for WSL telemetry
cd .\scripts\windows
powershell -ExecutionPolicy Bypass -File .\setup_host_temp_task.ps1
```

WSL reads temperature from (typical path):

`/mnt/c/Users/<YOU>/AppData/Local/SmartGuard/cpu_temp.txt`

CPU **usage** and **RAM** are read inside WSL by `web/src/telemetry.c` (`/proc/stat`, `sysinfo`).

---

## Configuration (`config.env`)

Use `config.example.env` as a clean template. Important keys:

| Key | Meaning |
|-----|---------|
| `STUDENT_ID` / `STUDENT_NAME` | Identity on cert, overlay, MQTT topics |
| `HTTP_PORT` / `HTTPS_PORT` | `8080` / `8443` on WSL |
| `CAMERA_INDEX` | OpenCV device index (usually `0`) |
| `YOLO_INPUT` / `DNN_CONF` / `FACE_CONF` | Detection tuning |
| `EMAIL_*` / `SMTP_*` | Part 3 email (Gmail App Password) |
| `MQTT_*` | Broker host/user/pass/interval |
| `THERMAL_TEMP_C` / `THERMAL_CLEAR_C` | Default `85` / `78` |
| `WATCHDOG_SEC` | Default `30` |

**Never commit real `SMTP_PASS` to a public repo.** Prefer placeholders in the zip you give TAs.

---

## REST API reference

Base URL: `https://127.0.0.1:8443` (use `curl -k` for the self-signed cert).  
Swagger mirrors the same paths via `http://127.0.0.1:8000/docs`.

### GET

| Path | Description |
|------|-------------|
| `/` | HTML dashboard |
| `/api/v1/stream` | MJPEG proxy from detector |
| `/api/v1/telemetry` | `cpu_temp`, `free_mem_kb`, `mem_used_percent`, `cpu_usage_percent` |
| `/api/v1/persons` | Current person count + timestamp |
| `/api/v1/history` | Last few detection records |
| `/api/v1/config` | Student name / ID (for UI) |
| `/api/v1/camera` | Camera enabled flag |
| `/api/v1/guard` | Guard armed flag |
| `/api/v1/watchdog` | Watchdog enabled |
| `/api/v1/thermal` | Thermal enabled |
| `/api/v1/part4` | Combined Part 4 snapshot |
| `/api/v1/blackbox` | Black-box stats / events |

### POST `/api/v1/command`

JSON body: `{"cmd":"<name>"}`

| `cmd` | Action |
|-------|--------|
| `reboot` | Soft reboot / restart path (WSL-safe behaviour) |
| `test_email` | Send a test alert email |
| `camera_on` / `camera_off` | Start / stop webcam capture |
| `guard_on` / `guard_off` | Arm / disarm anti-theft Guard |
| `watchdog_on` / `watchdog_off` | Software camera watchdog |
| `thermal_on` / `thermal_off` | Adaptive thermal management |

Example:

```bash
curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"camera_on"}'
```

---

## MQTT

| Item | Value |
|------|--------|
| Broker | `127.0.0.1:1883` (`mosquitto_smartguard`) |
| Auth | Username/password (`allow_anonymous false`) |
| Default user/pass | `smartguard` / `smartguard` (change for production) |
| QoS | 1 |
| Client | **C** (`web/src/mqtt_pub.c`) |

| Topic | Content |
|-------|---------|
| `home/<STUDENT_ID>/persons` | count, cpu_temp, timestamp |
| `home/<STUDENT_ID>/telemetry` | same-style telemetry JSON |
| `home/<STUDENT_ID>/status` | retained `online` / LWT `offline` |
| `home/<STUDENT_ID>/alarm` | Part 4 Guard alarm JSON |
| `home/<STUDENT_ID>/watchdog` | Part 4 camera tampering / no-frame event |
| `home/<STUDENT_ID>/thermal` | Part 4 thermal throttle event |

```bash
mosquitto_sub -h 127.0.0.1 -u smartguard -P 'smartguard' -t 'home/#' -v
```

Setup / reset broker:

```bash
bash scripts/setup_mosquitto.sh
```

---

## Email alerts

Implemented in **C** (`web/src/email_alert.c`) with libcurl SMTP.

| Mode | When |
|------|------|
| Part 3 presence | Persons ≥ 1, debounced ~`EMAIL_DEBOUNCE_SEC` (30 s) |
| Part 4 Guard | Count **increases** while Guard armed (fast, ~2 s min gap) |
| Part 4 Watchdog | No successful frame &gt; 30 s while camera expected → tampering mail |
| Part 4 Thermal | Entering throttle band → notification email |
| Test | `{"cmd":"test_email"}` |

Uses Gmail App Password in `SMTP_PASS` (not your normal Google password).

---

## Dashboard controls

Open `https://127.0.0.1:8443/` after accepting the certificate warning.

| Control | Meaning |
|---------|---------|
| **Camera** | Manual ON/OFF only (page load does not start the cam) |
| **Guard** | Anti-theft: count increase → email + MQTT `…/alarm` |
| **Watchdog** | Stale frames → email + MQTT `…/watchdog` + restart detector |
| **Thermal** | Hot CPU → throttle YOLO + email + MQTT `…/thermal` |
| **Watchdog** | Stalled frames → tamper email + restart detector |
| **Thermal** | Hot CPU → skip YOLO frames / mild resize; clear when cool |

Overlay on the stream shows student ID, datetime, person count, FPS, and throttle tags when active.

---

## Reports

Course mandatory-experiment write-ups (PDF order):

| Folder | Contents |
|--------|----------|
| `report/part 1/` | Experiments 1-1 … 1-6 (boot, kill -9, autostart video, 301, CN cert, HTML) |
| `report/part 2/` | 2-1 … 2-4 (temp curves, C memory, curl load, network) |
| `report/part 3/` | 3-1 … 3-7 (lighting, spoof, resolution, LWT, latency, MQTT/SSH auth fails) |
| `report/part 4/` | 4-1 … 4-4 (Guard, black box, watchdog, thermal) |

Put screenshots/videos in each part’s `fig/` directory as named in that part’s `report.md`.

---

## Daily use / smoke checks

```bash
# Services
systemctl is-active human_detector web_server api_gateway mosquitto_smartguard

# HTTPS + telemetry
curl -sk https://127.0.0.1:8443/api/v1/telemetry
curl -sk https://127.0.0.1:8443/api/v1/persons

# Camera on
curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' -d '{"cmd":"camera_on"}'

# Rebuild C after code changes
make -C web && sudo systemctl restart web_server
```

After Windows sleep / usbipd detach, turn **Camera OFF** then **ON** (or restart `human_detector`) so VideoCapture reopens cleanly.

---

## Troubleshooting

| Symptom | What to try |
|---------|-------------|
| No `/dev/video0` | Re-run Windows attach script; check `usbipd list` |
| Black / idle stream | Press **Camera ON**; check `journalctl -u human_detector -n 50` |
| Temp always wrong / −1 | Start HostCpuTemp scheduled task; check temp file path |
| Email fails | App Password, `EMAIL_ENABLED=1`, no spaces in `SMTP_PASS`, restart `web_server` |
| MQTT silent | `systemctl restart mosquitto_smartguard web_server`; check user/pass |
| Port busy | `ss -ltnp \| grep -E '8080\|8443\|5000\|8000\|1883'` |
| OpenCV / HOG errors | Use venv + `opencv-python-headless>=4.8,<5` from `detection/requirements.txt` |
| Swagger `missing_cmd` | Prefer dashboard or gateway’s HTTP/1.0 command helper; ensure JSON body is sent |

```bash
journalctl -u human_detector -u web_server -u api_gateway -n 80 --no-pager
```

---

## Before submitting to TAs

1. **Scrub secrets** from `config.env` (especially `SMTP_PASS`) — use placeholders; rotate the Gmail App Password if it was ever shared.  
2. Include **`scripts/windows/`** so TAs can attach the camera and host temp helper.  
3. Include **`report/`** with figures/videos filled in.  
4. Confirm cert CN: `openssl x509 -in web/www/server.crt -noout -subject` → **CN = student ID**.  
5. Do **not** rely on committing runtime junk: `data/persons.json`, `data/vision_heartbeat.json`, etc. change while the system runs.  
6. Smoke-test after a clean `wsl --shutdown` that systemd brings services back.

---

## License / course note

Submitted as coursework for the Embedded Systems final project (**Smart Guard System**). Follow your course PDF for grading experiments and demo requirements.
