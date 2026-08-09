# Part 2 — Code & Architecture Explanation

| Field | Value |
|-------|--------|
| Document | Final architecture & code explanation |
| Companion | `report/part 2/report.md` (mandatory experiments) |
| Project | Smart Guard System |
| Student | Parsa Nikookalam · `402102657` |
| Scope | REST APIs in C, live telemetry, shared state, Swagger gateway |

Part 2 does **not** replace the Part 1 server. It **extends** `web/src/server.c` with JSON endpoints and adds a documentation-only proxy.

---

## 1. Goals (from the course brief)

| Requirement | Implementation |
|-------------|----------------|
| REST APIs | Handled inside C HTTPS server (`server.c`) |
| Live system telemetry | `telemetry.c` → `GET /api/v1/telemetry` |
| Swagger / API docs | `gateway/main.py` proxies to C; UI at `:8000/docs` |
| Core logic still in C | Gateway must not implement business rules |

Also used heavily in Part 2 **experiments**: temperature sampling, C process memory, concurrent `curl` load on telemetry, behaviour under network loss.

---

## 2. System architecture (Part 2)

```
┌──────────────┐  HTTPS :8443   ┌─────────────────────────────────────┐
│  Dashboard   │◄──────────────►│  web_server (C)                     │
│  JS fetch()  │                │  GET  /api/v1/telemetry             │
└──────────────┘                │  GET  /api/v1/persons               │
                                │  GET  /api/v1/history               │
┌──────────────┐  HTTP :8000    │  GET  /api/v1/stream  (proxy)       │
│ Swagger UI   │──proxy────────►│  GET  /api/v1/config                │
│ api_gateway  │                │  POST /api/v1/command               │
└──────────────┘                └───────────┬─────────────┬───────────┘
                                            │             │
                         ┌──────────────────▼──┐   ┌──────▼──────────────┐
                         │ telemetry.c         │   │ persons_state.c     │
                         │ /proc, sysinfo,     │   │ persons.json (R)    │
                         │ host temp file/WSL  │   │ history.db (R)      │
                         └─────────────────────┘   └─────────▲───────────┘
                                                             │ write
                                                   ┌─────────┴───────────┐
                                                   │ human_detector.py   │
                                                   │ (Part 3 vision;     │
                                                   │  already feeds JSON)│
                                                   └─────────────────────┘
```

**Rule of ownership:**

| Concern | Owner |
|---------|--------|
| HTTP routing, JSON responses, stream proxy, commands | **C** |
| Measuring CPU/RAM/temp | **C** (`telemetry.c`) |
| Writing person counts / history rows | **Python detector** |
| OpenAPI documentation UI | **Python gateway** (proxy only) |

---

## 3. File map (Part 2–centric)

```
web/
├── src/
│   ├── server.c           # Route table + stream proxy + command dispatcher
│   ├── telemetry.c/.h     # cpu_temp, mem, cpu_usage
│   └── persons_state.c/.h # Read persons.json + SQLite history/blackbox
gateway/
├── main.py                # FastAPI + Swagger + HTTP/1.0 command helper
├── requirements.txt
└── static/                # Local Swagger UI assets (offline /docs)
services/
├── web_server.service
└── api_gateway.service
data/
├── persons.json           # { "count", "timestamp" }
└── history.db             # detections table (written by detector)
```

---

## 4. REST routing in C (`server.c`)

### 4.1 Request intake

After TLS accept and `SSL_accept`:

1. `ssl_read_http_request()` accumulates bytes until headers + `Content-Length` body are complete (needed because some clients split TLS records).  
2. `path_match(req, "GET", "/api/v1/…")` finds routes (case-tolerant).  
3. Handlers call `send_ssl_response()` with JSON or stream bytes.

### 4.2 Endpoint catalogue (Part 2 core)

| Method | Path | Handler behaviour |
|--------|------|-------------------|
| GET | `/api/v1/telemetry` | `get_system_telemetry()` → JSON |
| GET | `/api/v1/persons` | `read_persons_snapshot()` → JSON |
| GET | `/api/v1/history` | `read_persons_history()` last 5 rows → JSON |
| GET | `/api/v1/config` | Student name/ID from globals loaded at start |
| GET/HEAD | `/api/v1/stream` | `handle_stream_proxy()` → MJPEG from detector |
| POST | `/api/v1/command` | `handle_command()` parse `"cmd"` |

Example telemetry payload:

```json
{
  "cpu_temp": 72.50,
  "free_mem_kb": 4123456,
  "mem_used_percent": 48.20,
  "cpu_usage_percent": 23.10
}
```

### 4.3 Stream proxy (`handle_stream_proxy`)

Purpose: browser talks **only** to the C HTTPS server; it never needs to know about Python `:5000`.

Algorithm:

1. TCP connect to `STREAM_HOST:STREAM_PORT` (default `127.0.0.1:5000`).  
2. Send `GET /video_feed HTTP/1.1`.  
3. Read upstream response; rewrite outer headers to:

```http
Content-Type: multipart/x-mixed-replace; boundary=frame
```

4. Pipe body chunks with `SSL_write` until client or upstream closes.

If the detector is down → `502` JSON `detector_not_running`.

This is why Part 2 load tests and Part 3 demos can use a single secure origin.

### 4.4 Command dispatcher (`handle_command`)

Parses JSON `"cmd"` from body (or whole request buffer as fallback).

Part 2–relevant commands:

| cmd | Behaviour |
|-----|-----------|
| `reboot` | On `TARGET=wsl`: soft marker file only (no host reboot). On board: `fork` + `reboot` |
| `camera_on` / `camera_off` | Persist flag for detector (see Part 3/4) |

Later parts add `guard_*`, `watchdog_*`, `thermal_*`, `test_email` in the **same** function — one extensible command bus.

---

## 5. Telemetry module (`telemetry.c` / `telemetry.h`)

### 5.1 Public API

```c
typedef struct {
    float cpu_temp;
    float mem_used_percent;
    long free_mem_kb;
    float cpu_usage_percent;
} SystemTelemetry;

int get_system_telemetry(SystemTelemetry *telemetry);
```

### 5.2 CPU usage

Classic Linux approach in `read_cpu_usage()`:

1. Read `/proc/stat` aggregate `cpu` line (user/nice/system/idle/iowait/…).  
2. Compare with **static previous counters** from the last call (no internal `sleep`).  
3. `usage% = 100 * (Δtotal − Δidle) / Δtotal`.  

The first call after process start often returns `0` (no prior sample). Dashboard polls every few seconds, so later samples are meaningful.

### 5.3 Memory

Uses `sysinfo()` (or `/proc/meminfo` patterns):

- `free_mem_kb`  
- `mem_used_percent = used / total * 100`

### 5.4 Temperature — board vs WSL

**Orange Pi / native Linux:**

- Scan `/sys/class/thermal/thermal_zone*/temp`  
- Scan `/sys/class/hwmon/*/temp*_input`  
- Prefer sensors whose names look like CPU/SoC (`x86_pkg`, `cpu-thermal`, `k10temp`, …) via a scoring function  

**WSL:**

Linux sysfs often has **no real package temp**. So:

1. Prefer a **cache file** written by Windows (`HostCpuTemp` task), e.g.  
   `/mnt/c/Users/<user>/AppData/Local/SmartGuard/cpu_temp.txt`  
2. Or `data/host_cpu_temp.txt`  
3. Fallback: run `scripts/host_cpu_temp.sh` (PowerShell) with a short in-process cache (PowerShell is slow)

`web_server.service` sets:

```ini
Environment="HOST_CPU_TEMP_CMD=/bin/bash …/scripts/host_cpu_temp.sh"
Environment=PATH=…:/mnt/c/Windows/System32/WindowsPowerShell/v1.0
```

so systemd can still reach Windows tools.

### 5.5 Why this matters for experiments

Part 2 mandatory tests sample `/api/v1/telemetry` every 30 s (temp curves) and under curl load. All of that data comes from this module — **no Python telemetry service**.

---

## 6. Shared person / history state (`persons_state.c`)

### 6.1 `persons.json` (IPC file)

Written by Python detector; read by C:

```json
{"count": 1, "timestamp": 1710000000}
```

`read_persons_snapshot()` does a small-string parse (`strstr` + `sscanf`) — intentionally dependency-light (no full JSON library required in C).

Used by:

- `GET /api/v1/persons`  
- Email / MQTT / Part 4 Guard (same snapshot reader)

### 6.2 `history.db` (SQLite)

Detector inserts into `detections(count, timestamp)`.  
C opens **read-only** and:

- `read_persons_history()` → last **5** rows for `GET /api/v1/history`  
- `read_blackbox_stats()` → Part 4 `GET /api/v1/blackbox` JSON:  
  `{"total_human_events":…,"stored":…,"capacity":500}`

This split keeps **writers in Python** (vision cadence) and **readers in C** (API surface).

---

## 7. FastAPI / Swagger gateway (`gateway/main.py`)

### 7.1 Why it exists

The PDF allows Python for Swagger. The gateway:

- Serves OpenAPI docs at `http://127.0.0.1:8000/docs`  
- Proxies each documented route to `https://127.0.0.1:8443/...`  
- Uses **local** Swagger static files so `/docs` works offline  

It must **not** compute telemetry or detection itself.

### 7.2 Proxy helpers

| Helper | Role |
|--------|------|
| `_proxy_json` | httpx GET/POST to C with `verify=False` (self-signed) |
| `_c_command_http10` | Tiny raw **HTTP/1.0** TLS client for `POST /command` |

### 7.3 Why HTTP/1.0 for commands?

Some HTTP/2 or httpx write patterns split headers and body across TLS records. Early C code that did a single `SSL_read` then reported `missing_cmd`.  

Fix (two sides):

1. C: `ssl_read_http_request()` loops until `Content-Length` satisfied.  
2. Gateway: send a classic HTTP/1.0 request in **one** `sendall` for commands.

### 7.4 systemd

`api_gateway.service` runs the module directly (uvicorn is started from `if __name__ == "__main__"` in `gateway/main.py`):

```ini
WorkingDirectory=/home/parsa/embedded_project/gateway
ExecStart=/home/parsa/embedded_project/.venv/bin/python \
  /home/parsa/embedded_project/gateway/main.py
```

That calls `uvicorn.run(app, host="0.0.0.0", port=GATEWAY_PORT)` with `GATEWAY_PORT` from `config.env` (default `8000`).

---

## 8. Dashboard integration (Part 2 UX)

`index.html` JavaScript:

1. Polls `/api/v1/telemetry` every few seconds → fill CPU/temp/RAM cards.  
2. Polls `/api/v1/persons` → person count widget.  
3. Sets `<img src="/api/v1/stream">` for live video.  
4. Uses `/api/v1/config` for name/ID in the header.

All of those requests hit the **same origin** as the page (`:8443`), so there is no mixed-content problem.

---

## 9. Concurrency under Part 2 load tests

Experiment **2-3** fires ~50 parallel `GET /api/v1/telemetry` requests.

Because each connection is a pthread:

- Accept loop stays responsive.  
- Telemetry reads are mostly lock-free local `/proc` + cached temp.  
- Latency rises under TLS handshake + thread scheduling — that rise is what the report measures.

Memory experiment **2-2** samples **RSS of `web_server`** (`/proc/<pid>/status` `VmRSS`) during a long stream — the long-lived stream thread holds the MJPEG proxy open; RSS should stabilize if there is no leak.

---

## 10. Network disconnect experiment (2-4) vs code

Local `127.0.0.1` traffic does **not** depend on Wi-Fi. Architecture implications:

| Component | If uplink dies |
|-----------|----------------|
| C server + detector on same machine | Keep running |
| Remote browser / MQTT “PC” role | Stalls until link returns |
| systemd units | Stay `active` |

Recovery is “reconnect TCP/TLS clients”; no special reconnect state machine is required in C for localhost demos.

---

## 11. Build & link notes

Part 2 needs:

```text
-lssl -lcrypto -lsqlite3 -lpthread
```

SQLite is for history reads. Telemetry itself needs no extra library beyond libc.

---

## 12. Configuration keys used in Part 2

| Key | Consumer |
|-----|----------|
| `HTTPS_PORT` / `HTTP_PORT` | C listeners |
| `STREAM_HOST` / `STREAM_PORT` | Stream proxy upstream |
| `GATEWAY_PORT` | Uvicorn |
| `HOST_CPU_TEMP_FILE` / helper script | WSL temperature |
| `TARGET` | reboot command behaviour |

---

## 13. End-to-end example: telemetry request

1. Client: `GET /api/v1/telemetry` over TLS.  
2. `server.c` matches path.  
3. `get_system_telemetry(&t)` fills struct.  
4. `snprintf` JSON → `send_ssl_response(200, …)`.  
5. Dashboard chart or Part 2 CSV logger records `cpu_temp`.

---

## 14. How Part 2 connects to other parts

| Part | Relationship |
|------|----------------|
| Part 1 | Same TLS server; Part 2 adds routes |
| Part 3 | Detector fills `persons.json` / DB that Part 2 already exposes |
| Part 4 | Extra GET routes (`/guard`, `/blackbox`, …) and more `cmd` values |

---

## 15. Quick reference commands

```bash
curl -sk https://127.0.0.1:8443/api/v1/telemetry
curl -sk https://127.0.0.1:8443/api/v1/persons
curl -sk https://127.0.0.1:8443/api/v1/history
# Swagger
xdg-open http://127.0.0.1:8000/docs   # or browser on Windows host
```

---

## 16. Conclusion

Part 2 turns the Part 1 HTTPS appliance into a **telemetry- and control-capable REST device**:

| Layer | Responsibility |
|-------|----------------|
| C (`server.c`, `telemetry.c`, `persons_state.c`) | Measurement, JSON APIs, stream proxy, commands |
| Shared files / SQLite | Bridge from vision (writer) to API (reader) |
| FastAPI gateway | OpenAPI docs + transparent proxy only |

This separation matches the course rule: **core logic in C**; Python is limited to allowed roles (later vision + thin API documentation).

---

## 17. Related documents

| Document | Role |
|----------|------|
| `report/part 2/report.md` | Mandatory experiments 2-1 … 2-4 |
| `report/part 2/explain.md` | This file — architecture & code |
| `report/part 1/explain.md` | HTTPS server foundation |
| `README.md` (repo root) | Full-project setup guide |
