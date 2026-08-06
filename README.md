# Smart Guard — Parts 1–3 (WSL + usbipd)

**Parsa Nikookalam** · `402102657`  
Target: **WSL Ubuntu** + laptop webcam via **usbipd** (auto on login)

## Architecture

```
Windows logon                          WSL boot (systemd)
┌─────────────────────────┐            ┌──────────────────────────────┐
│ Task: SmartGuard-WSL-   │  usbipd    │ human_detector → /dev/video0 │
│ Webcam  (BusId 1-7)     ├───────────►│ web_server :8080→:8443       │
│ Task: HostCpuTempLoop   │  temp file │  + C email + C MQTT          │
└─────────────────────────┘            │ api_gateway :8000/docs       │
                                       │ mosquitto :1883 (auth)       │
                                       └──────────────────────────────┘
```

| Path | Role |
|------|------|
| `web/` | C: HTTPS, REST, stream proxy, **email alerts**, **MQTT publisher** |
| `detection/` | Python vision only (YOLO+face, MJPEG, JSON/SQLite) |
| `gateway/` | Swagger proxy only |
| `services/` | systemd units |
| `scripts/windows/` | **Keep for TAs** — usbipd + host CPU temp helpers |
| `scripts/setup_mosquitto.sh` | Mosquitto broker + password file |

## Windows helpers (in this repo — submit to TAs)

All under `scripts/windows/`:

| File | Purpose |
|------|---------|
| `setup_usbipd_camera.ps1` | Bind webcam + logon task |
| `attach_camera_to_wsl.ps1` | Attach BusId to WSL |
| `host_cpu_temp.ps1` | Read Windows thermal → file |
| `setup_host_temp_task.ps1` | Hidden logon loop (every ~2s) |
| `run_hidden.vbs` | No console flash |
| `camera.busid` | Saved BusId (e.g. `1-7`) |

WSL reads temp from:  
`/mnt/c/Users/<you>/AppData/Local/SmartGuard/cpu_temp.txt`

CPU **usage** and **memory** are read in C from Linux (`/proc/stat`, `sysinfo`) inside WSL — that code is in `web/src/telemetry.c`.

## One-time setup

### A) Windows — Admin PowerShell (camera)

```powershell
cd \\wsl$\Ubuntu\home\parsa\embedded_project
powershell -ExecutionPolicy Bypass -File .\scripts\windows\setup_usbipd_camera.ps1 -BusId 1-7
```

### B) Windows — CPU temp file (no empty terminal)

```powershell
cd \\wsl$\Ubuntu\home\parsa\embedded_project\scripts\windows
powershell -ExecutionPolicy Bypass -File .\setup_host_temp_task.ps1
```

### C) WSL — services + Mosquitto

```bash
cd ~/embedded_project
chmod +x scripts/*.sh
bash scripts/enable_wsl_autostart.sh
bash scripts/setup_mosquitto.sh
make -C web && sudo systemctl restart web_server
```

## Part 3

| Piece | Where |
|-------|--------|
| Detection overlay + FPS | Python `detection/` |
| Email (C, 30s debounce) | `web/src/email_alert.c` |
| MQTT (C, QoS1 + LWT) | `web/src/mqtt_pub.c` → Mosquitto |

Topics:
- `home/402102657/persons`
- `home/402102657/telemetry`
- LWT: `home/402102657/status` → `offline` / `online`

Payload JSON:
```json
{"count":1,"cpu_temp":47.20,"timestamp":1710000000}
```

Subscribe test:
```bash
mosquitto_sub -h 127.0.0.1 -u smartguard -P smartguard -t 'home/#' -v
```

## APIs (C on :8443)

| Method | Path |
|--------|------|
| GET | `/api/v1/stream` |
| GET | `/api/v1/persons` |
| GET | `/api/v1/telemetry` |
| GET | `/api/v1/history` |
| POST | `/api/v1/command` `{"cmd":"reboot"}` or `{"cmd":"test_email"}` |

Swagger: http://127.0.0.1:8000/docs  

**Before sending to TAs:** remove real Gmail App Password from `config.env` (use a placeholder) and regenerate the Google App Password if it was shared.
