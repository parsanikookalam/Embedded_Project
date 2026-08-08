# Part 1 — Embedded Web Server & Dashboard

**Course:** Final Project — Embedded Systems  
**Project:** Smart Guard System  
**Student:** Parsa Nikookalam  
**Student ID:** 402102657  
**Target:** WSL Ubuntu Linux (accepted Linux VM path; same design as Orange Pi)  

---

## 1. Introduction

Part 1 requires a **native C web server** that serves a secure web dashboard for the Smart Guard System. According to the project brief, the core application logic must be implemented in **C**. Python is reserved for later vision processing (Part 3+).

This part delivers:

1. An HTTP service that permanently redirects clients to HTTPS  
2. An HTTPS service using a **self-signed TLS certificate** whose Common Name (CN) is the student ID  
3. A browser dashboard served from the C process  
4. Automatic start of the web server after reboot via **systemd**

---

## 2. Objectives (from project PDF)

| Requirement | Status |
|-------------|--------|
| C language web server | Done |
| Serve HTML dashboard | Done |
| Live camera area on dashboard (via later stream API) | Done (wired to `/api/v1/stream`) |
| HTTPS with self-signed certificate | Done |
| Certificate CN = student ID (`402102657`) | Done |
| HTTP → HTTPS redirect | Done |
| systemd unit / auto-start | Done |

---

## 3. System architecture (Part 1)

```
Browser
   │
   ├─ http://127.0.0.1:8080/  ──301──►  https://127.0.0.1:8443/
   │
   └─ https://127.0.0.1:8443/  ──TLS──►  web_server (C)
                                              │
                                              ├─ www/index.html  (dashboard)
                                              ├─ www/server.crt / server.key
                                              └─ (later parts: REST, stream proxy…)
```

On a physical Orange Pi the same design can use ports **80/443**. On WSL those ports are often reserved by Windows, so this deployment uses:

- `HTTP_PORT=8080`
- `HTTPS_PORT=8443`

Configured in `config.env`.

---

## 4. Implementation

### 4.1 Project layout (web)

```
web/
├── Makefile
├── web_server              # built binary
├── include/
│   └── http_server.h
├── src/
│   ├── main.c              # entry: start services, then HTTP/HTTPS
│   └── server.c            # sockets, TLS, redirect, HTML, APIs
└── www/
    ├── index.html          # dashboard
    ├── server.crt          # self-signed certificate
    └── server.key          # private key
```

### 4.2 Building the C server

```bash
cd ~/embedded_project/web
make clean all
```

Dependencies: `gcc`, OpenSSL development libraries (`libssl-dev`).

### 4.3 Self-signed TLS certificate (CN = student ID)

Certificate generation is automated by `scripts/gen_ssl.sh`:

- Algorithm: RSA 2048  
- Validity: 365 days  
- Subject: `CN=402102657`, `O=Smart Guard System`, `C=IR`  
- Output: `web/www/server.crt`, `web/www/server.key`

Verify:

```bash
openssl x509 -in ~/embedded_project/web/www/server.crt -noout -subject
# subject=CN = 402102657, O = Smart Guard System, C = IR
```

**Figure 1 — Certificate subject (CN = student ID)**

- **What to run in terminal (WSL):**
  ```bash
  openssl x509 -in ~/embedded_project/web/www/server.crt -noout -subject
  ```
- **How to take the picture:** Keep the WSL terminal window visible so the full `subject=CN = 402102657, …` line is on screen. Take a Windows screenshot (`Win + Shift + S` or `PrtSc`), crop to the terminal output, and save as:
  `report/part 1/fig/01_cert_subject.png`

![Figure 1 — Certificate subject (CN=student ID)](fig/01_cert_subject.png)

Because the certificate is self-signed, browsers show a security warning. For the course demo, proceed by accepting the exception (or use `curl -k`).

### 4.4 HTTP → HTTPS redirect

A dedicated thread listens on the HTTP port. For each request it answers:

```http
HTTP/1.1 301 Moved Permanently
Location: https://<Host>:8443/
Connection: close
```

The `Host` header is parsed so the redirect works for `127.0.0.1` and LAN names.

**Figure 2 — HTTP 301 redirect to HTTPS**

- **What to run in terminal (WSL):**  
  First make sure the web server is running:
  ```bash
  systemctl is-active web_server
  curl -v http://127.0.0.1:8080/
  ```
  You should see `HTTP/1.1 301` and a `Location: https://127.0.0.1:8443/` (or similar) header.
- **How to take the picture:** Screenshot the terminal region that shows the `301` status line and the `Location:` header. Save as:
  `report/part 1/fig/02_http_redirect.png`

![Figure 2 — curl -v showing 301 to HTTPS](fig/02_http_redirect.png)

### 4.5 HTTPS dashboard

The HTTPS listener:

1. Creates an OpenSSL `SSL_CTX`  
2. Loads `www/server.crt` and `www/server.key`  
3. Accepts connections with TLS  
4. Serves `www/index.html` for `GET /`  

The dashboard shows:

- Student name and ID (from `/api/v1/config`)  
- Live stream panel (`/api/v1/stream`)  
- Telemetry widgets (filled by Part 2+)  
- Part 4 control buttons (added later; HTML still served by Part 1’s C server)

**Figure 3 — HTTPS dashboard in the browser**

- **What to run in terminal (WSL):** optional check that HTTPS answers:
  ```bash
  systemctl is-active web_server
  curl -sk -o /dev/null -w "%{http_code}\n" https://127.0.0.1:8443/
  # expect: 200
  ```
- **How to take the picture:** Open **Chrome/Edge** on Windows and go to  
  `https://127.0.0.1:8443/`  
  If the browser warns about a self-signed certificate, click **Advanced → Proceed** (unsafe). When the Smart Guard page loads (name + student ID visible), screenshot the browser window and save as:
  `report/part 1/fig/03_dashboard.png`

![Figure 3 — Smart Guard dashboard over HTTPS](fig/03_dashboard.png)

**Figure 3b (optional) — Browser certificate warning**

- **What to run:** none (browser only).
- **How to take the picture:** On first visit to `https://127.0.0.1:8443/`, capture the “Your connection is not private” / certificate warning screen before clicking Proceed. Save as:
  `report/part 1/fig/05_optional_browser_cert_warning.png`

![Figure 3b — Self-signed cert warning (optional)](fig/05_optional_browser_cert_warning.png)

### 4.6 Concurrent clients (threading)

Each HTTPS connection is handled on a **detached pthread** so a long-lived MJPEG stream does not block telemetry or other clients. (Earlier fork-per-request caused instability with MQTT/email worker threads; threading is the production approach in this project.)

### 4.7 systemd auto-start

Unit file: `services/web_server.service`

Important settings:

| Key | Value |
|-----|--------|
| `WorkingDirectory` | `/home/parsa/embedded_project/web` |
| `EnvironmentFile` | `…/config.env` |
| `ExecStart` | `…/web/web_server` |
| `Restart` | `always` |
| Ordering | After `network` / vision service |

Install / enable:

```bash
sudo cp ~/embedded_project/services/web_server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now web_server
systemctl status web_server --no-pager
```

**Figure 4 — systemd unit running**

- **What to run in terminal (WSL):**
  ```bash
  systemctl status web_server --no-pager
  ```
  Confirm lines like `Active: active (running)` and `Loaded: … enabled`.
- **How to take the picture:** Screenshot the terminal output of that status command (include the green `active (running)` if shown). Save as:
  `report/part 1/fig/04_systemd_web_server.png`

![Figure 4 — systemctl status web_server](fig/04_systemd_web_server.png)

---

## 5. Configuration

Relevant `config.env` keys for Part 1:

```env
STUDENT_ID=402102657
STUDENT_NAME="Parsa Nikookalam"
TARGET=wsl
HTTP_PORT=8080
HTTPS_PORT=8443
```

The C server reads these at startup from `../config.env` (relative to `WorkingDirectory=web`).

---

## 6. Test procedure & results

### Test 1.1 — Certificate CN

| Step | Action | Expected |
|------|--------|----------|
| 1 | `openssl x509 -in web/www/server.crt -noout -subject` | CN contains `402102657` |
| Result | ☐ Pass / ☐ Fail | |

### Test 1.2 — HTTP redirect

| Step | Action | Expected |
|------|--------|----------|
| 1 | `curl -v http://127.0.0.1:8080/` | `301` and `Location: https://…:8443/` |
| Result | ☐ Pass / ☐ Fail | |

### Test 1.3 — HTTPS dashboard

| Step | Action | Expected |
|------|--------|----------|
| 1 | Browse `https://127.0.0.1:8443/` | Dashboard HTML loads over TLS |
| 2 | Page title / header | Shows student name and ID |
| Result | ☐ Pass / ☐ Fail | |

### Test 1.4 — systemd persistence

| Step | Action | Expected |
|------|--------|----------|
| 1 | `systemctl is-enabled web_server` | `enabled` |
| 2 | `sudo systemctl restart web_server` | Active (running) |
| 3 | (Optional) reboot WSL and re-check | Service starts automatically |
| Result | ☐ Pass / ☐ Fail | |

---

## 7. Figures checklist

Put image files in `report/part 1/fig/`. For each figure, the report section above lists **what to run** and **how to capture**.

| File | Terminal command (if any) | How to capture |
|------|---------------------------|----------------|
| `fig/01_cert_subject.png` | `openssl x509 -in ~/embedded_project/web/www/server.crt -noout -subject` | Screenshot WSL terminal showing CN |
| `fig/02_http_redirect.png` | `curl -v http://127.0.0.1:8080/` | Screenshot `301` + `Location:` lines |
| `fig/03_dashboard.png` | optional: `curl -sk -o /dev/null -w "%{http_code}\n" https://127.0.0.1:8443/` | Screenshot browser at `https://127.0.0.1:8443/` |
| `fig/04_systemd_web_server.png` | `systemctl status web_server --no-pager` | Screenshot status output |
| `fig/05_optional_browser_cert_warning.png` | *(none)* | Screenshot browser cert warning page |

---

## 8. Discussion

Part 1 establishes the always-on secure web front-end for Smart Guard. Using **C + OpenSSL** satisfies the course requirement that the application core is not a Python web framework. WSL port mapping (8080/8443) preserves the same redirect/TLS design that would use 80/443 on an Orange Pi. systemd ensures the dashboard comes up after reboot without manual intervention—important for an embedded “appliance” style submission.

Later parts reuse this same C process for REST APIs (Part 2), email/MQTT (Part 3), and guard/watchdog/thermal coordination (Part 4).

---

## 9. Conclusion

Part 1 is complete:

- C HTTPS web server with self-signed cert **CN = 402102657**  
- HTTP → HTTPS redirect  
- HTML dashboard served securely  
- systemd unit with auto-restart  

---

## 10. References

- Course PDF: *Final Project — Embedded Systems (Smart Guard System)*  
- OpenSSL documentation (self-signed certificates, `TLS_server_method`)  
- systemd.service(5)  
- Project sources: `web/src/server.c`, `scripts/gen_ssl.sh`, `services/web_server.service`
