# Install Smart Guard host CPU temp updater (Windows-local only).
# Updates every 2 seconds (PDF telemetry interval) with NO visible console.
#
# Run in Windows PowerShell:
#   cd \\wsl$\Ubuntu\home\parsa\embedded_project\scripts\windows
#   powershell -ExecutionPolicy Bypass -File .\setup_host_temp_task.ps1

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SrcPs1 = Join-Path $Root "host_cpu_temp.ps1"
$SrcVbs = Join-Path $Root "run_hidden.vbs"
$InstallDir = Join-Path $env:LOCALAPPDATA "SmartGuard"
$InstalledPs1 = Join-Path $InstallDir "host_cpu_temp.ps1"
$InstalledVbs = Join-Path $InstallDir "run_hidden.vbs"
$OutFile = Join-Path $InstallDir "cpu_temp.txt"
$PsExe = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$Wscript = Join-Path $env:SystemRoot "System32\wscript.exe"

$TaskLoop = "SmartGuard-HostCpuTempLoop"
$TaskMin = "SmartGuard-HostCpuTemp"

function Remove-TaskQuiet([string]$Name) {
    Unregister-ScheduledTask -TaskName $Name -Confirm:$false -ErrorAction SilentlyContinue | Out-Null
}

if (-not (Test-Path -LiteralPath $SrcPs1)) { throw "Missing $SrcPs1" }
if (-not (Test-Path -LiteralPath $SrcVbs)) { throw "Missing $SrcVbs" }

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
Copy-Item -LiteralPath $SrcPs1 -Destination $InstalledPs1 -Force
Copy-Item -LiteralPath $SrcVbs -Destination $InstalledVbs -Force

Write-Host "Installed: $InstalledPs1"
Write-Host "Out file : $OutFile"
Write-Host "Probing once..."
& $PsExe -NoProfile -ExecutionPolicy Bypass -File $InstalledPs1 -OutFile $OutFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "WARNING: probe failed - sensor may be unavailable."
} else {
    Write-Host "Probe OK: $(Get-Content -LiteralPath $OutFile -Raw)"
}

Remove-TaskQuiet $TaskLoop
Remove-TaskQuiet $TaskMin

$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

# Hidden continuous loop every 2s (matches dashboard poll)
# wscript + run_hidden.vbs => no console window at all
$inner = "$PsExe -NoProfile -ExecutionPolicy Bypass -File `"$InstalledPs1`" -Loop -IntervalSec 2 -OutFile `"$OutFile`""
$loopArgs = "`"$InstalledVbs`" `"$inner`""
$loopAction = New-ScheduledTaskAction -Execute $Wscript -Argument $loopArgs
$loopTrigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$loopSettings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew `
    -Hidden

Register-ScheduledTask -TaskName $TaskLoop -Action $loopAction -Trigger $loopTrigger `
    -Settings $loopSettings -Principal $principal -Force | Out-Null

# Start now (also via hidden wscript)
Start-Process -FilePath $Wscript -ArgumentList @("`"$InstalledVbs`"", "`"$inner`"") -WindowStyle Hidden

# Backup every 1 minute (also hidden)
$minInner = "$PsExe -NoProfile -ExecutionPolicy Bypass -File `"$InstalledPs1`" -OutFile `"$OutFile`""
$minArgs = "`"$InstalledVbs`" `"$minInner`""
$minAction = New-ScheduledTaskAction -Execute $Wscript -Argument $minArgs
$minStart = (Get-Date).AddMinutes(1)
$minTrigger = New-ScheduledTaskTrigger -Once -At $minStart -RepetitionInterval (New-TimeSpan -Minutes 1) -RepetitionDuration (New-TimeSpan -Days 3650)
$minSettings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 3) `
    -Hidden

Register-ScheduledTask -TaskName $TaskMin -Action $minAction -Trigger $minTrigger `
    -Settings $minSettings -Principal $principal -Force | Out-Null

Start-Sleep -Seconds 3

Write-Host ""
Write-Host "Task states:"
Get-ScheduledTask -TaskName $TaskLoop, $TaskMin -ErrorAction SilentlyContinue |
    ForEach-Object { "  $($_.TaskName) = $($_.State)" }
Write-Host ""
Write-Host "Temp file (should refresh about every 2s):"
Get-Item -LiteralPath $OutFile | Format-List FullName, LastWriteTime
Get-Content -LiteralPath $OutFile
Write-Host ""
Write-Host "No empty terminal should pop up. Watch:"
Write-Host '  1..6 | % { (Get-Item $env:LOCALAPPDATA\SmartGuard\cpu_temp.txt).LastWriteTime; Start-Sleep 2 }'
Write-Host ""
Write-Host "Then in WSL (if web_server not rebuilt recently):"
Write-Host "  make -C ~/embedded_project/web && sudo systemctl restart web_server"
