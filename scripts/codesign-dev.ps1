#Requires -Version 5.1
<#
.SYNOPSIS
  Create a self-signed *code signing* PFX (dev/test) and Authenticode-sign built binaries with signtool.
.DESCRIPTION
  Modern approach: New-SelfSignedCertificate (replaces MakeCert) + Export-PfxCertificate + signtool, per:
  - https://stackoverflow.com/questions/84847/how-do-i-create-a-self-signed-certificate-for-code-signing-on-windows
  - https://learn.microsoft.com/powershell/module/pki/new-selfsignedcertificate

  Default subject "CN=PolyMech PM-Image Sparse" matches PM_SPARSE_PUBLISHER in CMake (sparse Appx manifest).

  Password order: (1) env PM_CODESIGN_PFX_PASSWORD (2) file dist/certs/.pm-codesign-pfx.password
  (auto-created on first PFX export; gitignored) (3) interactive prompt.
  CI: set PM_CODESIGN_PFX_PASSWORD.
.PARAMETER CertOnly
  Create/export PFX only; do not run signtool.
.PARAMETER PfxPath
  Output PFX path (default: dist/certs/pm-image-dev-codesign.pfx).
.PARAMETER SignFiles
  Paths relative to repo root (or absolute) to sign; missing files are skipped with a warning.
#>
[CmdletBinding()]
param(
  [string] $PfxPath,
  [string] $Subject = "CN=PolyMech PM-Image Sparse",
  [string[]] $SignFiles = @(
    "dist\pm-image.exe",
    "dist\pm-image-explorer11.dll",
    "dist\sparse\pm-image.exe",
    "dist\sparse\pm-image-explorer11.dll"
  ),
  [string] $TimestampRfc3161 = "http://timestamp.digicert.com",
  [switch] $NoTimestamp,
  [switch] $ForceNewCert,
  [switch] $CertOnly
)

$ErrorActionPreference = "Stop"
# $PSScriptRoot can be empty when invoked as `powershell -File ...` from some hosts (e.g. npm on Windows)
$ScriptDir = $PSScriptRoot
if ([string]::IsNullOrEmpty($ScriptDir) -and $MyInvocation.MyCommand.Path) {
  $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrEmpty($ScriptDir)) { throw "Cannot resolve script directory; run: powershell -File scripts\codesign-dev.ps1" }
$root = (Resolve-Path (Join-Path $ScriptDir "..")).Path
if ([string]::IsNullOrEmpty($PfxPath)) {
  $PfxPath = Join-Path $ScriptDir "..\dist\certs\pm-image-dev-codesign.pfx"
}
if ([IO.Path]::IsPathRooted($PfxPath)) { $p = $PfxPath } else { $p = Join-Path $root $PfxPath }
$PfxPath = $p
Set-Location -LiteralPath $root

$script:PasswordFile = Join-Path $root "dist\certs\.pm-codesign-pfx.password"

function Get-PfxPasswordPlain {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("CreatePfx", "SignExisting")]
    [string] $Mode
  )
  if ($env:PM_CODESIGN_PFX_PASSWORD) { return $env:PM_CODESIGN_PFX_PASSWORD }
  if (Test-Path -LiteralPath $script:PasswordFile) {
    return (Get-Content -LiteralPath $script:PasswordFile -Raw -Encoding utf8).Trim()
  }
  if ($Mode -eq "CreatePfx") {
    $dir = Split-Path -Parent $script:PasswordFile
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $b = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b)
    $plain = [Convert]::ToBase64String($b)
    [System.IO.File]::WriteAllText($script:PasswordFile, $plain, [System.Text.UTF8Encoding]::new($false))
    Write-Host "Created local PFX password file: $script:PasswordFile (gitignored). Set PM_CODESIGN_PFX_PASSWORD to override."
    return $plain
  }
  if ([Environment]::UserInteractive) {
    $sec = Read-Host "PFX password (or set env PM_CODESIGN_PFX_PASSWORD, or create the file above)" -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
  }
  throw "Non-interactive: set PM_CODESIGN_PFX_PASSWORD or add dist/certs/.pm-codesign-pfx.password with the PFX password."
}

function Get-SignTool {
  $hits = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe",
    "${env:ProgramFiles}\Windows Kits\10\bin\*\x64\signtool.exe"
  ) | ForEach-Object { Get-Item -ErrorAction SilentlyContinue $_ }
  if (-not $hits) { throw "signtool.exe not found. Install the Windows 10/11 SDK (or Visual Studio) with Windows SDK." }
  $hits | Sort-Object { $_.DirectoryName } -Descending | Select-Object -First 1
}

# --- PFX: reuse or create ---
$pfx = $PfxPath
$pfxDir = Split-Path -Parent $pfx
if ($pfxDir -and -not (Test-Path -LiteralPath $pfxDir)) { New-Item -ItemType Directory -Path $pfxDir | Out-Null }
$useExisting = (Test-Path -LiteralPath $pfx) -and -not $ForceNewCert

if (-not $useExisting) {
  $passPlain = Get-PfxPasswordPlain -Mode CreatePfx
  if (-not $passPlain) { throw "Empty password; refusing to create unprotected PFX in automation." }
  $passSec   = ConvertTo-SecureString -String $passPlain -AsPlainText -Force

  Write-Host "Creating self-signed code signing certificate: $Subject"
  $newParams = @{
    Subject            = $Subject
    Type               = "CodeSigningCert"
    KeyExportPolicy    = "Exportable"
    KeyLength          = 2048
    KeyAlgorithm       = "RSA"
    HashAlgorithm      = "SHA256"
    CertStoreLocation  = "Cert:\CurrentUser\My"
    NotAfter           = (Get-Date).AddYears(5)
  }
  $cert = New-SelfSignedCertificate @newParams
  try {
    Write-Host "Exporting PFX to $pfx"
    Export-PfxCertificate -Cert "cert:\CurrentUser\My\$($cert.Thumbprint)" -FilePath $pfx -Password $passSec
  } finally { Remove-Item "cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey }
  $useExisting = $true
}

if ($CertOnly) { Write-Host "Done (-CertOnly). PFX: $pfx"; return }

# --- sign ---
$passForSign = Get-PfxPasswordPlain -Mode SignExisting

$signtool = Get-SignTool
Write-Host "Using signtool: $($signtool.FullName)"
foreach ($rel in $SignFiles) {
  $f = Join-Path $root $rel
  if (-not (Test-Path -LiteralPath $f)) { Write-Warning "Skip (missing): $rel"; continue }
  if ($NoTimestamp) { $argList = @("sign", "/v", "/fd", "sha256", "/f", $pfx, "/p", $passForSign, $f) }
  else { $argList = @("sign", "/v", "/fd", "sha256", "/f", $pfx, "/p", $passForSign, "/tr", $TimestampRfc3161, "/td", "sha256", $f) }
  $proc = Start-Process -FilePath $signtool.FullName -ArgumentList $argList -Wait -PassThru -NoNewWindow
  if ($proc.ExitCode -ne 0) { throw "signtool failed (exit $($proc.ExitCode)) for: $f" }
}
Write-Host "codesign-dev.ps1: done."