$ErrorActionPreference = 'Stop'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell prompt.'
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$openSshDir = Join-Path $root 'OpenSSH-Win64'
$installer = Join-Path $openSshDir 'install-sshd.ps1'

if (-not (Test-Path $installer)) {
    throw "OpenSSH installer not found: $installer"
}

Push-Location $openSshDir
try {
    & $installer
}
finally {
    Pop-Location
}

$existingRule = Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue
if (-not $existingRule) {
    New-NetFirewallRule `
        -Name 'OpenSSH-Server-In-TCP' `
        -DisplayName 'OpenSSH Server (sshd)' `
        -Enabled True `
        -Direction Inbound `
        -Protocol TCP `
        -Action Allow `
        -LocalPort 22 | Out-Null
}

Set-Service -Name sshd -StartupType Automatic
Start-Service -Name sshd
Get-Service -Name sshd
