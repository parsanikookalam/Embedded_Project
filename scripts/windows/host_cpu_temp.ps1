# Smart Guard - host CPU / ACPI temperature helper (run on Windows).
# Prints Celsius to stdout and writes OutFile for WSL (/mnt/c/.../SmartGuard/cpu_temp.txt).

param(
    [switch]$Loop,
    [string]$OutFile = "$env:LOCALAPPDATA\SmartGuard\cpu_temp.txt",
    [int]$IntervalSec = 2,
    [switch]$Debug,
    [switch]$Thorough
)

$ErrorActionPreference = "SilentlyContinue"

function Test-Celsius([double]$c) {
    return ($c -gt 5.0 -and $c -lt 115.0)
}

function Get-HostCelsius {
    $vals = New-Object System.Collections.Generic.List[double]

    # Fast: ACPI WMI (tenths of Kelvin)
    try {
        Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature | ForEach-Object {
            $c = ([double]$_.CurrentTemperature / 10.0) - 273.15
            if (Test-Celsius $c) { [void]$vals.Add($c) }
        }
    } catch {}

    # Fast: English thermal counter path
    try {
        $samples = (Get-Counter -Counter '\Thermal Zone Information(*)\Temperature' -ErrorAction Stop).CounterSamples
        foreach ($s in $samples) {
            $c = [double]$s.CookedValue - 273.15
            if (Test-Celsius $c) { [void]$vals.Add($c) }
        }
    } catch {}

    # Fast: perf formatted class
    try {
        Get-CimInstance Win32_PerfFormattedData_Counters_ThermalZoneInformation | ForEach-Object {
            $c = [double]$_.Temperature - 273.15
            if (Test-Celsius $c) { [void]$vals.Add($c) }
        }
    } catch {}

    # Optional: LibreHardwareMonitor
    try {
        Get-CimInstance -Namespace root/LibreHardwareMonitor -ClassName Sensor |
            Where-Object { $_.SensorType -eq 'Temperature' -and $_.Name -match 'CPU|Package|Tctl|Core' } |
            ForEach-Object {
                $c = [double]$_.Value
                if (Test-Celsius $c) { [void]$vals.Add($c) }
            }
    } catch {}

    # Slow path only if needed (enumerating all counter sets is expensive)
    if (($vals.Count -eq 0) -or $Thorough) {
        try {
            $sets = Get-Counter -ListSet * | Where-Object {
                $_.PathsWithInstances -match 'Temperature' -or
                $_.CounterSetName -match 'Thermal|Zone'
            }
            foreach ($set in $sets) {
                try {
                    $paths = @($set.PathsWithInstances)
                    if (-not $paths -or $paths.Count -eq 0) { $paths = @($set.Paths) }
                    if (-not $paths) { continue }
                    $samples = (Get-Counter -Counter $paths -MaxSamples 1).CounterSamples
                    foreach ($s in $samples) {
                        $k = [double]$s.CookedValue
                        if ($k -gt 200 -and $k -lt 400) {
                            $c = $k - 273.15
                            if (Test-Celsius $c) { [void]$vals.Add($c) }
                        }
                    }
                } catch {}
            }
        } catch {}
    }

    if ($vals.Count -eq 0) { return $null }
    return [math]::Round((($vals | Measure-Object -Maximum).Maximum), 2)
}

function Write-TempFile([double]$c, [string]$path) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    # Local Windows disk only (never \\wsl$ from Task Scheduler).
    $text = ('{0:N2}' -f $c)
    [System.IO.File]::WriteAllText($path, $text + [Environment]::NewLine)
    try {
        $item = Get-Item -LiteralPath $path
        $item.LastWriteTime = Get-Date
    } catch {}
}

if ($Loop) {
    if ($Debug) { Write-Host "Looping $OutFile every ${IntervalSec}s" }
    while ($true) {
        $t = Get-HostCelsius
        if ($null -ne $t) {
            Write-TempFile -c $t -path $OutFile
            if ($Debug) { Write-Host "$(Get-Date -Format o) $t C" }
        } elseif ($Debug) {
            Write-Host "$(Get-Date -Format o) no sensor"
        }
        Start-Sleep -Seconds $IntervalSec
    }
} else {
    $t = Get-HostCelsius
    if ($null -eq $t) {
        if ($Debug) {
            Write-Host "ERROR: no temperature sensor readable on this PC."
        }
        exit 1
    }
    Write-Output $t
    Write-TempFile -c $t -path $OutFile
    exit 0
}
