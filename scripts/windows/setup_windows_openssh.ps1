# Optional: OpenSSH Server on Windows so WSL can SSH → Windows.
# Run in Windows PowerShell as Administrator:
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   .\scripts\windows\setup_windows_openssh.ps1

Write-Host "==> Install OpenSSH Server (Windows optional capability)"
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0 | Out-Null

Write-Host "==> Start sshd"
Start-Service sshd
Set-Service -Name sshd -StartupType Automatic

# Harden: disallow empty passwords; keep password or key (default Windows OpenSSH)
$cfg = "C:\ProgramData\ssh\sshd_config"
if (Test-Path $cfg) {
  $txt = Get-Content $cfg -Raw
  if ($txt -notmatch "(?m)^PermitRootLogin") {
    Add-Content $cfg "`nPermitRootLogin no`n"
  }
  # On Windows there is no Unix root; Administrators use special rules.
  Write-Host "Edited/checked $cfg"
}

Restart-Service sshd
Write-Host "Windows sshd listening. From WSL try:"
Write-Host '  ssh $USER@$(cat /etc/resolv.conf | awk ''/nameserver/{print $2}'')'
Write-Host "Done."
