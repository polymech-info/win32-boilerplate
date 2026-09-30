# Wrapper for NSSM, WinSW, Task Scheduler, or manual runs. Configure via environment variables.
#
#   $env:MEDIA_IMG_CMD       = serve | ipc (default: serve)
#   $env:MEDIA_IMG_BIN       = path to pm-image.exe (optional)
#   $env:MEDIA_IMG_HOST      = bind address (default: 127.0.0.1)
#   $env:MEDIA_IMG_PORT      = TCP port — default 8080 for serve, 9333 for ipc
#   $env:MEDIA_IMG_CACHE_DIR = optional — passed as --cache-dir
#   $env:MEDIA_IMG_NO_CACHE  = "1" to add --no-cache
#   $env:MEDIA_IMG_IPC_UNIX  = not used on Windows (IPC TCP only here)
#
# If MEDIA_IMG_BIN is unset, uses ..\dist\pm-image.exe next to this script when present,
# otherwise "pm-image.exe" on PATH.
#
# Example:
#   $env:MEDIA_IMG_CMD = "serve"; $env:MEDIA_IMG_PORT = "8080"
#   & C:\Apps\media\scripts\run-media-img.ps1

param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ExtraArgs
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$candidate = Join-Path $here "..\dist\pm-image.exe"

if ($env:MEDIA_IMG_BIN) {
    $bin = $env:MEDIA_IMG_BIN
}
elseif (Test-Path -LiteralPath $candidate) {
    $bin = (Resolve-Path $candidate).Path
}
else {
    $bin = "pm-image.exe"
}

$cmd = if ($env:MEDIA_IMG_CMD) { $env:MEDIA_IMG_CMD } else { "serve" }
$hostAddr = if ($env:MEDIA_IMG_HOST) { $env:MEDIA_IMG_HOST } else { "127.0.0.1" }

$extra = @()
if ($env:MEDIA_IMG_CACHE_DIR) {
    $extra += "--cache-dir", $env:MEDIA_IMG_CACHE_DIR
}
if ($env:MEDIA_IMG_NO_CACHE -eq "1") {
    $extra += "--no-cache"
}

switch ($cmd) {
    "serve" {
        $port = if ($env:MEDIA_IMG_PORT) { $env:MEDIA_IMG_PORT } else { "8080" }
        & "$bin" serve --host $hostAddr --port $port @extra @ExtraArgs
        exit $LASTEXITCODE
    }
    "ipc" {
        $port = if ($env:MEDIA_IMG_PORT) { $env:MEDIA_IMG_PORT } else { "9333" }
        & "$bin" ipc --host $hostAddr --port $port @extra @ExtraArgs
        exit $LASTEXITCODE
    }
    default {
        Write-Error "run-media-img.ps1: MEDIA_IMG_CMD must be 'serve' or 'ipc', got: $cmd"
    }
}
