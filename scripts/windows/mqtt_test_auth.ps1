# Smart Guard — MQTT auth tests from Windows via WSL (Part 3-6)
# Terminal A (SUCCESS subscribe — leave open):
#   powershell -ExecutionPolicy Bypass -File ...\mqtt_listen.ps1
# Terminal B (publish OK / FAIL):
#   powershell -ExecutionPolicy Bypass -File ...\mqtt_test_auth.ps1

param(
  [string]$Distro = "Ubuntu",
  [ValidateSet("listen", "ok", "fail", "all")]
  [string]$Mode = "all"
)

$ErrorActionPreference = "Continue"
$proj = "/home/parsa/embedded_project"

function Invoke-WslBash([string]$Cmd) {
  wsl -d $Distro -- bash -lc $Cmd
}

Write-Host "==> Ensure Mosquitto broker..."
Invoke-WslBash "cd $proj && (systemctl is-active mosquitto_smartguard >/dev/null || bash scripts/setup_mosquitto.sh)"

$USER = "smartguard"
$PASS = "smartguard"
$ID = "402102657"

switch ($Mode) {
  "listen" {
    Write-Host "=== LISTEN (leave this terminal open) topic home/$ID/# ==="
    Write-Host "When authorized pubs arrive you should see JSON lines."
    Invoke-WslBash "mosquitto_sub -h 127.0.0.1 -p 1883 -u $USER -P '$PASS' -t 'home/$ID/#' -v"
  }
  "ok" {
    Write-Host "=== SUCCESS publish (screenshot → fig/09a_mqtt_auth_ok.png) ==="
    Invoke-WslBash "mosquitto_pub -h 127.0.0.1 -p 1883 -u $USER -P '$PASS' -t 'home/$ID/persons' -m '{\`"ok\`":1,`"from\`":\`"windows\`"}' -q 1 -d"
  }
  "fail" {
    Write-Host "=== FAIL anonymous (screenshot → fig/09b_mqtt_auth_fail.png) ==="
    Invoke-WslBash "mosquitto_pub -h 127.0.0.1 -p 1883 -t 'test/anon' -m 'x' -d; echo EXIT:`$?"
    Write-Host "=== FAIL wrong password ==="
    Invoke-WslBash "mosquitto_pub -h 127.0.0.1 -p 1883 -u $USER -P 'wrongpass' -t 'test/bad' -m 'x' -d; echo EXIT:`$?"
  }
  "all" {
    Write-Host "=== SUCCESS (authorized) ==="
    Invoke-WslBash "mosquitto_pub -h 127.0.0.1 -p 1883 -u $USER -P '$PASS' -t 'home/$ID/persons' -m '{\`"ok\`":1}' -q 1 -d"
    Write-Host ""
    Write-Host "=== FAIL (anonymous) ==="
    Invoke-WslBash "mosquitto_pub -h 127.0.0.1 -p 1883 -t 'test/anon' -m 'x' -d || true"
    Write-Host ""
    Write-Host "=== FAIL (wrong password) ==="
    Invoke-WslBash "mosquitto_pub -h 127.0.0.1 -p 1883 -u $USER -P 'wrongpass' -t 'test/bad' -m 'x' -d || true"
    Write-Host ""
    Write-Host "Save screenshots:"
    Write-Host "  success → report/part 3/fig/09a_mqtt_auth_ok.png"
    Write-Host "  fail    → report/part 3/fig/09b_mqtt_auth_fail.png"
  }
}
