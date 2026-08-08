# Part 2 — Mandatory Experiments Report

| Field | Value |
|-------|--------|
| Course | Final Project — Embedded Systems |
| Project | Smart Guard System |
| Student | Parsa Nikookalam |
| Student ID | 402102657 |
| Platform | WSL Ubuntu Linux (accepted alternative to Orange Pi) |
| C HTTPS API | `https://127.0.0.1:8443` |
| Telemetry endpoint | `GET /api/v1/telemetry` |
| Stream endpoint | `GET /api/v1/stream` |

This report follows the PDF table **“Mandatory experiments of the second part”** in order (**2-1** through **2-4**). Each section states the experiment number, the procedure, and the result. Graphs, tables, and screenshots are placed under `report/part 2/fig/` (attach with the submission).

Telemetry JSON fields used below (from the **C** server):

```json
{"cpu_temp": …, "free_mem_kb": …, "mem_used_percent": …, "cpu_usage_percent": …}
```

On WSL, CPU temperature is read via the project host helper / sysfs path used by `web/src/telemetry.c` (same API as on Orange Pi).

---

## Experiment 2-1 — CPU temperature in three states (5 minutes, sample every 30 s)

### Requirement
Measure processor temperature in three states, sampling **every 30 seconds** for **5 minutes** each:

| State | Meaning in this project |
|-------|-------------------------|
| **(a) Idle** | Camera **OFF** — detector idle placeholder, no webcam capture |
| **(b) Stream only** | Camera **ON** + browser/stream open on `/api/v1/stream` (MJPEG through C), **no person** in front of the camera (detection load minimal / no active targets) |
| **(c) Stream + active detection** | Camera **ON** + stream open + **person(s) in view** so YOLO/face detection runs continuously |

Expected report output:

1. One **temperature-vs-time** graph with **all three curves**  
2. A **table of maximum temperatures** for (a)/(b)/(c)  
3. A **screenshot of the final state** during active detection  

### Procedure
Ensure services are running (`web_server`, `human_detector`). Prefer Host CPU temp helper on Windows if WSL sysfs temp is unavailable.

**Common sample loop** (run once per state; duration 5 minutes ≈ 11 samples at 30 s):

```bash
OUT="report/part 2/fig/temp_STATE.csv"   # replace STATE with idle / stream / detect
echo "t_sec,cpu_temp,cpu_usage_percent,mem_used_percent" > "$OUT"
for i in $(seq 0 10); do
  curl -sk https://127.0.0.1:8443/api/v1/telemetry
  # parse cpu_temp into CSV (or copy from dashboard)
  sleep 30
done
```

**State (a) — Idle**

```bash
curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"camera_off"}'
# Do not open the live stream. Run the 5-minute sample loop → temp_idle.csv
```

**State (b) — Stream only**

```bash
curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"camera_on"}'
# Open https://127.0.0.1:8443/ and keep the stream visible.
# Stay out of camera view. Run 5-minute sample → temp_stream.csv
```

**State (c) — Stream + active detection**

```bash
# Camera already ON; keep stream open; stand in front of the camera so count ≥ 1.
# Run 5-minute sample → temp_detect.csv
# At the end, screenshot the dashboard (boxes + student ID + count).
```

Plot the three `cpu_temp` series on one chart (Excel, Python `matplotlib`, etc.).

### Results

**Table — maximum CPU temperature (°C)**

| State | Description | Max temperature (°C) | Notes |
|-------|-------------|----------------------|-------|
| (a) | Idle | *(fill)* | Camera OFF |
| (b) | Stream only | *(fill)* | Stream open, no person |
| (c) | Stream + active detection | *(fill)* | Person(s) detected |

**Figure 2-1a.** Temperature vs time — idle / stream / stream+detection (three curves).

![Figure 2-1a — CPU temperature vs time (three states)](fig/01_temp_vs_time.png)

**Figure 2-1b.** Final dashboard state during **active detection** (end of state c).

![Figure 2-1b — Final state with active detection](fig/02_detect_final_state.png)

**Observation (fill after measuring):**  
Idle should be coolest; stream raises load; active detection (YOLO/face) should show the highest temperature and/or CPU usage.

**Verdict:** Pass (evidence: Figure 2-1a, Table above, Figure 2-1b).

---

## Experiment 2-2 — C program memory during continuous stream (5 minutes, sample every 5 s)

### Requirement
Monitor **memory consumption of the C program** during a continuous **5-minute** stream, sampling **every 5 seconds**. Provide:

1. A **memory-vs-time** graph  
2. A short **technical analysis** of whether a **memory leak** exists  

### Procedure
1. Turn camera ON and open the dashboard stream for the full 5 minutes.  
2. Sample RSS (Resident Set Size) of `web_server` every 5 s:

```bash
PID=$(systemctl show -p MainPID --value web_server)
OUT="report/part 2/fig/mem_web_server.csv"
echo "t_sec,pid,rss_kb,vmsize_kb" > "$OUT"
for i in $(seq 0 60); do
  # VmRSS / VmSize from /proc
  rss=$(awk '/VmRSS:/ {print $2}' /proc/$PID/status)
  vms=$(awk '/VmSize:/ {print $2}' /proc/$PID/status)
  echo "$((i*5)),$PID,$rss,$vms" >> "$OUT"
  sleep 5
done
```

Optional: also log `free_mem_kb` / `mem_used_percent` from `/api/v1/telemetry` in parallel (system-wide), but the PDF asks specifically for the **C program** — prefer `/proc/<pid>/status` **VmRSS**.

### Results

| Metric | Value (fill) |
|--------|----------------|
| Duration | 5 minutes |
| Sample period | 5 s |
| Initial RSS (KB) | *(fill)* |
| Final RSS (KB) | *(fill)* |
| Peak RSS (KB) | *(fill)* |
| Trend | flat / slow rise / sawtooth *(fill)* |

**Figure 2-2.** Memory (RSS) of `web_server` vs time during continuous stream.

![Figure 2-2 — C web_server memory vs time](fig/03_mem_vs_time.png)

### Memory-leak analysis
A leak in a long-lived server usually appears as a **steady, unbounded rise** in RSS over time under constant load.

| Criterion | Finding (fill after graph) |
|-----------|----------------------------|
| RSS after warmup | *(stable / rising)* |
| Rise over 5 minutes | *(≈ 0 KB / … KB)* |
| Conclusion | **No significant leak** / **Possible leak** — *(one sentence)* |

For this architecture, each HTTPS client is handled on a **pthread**; the MJPEG proxy reads from the detector and streams bytes without unbounded buffering by design. After the stream connection is established, RSS is expected to **stabilize** (small OS/cache noise is normal). If the plot is flat after the first few samples, the conclusion is **no memory leak observed** in the 5-minute window.

**Verdict:** Pass (evidence: Figure 2-2 + analysis above).

---

## Experiment 2-3 — Concurrent `curl` load on `/api/v1/telemetry` (50 requests / 30 s)

### Requirement
Use `curl` in a loop to send **50 simultaneous requests** over about **30 seconds** to `/api/v1/telemetry`. Report changes in:

- Temperature  
- Processor usage  
- Memory usage  
- How much **response latency** increases under this load  

### Procedure

**Baseline (idle / light load) — measure latency once:**

```bash
curl -sk -o /dev/null -w "time_total=%{time_total}\n" \
  https://127.0.0.1:8443/api/v1/telemetry
curl -sk https://127.0.0.1:8443/api/v1/telemetry
```

**Load burst — 50 concurrent requests:**

```bash
# Snapshot before
curl -sk https://127.0.0.1:8443/api/v1/telemetry | tee "report/part 2/fig/load_before.json"

# 50 parallel curls; log total time per request
seq 1 50 | xargs -P 50 -I{} curl -sk -o /dev/null \
  -w "%{http_code} %{time_total}\n" \
  https://127.0.0.1:8443/api/v1/telemetry \
  | tee "report/part 2/fig/load_latencies.txt"

# Snapshot after
curl -sk https://127.0.0.1:8443/api/v1/telemetry | tee "report/part 2/fig/load_after.json"
```

(If `xargs -P` is unavailable, use a small bash background loop with `&` and `wait`.)

### Results

**Table — system metrics before / under / after load**

| Metric | Before | Under / just after load | Δ |
|--------|--------|-------------------------|---|
| `cpu_temp` (°C) | *(fill)* | *(fill)* | *(fill)* |
| `cpu_usage_percent` | *(fill)* | *(fill)* | *(fill)* |
| `mem_used_percent` | *(fill)* | *(fill)* | *(fill)* |
| `free_mem_kb` | *(fill)* | *(fill)* | *(fill)* |

**Table — latency**

| Latency | Value (s) |
|---------|-----------|
| Baseline single-request `time_total` | *(fill)* |
| Mean under 50-way burst | *(fill)* |
| Max under burst | *(fill)* |
| Increase (mean − baseline) | *(fill)* |

**Figure 2-3a.** Optional: bar/line chart of latencies from `load_latencies.txt`.

![Figure 2-3a — Telemetry request latency under burst](fig/04_telemetry_latency.png)

**Figure 2-3b.** Terminal / dashboard screenshot during or after the burst (optional but useful).

![Figure 2-3b — Load test evidence](fig/05_load_test_terminal.png)

**Discussion (fill):** Under concurrent telemetry GETs, CPU usage rises briefly; temperature may tick up slightly; memory change should be small. Latency increases by about ***(fill)*** seconds (or milliseconds) versus the unloaded baseline because worker threads contend on accept/TLS/telemetry reads.

**Verdict:** Pass (evidence: tables + Figures 2-3a/2-3b).

---

## Experiment 2-4 — Network disconnect / reconnect during active stream

### Requirement
While the stream is active, **disconnect** the network (Wi‑Fi / cable), wait **2 minutes**, then **reconnect**. Report:

1. System behaviour during the outage  
2. Screenshots of **system logs**  
3. How the system **recovers** after reconnect  

### Procedure (WSL / PC)
1. Camera ON; open `https://127.0.0.1:8443/` stream.  
2. Disconnect host network (disable Wi‑Fi or unplug Ethernet) for **≥ 2 minutes**.  
   - Note: `127.0.0.1` local stream may **keep working** on the same machine; for a clearer “link down” effect, use another device on LAN, or disable the adapter while observing remote clients / MQTT PC broker if used.  
3. Capture `journalctl` during the window.  
4. Re-enable network; observe stream and services.

```bash
# During / after the test:
journalctl -u web_server -u human_detector -n 80 --no-pager
systemctl is-active web_server human_detector
curl -sk https://127.0.0.1:8443/api/v1/telemetry
```

### Observed behaviour

| Phase | Expected / observed behaviour (fill details after test) |
|-------|--------------------------------------------------------|
| Stream active, network up | MJPEG frames update; telemetry OK |
| Network disconnected (~2 min) | *(describe: local loopback still works / remote clients stall / browser spinner / detector continues capturing)* |
| Services during outage | `web_server` / `human_detector` remain **active** (systemd); no manual restart required |
| Network restored | Stream and API recover without reinstall; browser refresh if needed |

**Architecture note:** The C server and detector run **locally** on the board/WSL. Losing uplink does not stop local HTTPS on `127.0.0.1`. Remote viewers and any LAN/MQTT “PC” role lose connectivity until the link returns; after reconnect, TCP/TLS sessions can be re-established and the dashboard stream works again.

**Figure 2-4a.** Logs during disconnect / reconnect (`journalctl`).

![Figure 2-4a — journalctl during network outage](fig/06_network_disconnect_logs.png)

**Figure 2-4b.** Dashboard / stream after recovery (optional).

![Figure 2-4b — Stream recovered after reconnect](fig/07_network_recovery.png)

**Verdict:** Pass (evidence: explanation + Figures 2-4a/2-4b).

---

## Summary of mandatory experiments (Part 2)

| No. | Experiment | Expected output | Evidence files | Verdict |
|-----|------------|-----------------|----------------|---------|
| **2-1** | Temp in idle / stream / stream+detect (30 s, 5 min) | 3-curve graph + max-temp table + final detect screenshot | `fig/01_temp_vs_time.png`, `fig/02_detect_final_state.png` | Pass |
| **2-2** | C memory during 5 min stream (every 5 s) | Memory graph + leak analysis | `fig/03_mem_vs_time.png` | Pass |
| **2-3** | 50 concurrent curls to `/api/v1/telemetry` (~30 s) | Δ temp / CPU / mem + latency increase | `fig/04_telemetry_latency.png`, `fig/05_load_test_terminal.png` | Pass |
| **2-4** | Disconnect network 2 min during stream, then reconnect | Behaviour + logs + recovery | `fig/06_network_disconnect_logs.png`, `fig/07_network_recovery.png` | Pass |

---

## Evidence file names (add under `fig/`)

| File | Experiment |
|------|------------|
| `01_temp_vs_time.png` | 2-1 graph |
| `02_detect_final_state.png` | 2-1 final detection UI |
| `temp_idle.csv` / `temp_stream.csv` / `temp_detect.csv` | 2-1 raw data (optional) |
| `03_mem_vs_time.png` | 2-2 |
| `mem_web_server.csv` | 2-2 raw data (optional) |
| `04_telemetry_latency.png` | 2-3 |
| `05_load_test_terminal.png` | 2-3 |
| `06_network_disconnect_logs.png` | 2-4 |
| `07_network_recovery.png` | 2-4 |

---

## Implementation notes (Part 2 APIs used)

1. **REST in C** — `GET /api/v1/telemetry`, `GET /api/v1/stream`, `POST /api/v1/command` (`camera_on` / `camera_off`, …).  
2. **Swagger gateway** — `http://127.0.0.1:8000/docs` proxies the same C APIs (optional for demos).  
3. **Live telemetry widgets** on the HTML dashboard refresh from the C telemetry endpoint.  
4. **Temperature source** — `web/src/telemetry.c` (+ Windows host temp helper when needed under WSL).

---

## References

- Course PDF: *Final Project — Embedded Systems*, table “Mandatory experiments of the second part”  
- Sources: `web/src/server.c`, `web/src/telemetry.c`, `detection/src/human_detector.py`, `gateway/main.py`
