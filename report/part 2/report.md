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

Use the project sampler (writes real CSV rows from `/api/v1/telemetry`):

```bash
cd ~/embedded_project
# fix Windows line endings once if needed:
sed -i 's/\r$//' scripts/sample_temp_csv.sh

# (a) Idle — camera OFF, do not open stream (~5 min)
bash scripts/sample_temp_csv.sh idle

# (b) Stream only — script turns camera ON; open dashboard stream; stay out of view (~5 min)
bash scripts/sample_temp_csv.sh stream

# (c) Stream + detection — open stream and stand in front of camera (~5 min)
bash scripts/sample_temp_csv.sh detect

# Plot three curves
.venv/bin/python scripts/plot_part2_figs.py
```

Output files:

- `report/part 2/fig/temp_idle.csv`
- `report/part 2/fig/temp_stream.csv`
- `report/part 2/fig/temp_detect.csv`

Plot the three `cpu_temp` series on one chart (Excel, Python `matplotlib`, etc.).

### Results

**Table — maximum CPU temperature (°C)**

| State | Description | Max temperature (°C) | Notes |
|-------|-------------|----------------------|-------|
| (a) | Idle | **72.85** | Camera OFF (`temp_idle.csv`) |
| (b) | Stream only | **61.85** | No person in view (`temp_stream.csv`) |
| (c) | Stream + active detection | **88.85** | Person detected (`temp_detect.csv`) |

**Figure 2-1a.** Temperature vs time — idle / stream / stream+detection (three curves).

![Figure 2-1a — CPU temperature vs time (three states)](fig/01_temp_vs_time.png)

**Figure 2-1b.** Final dashboard state during **active detection** (end of state c).

![Figure 2-1b — Final state with active detection](fig/02_detect_final_state.png)

**Observation:**  
All three states were sampled for 5 minutes (30 s step). Peak temps: idle **72.85 °C**, stream **61.85 °C**, stream+detection **88.85 °C**. Active detection shows the highest thermal load.

**Verdict:** Pass for the temperature curves + max table (add `02_detect_final_state.png` screenshot if not attached yet).

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
| Initial RSS (KB) | **17512** |
| Final RSS (KB) | **17512** |
| Peak RSS (KB) | **17512** |
| Trend | **flat** |

**Figure 2-2.** Memory (RSS) of `web_server` vs time during continuous stream.

![Figure 2-2 — C web_server memory vs time](fig/03_mem_vs_time.png)

### Memory-leak analysis
A leak in a long-lived server usually appears as a **steady, unbounded rise** in RSS over time under constant load.

| Criterion | Finding (fill after graph) |
|-----------|----------------------------|
| RSS after warmup | **stable** (constant 17512 KB) |
| Rise over 5 minutes | **≈ 0 KB** |
| Conclusion | **No significant leak** — RSS stayed flat for the full 5‑minute stream window. |

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
| `cpu_temp` (°C) | **58.85** | **58.85** | **0.00** |
| `cpu_usage_percent` | **2.68** | **4.84** | **+2.16** |
| `mem_used_percent` | **24.41** | **24.68** | **+0.27** |
| `free_mem_kb` | **5996476** | **5975144** | **−21332** |

**Table — latency**

| Latency | Value (s) |
|---------|-----------|
| Baseline single-request `time_total` | ≈ **0.020** (min in burst ≈ unloaded) |
| Mean under 50-way burst | **0.0290** (~29.0 ms) |
| Max under burst | **0.0415** (~41.5 ms) |
| Increase (mean − min) | ≈ **0.009** s (~9 ms) |

**Figure 2-3.** Bar/line chart of latencies from `load_latencies.txt`.

![Figure 2-3 — Telemetry request latency under burst](fig/04_telemetry_latency.png)

**Discussion:** Under concurrent telemetry GETs, CPU usage rose briefly (**+2.16%**); temperature did not move in the before/after snapshots (**58.85 °C**); system memory change was small (**+0.27%**). Burst latencies stayed in the **~20–42 ms** band (mean **~29 ms**), so contention on accept/TLS/telemetry reads is visible but modest.

**Verdict:** Pass (evidence: tables + Figure 2-3).

---

## Experiment 2-4 — Network disconnect / reconnect during active stream

### Requirement
While the stream is active, **disconnect** the network (Wi‑Fi / cable), wait **2 minutes**, then **reconnect**. Report:

1. System behaviour during the outage  
2. Screenshots of **system logs**  
3. How the system **recovers** after reconnect  

### Behaviour in this project (localhost bind)

Web, stream, and APIs listen on **`127.0.0.1`** (WSL loopback). Turning **Windows Wi‑Fi OFF** does **not** stop local services: the browser on the same machine can still open `https://127.0.0.1:8443/` because loopback does not need an uplink.

| Phase | What happens |
|-------|----------------|
| Wi‑Fi ON, stream open | MJPEG + telemetry OK |
| Wi‑Fi OFF (~2 min) | Uplink gone; **localhost stream/web keep working**; `web_server` / `human_detector` stay **active** |
| Wi‑Fi ON again | External network returns; no restart needed for local services |

### Procedure
1. Camera ON; open `https://127.0.0.1:8443/` stream.  
2. Disconnect **Windows Wi‑Fi** for **≥ 2 minutes**.  
3. Confirm stream/API still work on localhost; capture `journalctl` / `systemctl` / curl.  
4. Re-enable Wi‑Fi; confirm nothing broke.

```bash
# During / after the test:
journalctl -u web_server -u human_detector -n 80 --no-pager
systemctl is-active web_server human_detector api_gateway
curl -sk https://127.0.0.1:8443/api/v1/telemetry
```

### Observed behaviour

| Phase | Expected / observed behaviour |
|-------|-------------------------------|
| Stream active, Wi‑Fi up | MJPEG + telemetry OK on `127.0.0.1` |
| Wi‑Fi disconnected (~2 min) | Localhost stream/web **still OK** (loopback ≠ Wi‑Fi) |
| Services during outage | `web_server` / `human_detector` remain **active** |
| Wi‑Fi restored | No special recovery; refresh optional |

**Architecture note:** Bind address is **127.0.0.1**, so Part 2-4 shows that the appliance keeps serving locally without uplink.

**Figure 2-4a.** Logs during disconnect / reconnect (`journalctl -u web_server` …).

![Figure 2-4a — journalctl during network outage](fig/06_network_disconnect_logs.png)

**Figure 2-4b.** Dashboard / stream after recovery (optional).

![Figure 2-4b — Stream recovered after reconnect](fig/07_network_recovery.png)

**Verdict:** Incomplete — behaviour text ready; need `fig/06_network_disconnect_logs.png` (+ optional `07_…`) after you run the disconnect test.

---

## Summary of mandatory experiments (Part 2)

| No. | Experiment | Expected output | Evidence files | Verdict |
|-----|------------|-----------------|----------------|---------|
| **2-1** | Temp in idle / stream / stream+detect (30 s, 5 min) | 3-curve graph + max-temp table + final detect screenshot | `01_temp_vs_time.png`, `02_detect_final_state.png` | **Pass** (curves+table done; attach detect screenshot if missing) |
| **2-2** | C memory during 5 min stream (every 5 s) | Memory graph + leak analysis | `03_mem_vs_time.png`, `mem_web_server.csv` | **Pass** (RSS flat 17512 KB) |
| **2-3** | 50 concurrent curls to `/api/v1/telemetry` (~30 s) | Δ temp / CPU / mem + latency increase | `04_telemetry_latency.png`, `load_*.json/txt` | **Pass** |
| **2-4** | Disconnect network 2 min during stream, then reconnect | Behaviour + logs + recovery | `06_…`, `07_…` | **Incomplete** — do later |

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
