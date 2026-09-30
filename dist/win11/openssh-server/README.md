# Win11 OpenSSH Server Payload

This folder contains the official Microsoft/PowerShell Win32-OpenSSH x64 release bundle:

- `OpenSSH-Win64.zip`: downloaded release archive
- `OpenSSH-Win64/`: extracted binaries and installer scripts
- `manifest.json`: release URL, timestamp, size, and SHA-256 hash
- `install-and-start-sshd.ps1`: local helper to install the `sshd` service, allow TCP port 22, set automatic startup, and start the service

Run from an elevated PowerShell prompt:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope Process
.\install-and-start-sshd.ps1
```

After installation, check the service with:

```powershell
Get-Service sshd
```
