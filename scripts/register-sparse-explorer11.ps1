# Register the sparse package layout (docs/win11.md). Requires an elevated or dev-mode
# environment suitable for Add-AppxPackage -Register on this machine.
param(
  [string] $LayoutRoot = (Join-Path $PSScriptRoot "..\dist\sparse")
)
$ErrorActionPreference = "Stop"
$manifest = Join-Path $LayoutRoot "AppxManifest.xml"
if (-not (Test-Path -LiteralPath $manifest)) {
  Write-Error "Missing $manifest — build the project (pm-image-explorer11) and reconfigure CMake so dist/sparse/ is generated."
}
Write-Host "Registering sparse package from: $LayoutRoot"
Add-AppxPackage -Register -Path $manifest
Write-Host "Done. If registration failed, check Developer settings, manifest schema vs SDK, and publisher/signing (see docs/win11.md)."
