# Download Microsoft Visual C++ 2015-2022 x64 redistributable for bundling in NSIS.
# License: see Microsoft VS / VC++ redistributable terms. Do not repackage under your own EULA.
# https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
$ErrorActionPreference = "Stop"
$root   = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root "dist\installer\redist"
$outFile = Join-Path $outDir "VC_redist.x64.exe"
$url     = "https://aka.ms/vs/17/release/vc_redist.x64.exe"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (Test-Path $outFile) {
  $n = (Get-Item $outFile).Length
  if ($n -gt 8MB) { Write-Host "VC redist already present ($n bytes), skip download."; exit 0 }
}
Write-Host "Downloading VC++ x64 redistributable -> $outFile"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $outFile -UseBasicParsing
Write-Host "OK: $((Get-Item $outFile).Length) bytes"
