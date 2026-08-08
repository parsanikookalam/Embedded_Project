#Requires -RunAsAdministrator
<#
  One-time: bind webcam + register Windows logon task so camera auto-attaches to WSL.
#>
param(
    [string]$BusId = "1-7",
    [string]$WslDistro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$Attach = Join-Path $Here "attach_camera_to_wsl.ps1"
$BusFile = Join-Path $Here "camera.busid"

if (-not (Get-Command usbipd -ErrorAction SilentlyContinue)) {
    Write-Host "Installing usbipd-win..."
    winget install --id dorssel.usbipd-win -e --accept-package-agreements --accept-source-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("Path","User")
}

Write-Host "Current devices:"
usbipd list

Set-Content -Path $BusFile -Value $BusId -NoNewline
usbipd bind --busid $BusId

# Register logon autostart
$action = New-ScheduledTaskAction -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$Attach`" -BusId $BusId -WslDistro $WslDistro"
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
Register-ScheduledTask -TaskName "SmartGuard-WSL-Webcam" -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force | Out-Null

Write-Host "Registered Task: SmartGuard-WSL-Webcam"
Write-Host "Attaching now..."
& $Attach -BusId $BusId -WslDistro $WslDistro

Write-Host ""
Write-Host "Done. After reboot/login, camera attaches automatically."
Write-Host "WSL dashboard: https://127.0.0.1:8443/"
