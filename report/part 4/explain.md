# Part 4 — Code & Architecture Explanation

**Project:** Smart Guard System  
**Student:** Parsa Nikookalam · `402102657`  
**Scope of Part 4:** **Guard** (anti-theft) mode, **black box** history, **software watchdog**, **adaptive thermal** management — coordinated in **C**, with Python consuming thermal hints and writing heartbeats / SQLite events.

Part 4 is a coordinator layer on top of Parts 1–3. It does not replace the web server or detector; it **arms policies** around the same shared files and APIs.

---

## 1. Goals (from the course brief)

| # | Feature | Expected demo | Code home |
|---|---------|---------------|-----------|
| 4-1 | Guard mode | Video/images of alarm behaviour | `features_part4.c` + `guard_state.c` + MQTT alarm + email |
| 4-2 | Black box | DB / API screenshot of stored events | Detector SQLite + `persons_state.c` + `GET /api/v1/blackbox` |
| 4-3 | Software watchdog | Disconnect camera → reaction video/image | Heartbeat file + watchdog branch in `features_part4.c` |
| 4-4 | Adaptive thermal | Stress CPU → throttle images | Thermal thresholds + `thermal_control.json` + detector skip |

Dashboard toggles: **Guard / Watchdog / Thermal / Camera**.

---

## 2. System architecture (Part 4)

```
                    ┌──────────────────────────────────────────────┐
                    │              web_server (C)                  │
                    │                                              │
  REST/UI toggles   │  guard_state.c      feature_flags.c          │
  guard_on/off      │  (armed?)           (watchdog/thermal on?)   │
  watchdog_on/off   │                                              │
  thermal_on/off    │         features_part4.c  [pthread 1 Hz]     │
                    │              │                               │
                    │    ┌─────────┼─────────┬─────────────┐       │
                    │    ▼         ▼         ▼             ▼       │
                    │  Guard    Watchdog   Thermal     (reads)     │
                    │  edge     heartbeat  temp≥85     persons.json│
                    │  email+   email+     write       telemetry   │
                    │  MQTT     restart    thermal_    heartbeat   │
                    │  alarm    detector   control.json            │
                    └──────────────┬───────────────┬───────────────┘
                                   │               │
                                   ▼               ▼
                    ┌──────────────────┐   ┌───────────────────────┐
                    │ human_detector.py│   │ data/history.db       │
                    │ read thermal_    │   │ circular 500 events   │
                    │ control; skip    │   │ meta total_human_…    │
                    │ YOLO every N;    │   └───────────────────────┘
                    │ write heartbeat  │
                    └──────────────────┘
```

**Single coordinator thread** (`part4_thread`) polls once per second. That keeps Guard edges responsive without busy-waiting inside the HTTPS accept loop.

---

## 3. File map

```
web/src/
├── features_part4.c/.h     # Coordinator thread (Guard + WD + thermal)
├── guard_state.c/.h        # Persist Guard armed flag
├── feature_flags.c/.h      # Persist watchdog + thermal enabled
├── camera_state.c/.h       # Camera on/off file for detector
├── email_alert.c           # email_send_event() used by Part 4
├── mqtt_pub.c              # mqtt_publish_alarm()
└── persons_state.c         # blackbox stats + persons snapshot
detection/src/human_detector.py
  - BLACKBOX_CAPACITY = 500
  - write vision_heartbeat.json (ts on real frames only)
  - read thermal_control.json (detect_every, yolo_input)
data/   (paths from C: ../data/… relative to WorkingDirectory=web/)
├── guard_state.json         # {"armed":0|1}           — default disarmed
├── watchdog_state.json      # {"enabled":0|1}         — default ON if file missing
├── thermal_state.json       # {"enabled":0|1}         — default ON if file missing
├── camera_state.json        # {"enabled":0|1}         — default OFF
├── vision_heartbeat.json    # {"ts":…,"mode":"idle"|"capturing"|…}
├── thermal_control.json     # throttle_level, detect_every, yolo_input (C→Python)
├── persons.json
├── history.db
└── latest_detection.jpg
```

**Defaults in code:** `feature_flags.c` initializes watchdog/thermal to **enabled** when the state file is missing (`return 1`). Guard (`guard_state.c`) and camera (`camera_state.c`) default to **off**.

---

## 4. Process startup

From `main.c`:

```c
email_alert_start();
mqtt_pub_start();
part4_start();          /* creates detached part4_thread */
start_http_server(0);
```

`part4_start()`:

```c
pthread_attr_setdetachstate(…, PTHREAD_CREATE_DETACHED);
pthread_create(…, part4_thread, NULL);
```

On entry, the thread loads thresholds from `config.env` and writes an initial `thermal_control.json` with throttle level 0.

---

## 5. Feature 4-1 — Guard (anti-theft) mode

### 5.1 State

Persisted in `data/guard_state.json` as `{"armed":0|1}` via `guard_state.c` (atomic write through `.tmp` + `rename`).  
Default when missing: **disarmed**.  

REST:

- `POST {"cmd":"guard_on"}` / `guard_off` (aliases `guard_arm` / `guard_disarm` also accepted)  
- `GET /api/v1/guard` → `{"armed":true|false}`

### 5.2 Trigger condition (important)

```c
if (guard_is_armed() && count > prev_count) {
    /* fire alarm */
}
prev_count = count;
```

| Event | Guard fires? |
|-------|----------------|
| 0 → 1 persons | **Yes** |
| 1 → 2 persons | **Yes** |
| 1 stays 1 | No (Part 3 presence email may still debounce) |
| 2 → 1 | No (decrease) |

This matches “someone new appeared / count increased” anti-theft semantics better than “any time count ≥ 1”.

### 5.3 Actions on fire

1. Build text body with student ID, old→new count, CPU temp, timestamp.  
2. `email_send_event(…, attach_photo=1)` — **fast** path (min gap ~**2 s** between Guard mails).  
3. `mqtt_publish_alarm(count, temp, ts)` → topic:

```text
home/<STUDENT_ID>/alarm
{"alarm":true,"count":N,"cpu_temp":T,"timestamp":TS}
```

4. Log `[part4] GUARD alarm a → b`.

### 5.4 Interaction with Part 3 email

| Path | When | Debounce |
|------|------|----------|
| Part 3 presence | count ≥ 1 | ~30 s |
| Part 4 Guard | count **increases** while armed | ~2 s |

Both can coexist: Guard is the urgent edge; Part 3 is periodic presence.

### 5.5 Demo checklist (code-aligned)

1. `guard_on` + `camera_on`.  
2. `mosquitto_sub -t home/402102657/alarm`.  
3. Walk into frame → expect mail + MQTT.  
4. Capture video/screenshots for report experiment 4-1.

---

## 6. Feature 4-2 — Black box

### 6.1 Writer (Python)

On detection updates, `human_detector.py` calls `append_history(...)`:

- `INSERT INTO detections(count, timestamp)`  
- If `bump_total` and `count > 0`: increment `meta.total_human_events`  
- Prune to last **`BLACKBOX_CAPACITY` (500)** rows (`DELETE … NOT IN (… LIMIT 500)`)

Database file: `data/history.db`.

### 6.2 Reader (C)

`persons_state.c`:

| Function | API |
|----------|-----|
| `read_persons_history` | `GET /api/v1/history` (last 5) |
| `read_blackbox_stats` | `GET /api/v1/blackbox` |

`GET /api/v1/blackbox` response shape (from `server.c`):

```json
{"total_human_events": 123, "stored": 45, "capacity": 500}
```

- `total_human_events` — from SQLite `meta` table (lifetime counter)  
- `stored` — `COUNT(*)` of rows currently in `detections`  
- `capacity` — `BLACKBOX_CAPACITY` (500)  

Opens SQLite **read-only** so the API cannot corrupt the writer’s DB.

### 6.3 Why “black box”?

Like an aviation recorder: even if the live stream is gone and count returns to 0, **past events remain** (until capacity eviction). Experiment 4-2 asks for a screenshot of those stored events (API JSON, UI, or `sqlite3` SELECT).

### 6.4 Example inspection

```bash
curl -sk https://127.0.0.1:8443/api/v1/blackbox
sqlite3 data/history.db \
  "SELECT id,count,datetime(timestamp,'unixepoch') FROM detections ORDER BY id DESC LIMIT 20;"
```

---

## 7. Feature 4-3 — Software watchdog

### 7.1 Heartbeat contract

Detector writes `data/vision_heartbeat.json`:

```json
{"ts": 1710000123, "mode": "capturing"}
```

**Critical design rule:** `write_heartbeat(mode, touch_ts=…)` in Python:

- `touch_ts=True` → update `ts` (healthy real frame, or idle with camera off).  
- `touch_ts=False` → keep previous `ts` while `mode` stays non-idle (opening camera, waiting for usbipd, read failures).  

Watchdog only arms when `mode != "idle"`. If `ts` were refreshed during stalled “capturing” waits, the watchdog would never fire — that bug was fixed by using `touch_ts=False` on those paths.

| mode (typical) | Meaning |
|----------------|---------|
| `idle` | Camera OFF — watchdog does **not** treat this as tampering |
| `capturing` (non-idle) | Camera intended ON — `ts` must keep advancing on real frames |

### 7.2 Watchdog logic (C)

```c
if (watchdog_is_enabled() && read_heartbeat(&hb_ts, mode, …) == 0) {
    age = now - hb_ts;
    watching = (strcmp(mode, "idle") != 0);
    if (watching && age > WATCHDOG_SEC && age < 3600) {
        email_send_event("… camera tampering …");
        system("systemctl restart human_detector");
        /* also rate-limit emails ~45s */
    }
}
```

Defaults: `WATCHDOG_SEC=30` from `config.env` (minimum clamped to 10).

### 7.3 Enable flag

`watchdog_on` / `watchdog_off` via `feature_flags.c` → `data/watchdog_state.json` + `GET /api/v1/watchdog`.  
**Default: enabled** if the file does not exist.

### 7.4 Disconnect-camera experiment (4-3)

1. Watchdog ON, Camera ON, healthy stream.  
2. Unplug USB cam or `usbipd detach`.  
3. After >30 s: tampering email + detector restart in `journalctl`.  
4. Re-attach; detector reopens device (may need Camera toggle if capture handle is dead).

### 7.5 Why restart the detector?

A wedged OpenCV `VideoCapture` often will not recover in-process after USB loss. systemd restart gives a clean Python interpreter and re-init of ONNX nets + device nodes.

---

## 8. Feature 4-4 — Adaptive thermal management

### 8.1 Thresholds

From `features_part4.c` / `config.env`:

| Name | Default | Meaning |
|------|---------|---------|
| `THERMAL_TEMP_C` | **85** | Enter / raise throttle |
| `THERMAL_CLEAR_C` | **78** | Hysteresis clear to level 0 |

Hysteresis avoids flapping around a single point.

### 8.2 Throttle levels written to JSON

`write_thermal_control(level, temp)` creates `data/thermal_control.json`:

| level | `yolo_input` | `detect_every` | Intent |
|-------|--------------|----------------|--------|
| 0 | 640 | 1 | Full detect |
| 1 | 640 | 2 | Run YOLO every 2nd frame |
| 2 | 416 | 3 | Mildly smaller + every 3rd frame |

`frame_sleep_ms` is kept **0** on purpose: sleeping the capture loop made the MJPEG stream look frozen on WSL. Skipping inference keeps the pipeline live.

Level 2 is chosen when `temp >= THERMAL_TEMP_C + 8` (hotter band).

### 8.3 Detector side

Each loop iteration:

```python
thermal = read_thermal_control()
detect_every = max(1, int(thermal["detect_every"]))
run_detect = (frame_i % detect_every) == 0
```

Overlay may show `THR{level} skip={detect_every}`.

### 8.4 Email on enter

When throttle level becomes &gt; 0, C sends a thermal notification (rate-limited ~60 s).

### 8.5 Enable flag

`thermal_on` / `thermal_off` → `data/thermal_state.json` + `GET /api/v1/thermal`.  
**Default: enabled** if the file does not exist.  
If disabled while throttling, coordinator forces level 0 and rewrites `thermal_control.json`.

### 8.6 Stress tools for experiment 4-4

```bash
stress-ng --cpu 0 --timeout 300s &
curl -sk https://127.0.0.1:8443/api/v1/telemetry
cat data/thermal_control.json
```

On WSL, ensure HostCpuTemp is updating so `telemetry.c` sees rising °C. For demos, temporarily lowering `THERMAL_TEMP_C` is acceptable if noted in the report.

---

## 9. REST surface added/used in Part 4

| Method | Path | Role |
|--------|------|------|
| GET | `/api/v1/guard` | Armed? |
| GET | `/api/v1/watchdog` | Enabled? |
| GET | `/api/v1/thermal` | Enabled? |
| GET | `/api/v1/camera` | Enabled? |
| GET | `/api/v1/part4` | Combined snapshot for UI |
| GET | `/api/v1/blackbox` | Recorder stats |
| POST | `/api/v1/command` | All `*_on` / `*_off` cmds |

Command names are listed in the 400 hint / gateway OpenAPI examples.

---

## 10. Persistence helpers

### 10.1 `guard_state.c`

File: `../data/guard_state.json`. Mutex-protected in-memory cache + disk write so `guard_on` survives process restart until explicitly disarmed.

### 10.2 `feature_flags.c`

Files: `../data/watchdog_state.json`, `../data/thermal_state.json`. Same pattern; missing file ⇒ feature **ON**.

### 10.3 `camera_state.c`

File: `../data/camera_state.json`. Bridge from C command handler to Python. Detector also keeps an in-memory copy (and re-reads the file) so UI toggles apply without relying only on a stale cache.

---

## 11. Timing / rate limits (coordinator)

| Concern | Interval |
|---------|----------|
| Coordinator loop | `sleep(1)` |
| Guard email min gap | 2 s |
| Watchdog email min gap | ~45 s |
| Thermal email min gap | ~60 s |
| Rewrite thermal JSON while stable | ~15 s |
| Reload Part 4 cfg from env | every ~60 ticks |

These limits protect SMTP and journal spam during demos.

---

## 12. Failure modes & design rationale

| Problem | Design choice |
|---------|----------------|
| Fork-per-HTTP + MQTT threads | Use pthreads for HTTP + Part 4 |
| Thermal sleep froze video | Skip YOLO frames instead |
| Heartbeat on idle fooled WD | Touch `ts` only on real frames |
| USB camera death | `systemctl restart human_detector` |
| Guard vs presence spam | Edge trigger + separate debounce |

---

## 13. Configuration keys (Part 4)

```env
THERMAL_TEMP_C=85
THERMAL_CLEAR_C=78
WATCHDOG_SEC=30
STUDENT_ID=402102657
```

Email/MQTT settings from Part 3 still required for Guard/watchdog/thermal notifications and `…/alarm`.

---

## 14. End-to-end scenarios

### 14.1 Guard

Armed → person enters → count↑ → email+photo + MQTT alarm within ~1–2 s poll.

### 14.2 Black box

Several detections → rows in `history.db` → `/api/v1/blackbox` shows totals/capacity.

### 14.3 Watchdog

Camera on + WD on → unplug cam → heartbeat stale → mail + restart → replug → stream returns.

### 14.4 Thermal

Thermal on → `stress-ng` → temp≥85 → `thermal_control.json` level≥1 → overlay THR / lower detect rate → cool below 78 → level 0.

---

## 15. How Part 4 depends on earlier parts

| Needs from… | What |
|-------------|------|
| Part 1 | Always-on C process + systemd |
| Part 2 | Telemetry temp, REST command bus, dashboard |
| Part 3 | persons.json, snapshots, email/MQTT stacks, detector loop |

Part 4 is policy + recording on top of that substrate.

---

## 16. Quick reference

```bash
curl -sk https://127.0.0.1:8443/api/v1/part4

curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' -d '{"cmd":"guard_on"}'
curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' -d '{"cmd":"watchdog_on"}'
curl -sk -X POST https://127.0.0.1:8443/api/v1/command \
  -H 'Content-Type: application/json' -d '{"cmd":"thermal_on"}'

mosquitto_sub -h 127.0.0.1 -u smartguard -P 'smartguard' \
  -t 'home/402102657/alarm' -v

journalctl -u web_server -u human_detector -f
```

---

## 17. Summary

Part 4 implements four embedded “appliance” behaviours in one C coordinator thread:

1. **Guard** — count-increase anti-theft alarms (mail + MQTT).  
2. **Black box** — bounded SQLite history exposed over REST.  
3. **Watchdog** — stale frame heartbeat → tamper mail + service restart.  
4. **Thermal** — hysteresis throttling via shared JSON without freezing the stream.

Together they complete the Smart Guard System as specified in the course PDF’s fourth part.
