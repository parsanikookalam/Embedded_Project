#Requires -RunAsAdministrator
<#
  Attach laptop webcam (usbipd) into WSL and keep it attached.
  Used by Task Scheduler at Windows logon for fully automatic camera.
#>
param(
    [string]$BusId = "",
    [string]$WslDistro = "Ubuntu"
)

$ErrorActionPreference = "Continue"
$Root = Split-Path -Parent $PSScriptRoot
$BusFile = Join-Path $PSScriptRoot "camera.busid"

if (-not $BusId -and (Test-Path $BusFile)) {
    $BusId = (Get-Content $BusFile -Raw).Trim()
}
if (-not $BusId) {
    $BusId = "1-7"  # default from this machine's UVC webcam
}

if (-not (Get-Command usbipd -ErrorAction SilentlyContinue)) {
    Write-Error "usbipd not installed (winget install dorssel.usbipd-win)"
    exit 1
}

# Ensure WSL is running
wsl -d $WslDistro -e true 2>$null

# Stop any previous attach loop for this busid (best-effort)
Get-CimInstance Win32_Process -Filter "Name='usbipd.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -match [regex]::Escape($BusId) } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

Start-Sleep -Seconds 1
usbipd bind --busid $BusId 2>$null

# Persistent auto-attach (keeps cam in WSL across reconnects). Hidden window.
$arg = "attach --wsl --busid $BusId --auto-attach"
Start-Process -FilePath "usbipd.exe" -ArgumentList $arg -WindowStyle Hidden

# Give WSL a moment, then bounce detector so it picks camera quickly
Start-Sleep -Seconds 3
wsl -d $WslDistro -e bash -lc "sudo systemctl restart human_detector.service" 2>$null

Write-Host "Attached webcam BusId=$BusId to WSL (auto-attach running in background)."
exit 0
