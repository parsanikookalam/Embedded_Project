# Connect Windows → WSL OpenSSH (Smart Guard Part 3-7)
param(
  [string]$User = "parsa",
  [int]$Port = 2222
)
$ip = (wsl -d Ubuntu -- hostname -I).Trim().Split(" ")[0]
Write-Host "SSH to WSL: $User@$ip :$Port"
ssh -p $Port "$User@$ip"
