# Leave OPEN — MQTT listener for Part 3-6 success evidence
# powershell -ExecutionPolicy Bypass -File \\wsl$\Ubuntu\home\parsa\embedded_project\scripts\windows\mqtt_listen.ps1

$Distro = "Ubuntu"
Write-Host "MQTT listen home/402102657/#  (Ctrl+C to stop)"
Write-Host "In another PowerShell run mqtt_test_auth.ps1 -Mode ok"
wsl -d $Distro -- bash -lc "systemctl is-active mosquitto_smartguard >/dev/null || bash ~/embedded_project/scripts/setup_mosquitto.sh; mosquitto_sub -h 127.0.0.1 -p 1883 -u smartguard -P smartguard -t 'home/402102657/#' -v"
