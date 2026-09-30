# Win11 SSH Server Setup

These steps run on the Windows 11 server machine.

## Install And Start OpenSSH

Open an elevated PowerShell in this folder:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope Process
.\install-and-start-sshd.ps1
```

Check the service:

```powershell
Get-Service sshd
```

## Add A Client Public Key

For a normal server user account, create the user's SSH folder and `authorized_keys` file:

```powershell
mkdir $env:USERPROFILE\.ssh -Force
notepad $env:USERPROFILE\.ssh\authorized_keys
```

Paste the client public key into `authorized_keys`, one key per line. Example:

```text
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIC0Ywjb60XXicPx2weJbSGshN85pLySaf8NwxU9TQhVr your-label
```

Fix permissions:

```powershell
icacls $env:USERPROFILE\.ssh /inheritance:r
icacls $env:USERPROFILE\.ssh /grant "$env:USERNAME:F"
icacls $env:USERPROFILE\.ssh\authorized_keys /inheritance:r
icacls $env:USERPROFILE\.ssh\authorized_keys /grant "$env:USERNAME:F"
Restart-Service sshd
```

Connect from the client:

```powershell
ssh <server-user>@<server-ip>
```

## Administrator Accounts

If the server user is an Administrator and key login fails, Windows OpenSSH may require admin keys in:

```text
C:\ProgramData\ssh\administrators_authorized_keys
```

Create or edit it from elevated PowerShell:

```powershell
notepad C:\ProgramData\ssh\administrators_authorized_keys
```

Paste the same client public key, then fix permissions:

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r
icacls C:\ProgramData\ssh\administrators_authorized_keys /grant "Administrators:F" "SYSTEM:F"
Restart-Service sshd
```

## Debug Login

From the client, use verbose output:

```powershell
ssh -vvv <server-user>@<server-ip>
```
