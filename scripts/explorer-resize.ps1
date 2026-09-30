# Explorer context-menu helper: resize images (single file, multiple files, or folder tree) via media-img.exe
# Multi-select: `register-explorer` (default) uses ` %*` and MultiSelectModel=Player — one run for the selection.
#  Use `pm-image register-explorer --per-file` for legacy one launch per file (`%1` only).
# Debug log: %APPDATA%\PolyMech\pm-image\invoke-resize.log
# Static CreateProcess verbs (this script) are limited vs COM handlers; see:
# https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd758091(v=vs.85)
# Uses Start-Process -WindowStyle Hidden so no console window (VIPS stderr is redirected to a temp file).
param(
    [Parameter(Mandatory)][string]$MediaImg,
    [Parameter(Mandatory)][int]$MaxWidth,
    [Parameter(Mandatory)][ValidateSet('InPlace', 'Copy')][string]$Mode,
    [Parameter(Mandatory, ValueFromRemainingArguments = $true)][string[]]$InputPaths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- %APPDATA%\PolyMech\pm-image\invoke-resize.log
$script:__invokeResizeLogPath = [System.IO.Path]::Combine(
    $env:APPDATA, 'PolyMech', 'pm-image', 'invoke-resize.log')
$script:__invokeResizeLogDir  = [System.IO.Path]::GetDirectoryName($script:__invokeResizeLogPath)
$script:__invokeResizeLogMutex = $null
function Write-InvokeResizeLog {
    param(
        [Parameter(Mandatory)][string]$Message
    )
    try {
        if ($null -eq $script:__invokeResizeLogDir) { return }
        if ($null -eq $script:__invokeResizeLogMutex) {
            $script:__invokeResizeLogMutex = New-Object System.Threading.Mutex(
                $false, 'Local\PolyMech-pm-image-invoke-resize-log')
        }
        if (-not (Test-Path -LiteralPath $script:__invokeResizeLogDir)) {
            New-Item -ItemType Directory -Path $script:__invokeResizeLogDir -Force -ErrorAction Stop | Out-Null
        }
        if (-not $script:__invokeResizeLogMutex.WaitOne(10000)) { return }
        try {
            $ts   = Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'
            $line = "[$ts] $Message"
            Add-Content -LiteralPath $script:__invokeResizeLogPath -Value $line -Encoding utf8 -ErrorAction Stop
        } finally {
            [void]$script:__invokeResizeLogMutex.ReleaseMutex()
        }
    } catch {
        # Never break resize because logging failed
    }
}
Write-InvokeResizeLog "script init pid=$PID"

# Canonical list — keep in sync with register_explorer.cpp (k_canonical_ext + case-insensitive match)
$imageExt = @(
    '.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp', '.tiff', '.tif', '.jpe', '.jfif',
    '.avif', '.arw'
)

function Test-ImageExt([string]$ext) {
    $e = $ext.ToLowerInvariant()
    return $script:imageExt -contains $e
}

function Invoke-MediaImgResize([string[]]$ArgList) {
    $outF = [System.IO.Path]::GetTempFileName()
    $errF = [System.IO.Path]::GetTempFileName()
    try {
        $winStyle = if ($ArgList -contains '--job-ui') { 'Normal' } else { 'Hidden' }
        $argForLog = ($ArgList | ForEach-Object {
                if ($_.Length -gt 400) { $_.Substring(0, 400) + '…' } else { $_ }
        }) -join ' '
        Write-InvokeResizeLog "Start-Process: WindowStyle=$winStyle MediaImg=$MediaImg args_count=$($ArgList.Count) args=$argForLog"
        $p = Start-Process -FilePath $MediaImg -ArgumentList $ArgList -Wait -PassThru `
            -WindowStyle $winStyle -RedirectStandardOutput $outF -RedirectStandardError $errF
        $stdOut = if (Test-Path -LiteralPath $outF) { Get-Content -Raw -LiteralPath $outF -ErrorAction SilentlyContinue } else { '' }
        if ($stdOut) {
            $slim = if ($stdOut.Length -gt 2000) { $stdOut.Substring(0, 2000) + '…' } else { $stdOut }
            Write-InvokeResizeLog "media-img stdout: $slim"
        }
        if ($p.ExitCode -ne 0) {
            $e = if (Test-Path -LiteralPath $errF) { Get-Content -Raw -LiteralPath $errF } else { '' }
            if ($e) {
                $eSlim = if ($e.Length -gt 2000) { $e.Substring(0, 2000) + '…' } else { $e }
                Write-InvokeResizeLog "media-img exit $($p.ExitCode) stderr: $eSlim"
            } else {
                Write-InvokeResizeLog "media-img exit $($p.ExitCode) (no stderr capture)"
            }
            throw "media-img failed (exit $($p.ExitCode)): $e"
        }
        Write-InvokeResizeLog "media-img exit 0 (ok)"
    } finally {
        Remove-Item -LiteralPath $outF, $errF -ErrorAction SilentlyContinue
    }
}

# Collects image file paths for every top-level path (file, folder, or multi-select).
function Get-ImageFilePathsFromInput {
    param([Parameter(Mandatory)][string[]]$TopPaths)
    $out = [System.Collections.Generic.List[string]]::new()
    foreach ($p in $TopPaths) {
        if (-not (Test-Path -LiteralPath $p)) {
            Write-InvokeResizeLog "PATH NOT FOUND (exit 1): $p"
            exit 1
        }
        $item = Get-Item -LiteralPath $p
        if ($item.PSIsContainer) {
            Get-ChildItem -LiteralPath $p -Recurse -File -ErrorAction Stop |
            Where-Object { Test-ImageExt $_.Extension } |
            ForEach-Object { $out.Add($_.FullName) }
        } elseif (Test-ImageExt $item.Extension) {
            $out.Add($item.FullName)
        }
    }
    return ,@($out)
}

# One media-img run with one --job-ui list when this script is invoked once with all paths (folder; or multi-select if ` %*` works).
function Invoke-ResizeBatch([string[]]$ImageFiles) {
    if ($null -eq $ImageFiles -or $ImageFiles.Count -eq 0) { return }
    $w  = $MaxWidth
    $al = [System.Collections.Generic.List[string]]::new()
    $al.Add('resize')
    $al.Add('--max-width')
    $al.Add("$w")
    $al.Add('--fit')
    $al.Add('inside')
    $al.Add('--no-cache')
    $al.Add('--job-ui')
    if ($Mode -eq 'Copy') {
        $template = '${SRC_DIR}/${SRC_NAME}_' + $w + '${SRC_FILE_EXT}'
        $al.Add('--dst')
        $al.Add($template)
    } else {
        $al.Add('--dst')
        $al.Add('${SRC_DIR}/${SRC_NAME}${SRC_FILE_EXT}')
    }
    foreach ($f in $ImageFiles) {
        $al.Add('--src')
        $al.Add($f)
    }
    Write-InvokeResizeLog "Invoke-ResizeBatch: $($ImageFiles.Count) source file(s)"
    Invoke-MediaImgResize $al.ToArray()
}

try {
    $InputPaths = @($InputPaths | Where-Object { $_ -and $_.Trim() -ne '' })
    if ($InputPaths.Length -eq 0) {
        Write-InvokeResizeLog "empty InputPaths (exit 1)"
        exit 1
    }
    $cmdLine = [Environment]::GetCommandLineArgs() -join ' '
    if ($cmdLine.Length -gt 3000) { $cmdLine = $cmdLine.Substring(0, 3000) + '…' }
    Write-InvokeResizeLog "======== pid=$PID PS=$($PSVersionTable.PSVersion) ========"
    Write-InvokeResizeLog "CommandLine: $cmdLine"
    Write-InvokeResizeLog "Param MediaImg=$MediaImg MaxWidth=$MaxWidth Mode=$Mode InputPaths_count=$($InputPaths.Count)"
    for ($i = 0; $i -lt $InputPaths.Count; $i++) {
        Write-InvokeResizeLog "  InputPaths[$i] = $($InputPaths[$i])"
    }

    $files = Get-ImageFilePathsFromInput -TopPaths $InputPaths
    Write-InvokeResizeLog "after scan: image files count = $($files.Count)"
    if ($files.Count -eq 0) {
        Write-InvokeResizeLog "no images in selection (exit 0)"
        exit 0
    }
    if ($files.Count -le 25) {
        for ($i = 0; $i -lt $files.Count; $i++) {
            Write-InvokeResizeLog "  image[$i] = $($files[$i])"
        }
    } else {
        for ($i = 0; $i -lt 10; $i++) {
            Write-InvokeResizeLog "  image[$i] = $($files[$i])"
        }
        Write-InvokeResizeLog "  … ($($files.Count) total, truncated log)"
    }

    Invoke-ResizeBatch -ImageFiles $files
    Write-InvokeResizeLog "finished OK (exit 0)"
    exit 0
} catch {
    Write-InvokeResizeLog "EXCEPTION: $($_.Exception.GetType().FullName): $($_.Exception.Message)"
    if ($_.InvocationInfo.PositionMessage) { Write-InvokeResizeLog "At: $($_.InvocationInfo.PositionMessage)" }
    if ($_.ScriptStackTrace) {
        $st = $_.ScriptStackTrace
        if ($st.Length -gt 4000) { $st = $st.Substring(0, 4000) + '…' }
        Write-InvokeResizeLog $st
    }
    exit 1
}
