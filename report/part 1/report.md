# Part 1 — Mandatory Experiments Report

**Course:** Final Project — Embedded Systems  
**Project:** Smart Guard System  
**Student:** Parsa Nikookalam  
**Student ID:** 402102657  
**Platform:** WSL Ubuntu (Linux VM path accepted instead of Orange Pi)  

This section follows the PDF table **“Mandatory experiments of the first part”** in order (**1-1 … 6-1**). Each experiment number, what was done, and the expected report output are given below.

**Ports on WSL:** HTTP `8080`, HTTPS `8443` (same behaviour as 80/443 on Orange Pi).

---

## Experiment 1-1 — Boot time with `systemd-analyze blame`

### What the PDF asks
Reboot the board and measure boot time with `systemd-analyze blame`. The report should include a **table of total boot time** and the **share of your services** (terminal screenshot).

### What I did / platform note (WSL)
This project runs on **WSL Ubuntu**, not a physical Orange Pi board. On WSL:

- “Boot” is a **lightweight VM start**, not a full embedded board cold boot.
- It finishes **very fast**, so there is **no useful slow boot timeline** to capture the way you would on Orange Pi (U-Boot → kernel → userspace).
- Because of that, a meaningful “board reboot + blame table” demo **cannot be shown the same way** as on real hardware.

**For auto-start after power cycle, please see Experiment 1-3 (video).** That video is the evidence that the application comes up by itself without keyboard interaction. Experiment 1-1 is limited here only because WSL boots too quickly to demonstrate as a board reboot.

### Commands to run anyway (optional WSL evidence)
```bash
# After a WSL restart (from Windows: wsl --shutdown, then open Ubuntu again):
systemd-analyze
systemd-analyze blame | head -n 40
systemctl is-enabled web_server
systemctl is-active web_server
```

You can fill a small table from the output if available:

| Metric | Value (fill from terminal) |
|--------|----------------------------|
| Total boot (`systemd-analyze`) | … |
| `web_server.service` time in blame | … |
| Other project units (`human_detector`, `api_gateway`) | … |

**Figure 1-1 (optional on WSL)** — if you capture anything:

- **Command:**
  ```bash
  systemd-analyze
  systemd-analyze blame | head -n 40
  ```
- **Screenshot file:** `report/part 1/fig/01_boot_blame.png`

![Figure 1-1 — Boot analysis (optional on WSL; see video in 1-3)](fig/01_boot_blame.png)

**Result:** Partially not applicable on WSL due to very fast boot → **auto-start proof deferred to Experiment 1-3 video**.

---

## Experiment 1-2 — Kill web server with `kill -9` (auto-restart)

### What the PDF asks
Kill the web server process with `kill -9`. The report must show a **`journalctl` screenshot** proving the service **restarted automatically**.

### What happens
`web_server.service` has `Restart=always`. After `kill -9` on the process PID, systemd starts a new process.

### What to run
```bash
# Find PID and kill it
systemctl show -p MainPID --value web_server
PID=$(systemctl show -p MainPID --value web_server)
sudo kill -9 "$PID"

# Wait a second, then check logs + status
sleep 2
journalctl -u web_server -n 30 --no-pager
systemctl status web_server --no-pager
```

In the log you should see the old process killed and a **new start** (active again).

**Figure 1-2 — journalctl after `kill -9`**

- **What to run:** the commands above.
- **How to take the picture:** Screenshot the `journalctl -u web_server` output that shows the crash/kill and **automatic restart**, plus `Active: active (running)`. Save as:
  `report/part 1/fig/02_kill9_restart.png`

![Figure 1-2 — Automatic restart after kill -9](fig/02_kill9_restart.png)

**Result:** ☐ Pass / ☐ Fail  

---

## Experiment 1-3 — Power off / power on once (auto-start video)

### What the PDF asks
Turn the board **off** and **on** once. Deliver a **video** showing the system boot and the **application starting automatically** with **no keyboard input**.

### What to do on WSL (instead of unplugging Orange Pi)
1. Stop WSL completely from **Windows PowerShell** or CMD:
   ```powershell
   wsl --shutdown
   ```
2. Start recording (Phone camera or Windows Game Bar `Win + G`, or OBS).
3. Open **Ubuntu (WSL)** from the Start menu — **do not type any project commands**.
4. In the new terminal, only check that the service is already running (optional on camera):
   ```bash
   systemctl is-active web_server
   # or open browser to https://127.0.0.1:8443/
   ```
5. Stop recording. Place the file next to this report, for example:
   `report/part 1/fig/03_autostart.mp4`

### Why this replaces 1-1 for boot proof
Because WSL boot is too fast to present a useful `systemd-analyze blame` board demo (Experiment **1-1**), **this video is the main evidence** that the stack **auto-starts after power-off / power-on** without manual start commands.

**Figure / Media 1-3 — Autostart video**

- **How to capture:** Record the full sequence: shutdown → start WSL → dashboard or `systemctl is-active` without typing `./web_server` or `systemctl start`.
- **File:** `report/part 1/fig/03_autostart.mp4`  
  (If your course portal accepts a Drive/YouTube link, put the link here too.)

**Video link / path:** _________________________________

**Result:** ☐ Pass / ☐ Fail  

---

## Experiment 1-4 — Open server with `http` (301 → HTTPS)

### What the PDF asks
Access the server with **`http`** (not `https`). Using the browser **Inspect** tool, show a screenshot of the **redirect to HTTPS** with status **HTTP 301**.

### What happens
HTTP listener on port **8080** answers:

```http
HTTP/1.1 301 Moved Permanently
Location: https://127.0.0.1:8443/
```

### What to do in the browser
1. Open Chrome/Edge DevTools: **F12** → **Network** tab.
2. Enable **Preserve log**.
3. Go to:
   ```text
   http://127.0.0.1:8080/
   ```
4. Click the first document request. Status should be **301**.  
   Response headers / next request should go to **`https://127.0.0.1:8443/`**.

Optional terminal check:
```bash
curl -v http://127.0.0.1:8080/
```

**Figure 1-4 — Browser Inspect: HTTP 301 to HTTPS**

- **What to open:** `http://127.0.0.1:8080/` with Network tab open.
- **How to take the picture:** Screenshot DevTools showing status **301** and redirect/Location to HTTPS. Save as:
  `report/part 1/fig/04_http_301_inspect.png`

![Figure 1-4 — Inspect Network: 301 redirect](fig/04_http_301_inspect.png)

**Result:** ☐ Pass / ☐ Fail  

---

## Experiment 1-5 — Open self-signed certificate (CN = student ID)

### What the PDF asks
Open the self-signed certificate of the HTML page. Screenshot the certificate details and show that **Common Name (CN)** contains the **student ID**.

### What we show
Certificate Viewer for the site certificate:

- **Common Name (CN):** `402102657`
- **Organization (O):** Smart Guard System  
- Self-signed (Issued To = Issued By)

**How the screenshot was taken (for reproducibility):**
1. Open `https://127.0.0.1:8443/`
2. Click the padlock / “Not secure” → **Certificate** (or Connection is secure → Certificate is valid)
3. On the **General** tab, show **Common Name (CN): 402102657**

**Figure 1-5 — Certificate details (CN = student ID)**

File name required for this experiment: `fig/05_cert_subject.png`  
(If you still only have `01_cert_subject.png`, rename it once in WSL:)

```bash
cd ~/embedded_project/"report/part 1"/fig
cp -f 01_cert_subject.png 05_cert_subject.png
```

![Figure 1-5 — Certificate Viewer: CN = 402102657](fig/05_cert_subject.png)

**Result:** Pass — CN is **402102657** (student ID). See figure above.

---

## Experiment 1-6 — Open the HTML page (student ID visible)

### What the PDF asks
Open the HTML page. Screenshot the rendered webpage; it must include the **student ID**.

### What to do
1. Open:
   ```text
   https://127.0.0.1:8443/
   ```
2. Accept the self-signed warning if asked (**Advanced → Proceed**).
3. Confirm the dashboard shows student identity (**402102657** / Parsa Nikookalam).

**Figure 1-6 — HTML dashboard with student ID**

- **How to take the picture:** Full browser window of the Smart Guard page with student ID visible. Save as:
  `report/part 1/fig/06_html_dashboard.png`

![Figure 1-6 — Dashboard HTML page with student ID](fig/06_html_dashboard.png)

**Result:** ☐ Pass / ☐ Fail  

---

## Summary table (mandatory experiments)

| No. | Experiment | Expected in report | Evidence file | Status |
|-----|------------|--------------------|---------------|--------|
| **1-1** | Reboot + `systemd-analyze blame` | Boot time table + service share screenshot | `fig/01_boot_blame.png` (optional) | **N/A on WSL (boot too fast)** → see **1-3 video** |
| **1-2** | `kill -9` web server | `journalctl` auto-restart screenshot | `fig/02_kill9_restart.png` | ☐ |
| **1-3** | Power off / on once | Autostart **video** (no keyboard) | `fig/03_autostart.mp4` | ☐ |
| **1-4** | Open with `http` | Browser Inspect: **301** → HTTPS | `fig/04_http_301_inspect.png` | ☐ |
| **1-5** | Open self-signed cert | Cert details, **CN = student ID** | `fig/05_cert_subject.png` | **Done** |
| **1-6** | Open HTML page | Webpage screenshot with student ID | `fig/06_html_dashboard.png` | ☐ |

---

## Figures / media checklist

| File | Experiment |
|------|------------|
| `fig/01_boot_blame.png` | 1-1 (optional on WSL) |
| `fig/02_kill9_restart.png` | 1-2 |
| `fig/03_autostart.mp4` | 1-3 (**required video**) |
| `fig/04_http_301_inspect.png` | 1-4 |
| `fig/05_cert_subject.png` | 1-5 (**your CN certificate picture**) |
| `fig/06_html_dashboard.png` | 1-6 |

---

## Short technical notes (supporting the experiments)

1. **C web server:** `web/web_server` (OpenSSL HTTPS + HTTP redirect thread).  
2. **HTML:** `web/www/index.html` served on `GET /`.  
3. **TLS:** self-signed cert from `scripts/gen_ssl.sh`, CN = `402102657`.  
4. **systemd:** `services/web_server.service` with `Restart=always` and `enable` on boot.

---

## References

- Course PDF: *Mandatory experiments of the first part* (table 1-1 … 6-1)  
- Sources: `web/src/server.c`, `services/web_server.service`, `scripts/gen_ssl.sh`
