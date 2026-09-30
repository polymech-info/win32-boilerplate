# Explorer context menu: convert images to JPEG (re-encode; no resize) via media-img resize --format jpg
# Multi-select: one invocation with all paths (Player + %*).
# Keep extensions in sync with register_explorer.cpp (canonical list; matching is case-insensitive).
param(
    [Parameter(Mandatory)][string]$MediaImg,
    [Parameter(Mandatory, ValueFromRemainingArguments = $true)][string[]]$InputPaths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$InputPaths = @($InputPaths | Where-Object { $_ -and $_.Trim() -ne '' })
if ($InputPaths.Length -eq 0) {
    exit 1
}

$imageExtLower = @(
    '.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp', '.tiff', '.tif', '.jpe', '.jfif',
    '.avif', '.arw'
)

function Test-ImageExt([string]$ext) {
    $e = $ext.ToLowerInvariant()
    return $script:imageExtLower -contains $e
}

function Invoke-MediaImg([string[]]$ArgList) {
    $outF = [System.IO.Path]::GetTempFileName()
    $errF = [System.IO.Path]::GetTempFileName()
    try {
        $winStyle = if ($ArgList -contains '--job-ui') { 'Normal' } else { 'Hidden' }
        $p = Start-Process -FilePath $MediaImg -ArgumentList $ArgList -Wait -PassThru `
            -WindowStyle $winStyle -RedirectStandardOutput $outF -RedirectStandardError $errF
        if ($p.ExitCode -ne 0) {
            $e = if (Test-Path -LiteralPath $errF) { Get-Content -Raw -LiteralPath $errF } else { '' }
            throw "media-img failed (exit $($p.ExitCode)): $e"
        }
    }
    finally {
        Remove-Item -LiteralPath $outF, $errF -ErrorAction SilentlyContinue
    }
}

function Convert-OneFile([string]$File) {
    $dst = '${SRC_DIR}/${SRC_NAME}_converted.jpg'
    Invoke-MediaImg @(
        'resize', '--src', $File, '--dst', $dst,
        '--format', 'jpg', '-q', '88',
        '--fit', 'inside', '--no-cache', '--job-ui'
    )
}

function Process-OnePath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        exit 1
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        $files = Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction Stop |
            Where-Object { Test-ImageExt $_.Extension }
        if (-not $files) {
            exit 0
        }
        foreach ($f in $files) {
            Convert-OneFile $f.FullName
        }
    }
    else {
        if (-not (Test-ImageExt $item.Extension)) {
            exit 0
        }
        Convert-OneFile $item.FullName
    }
}

# Two or more plain image files (not folders) → one process, one job window (same as IExecute --src).
if ($InputPaths.Length -ge 2) {
    $batchFiles = [System.Collections.Generic.List[string]]::new()
    $hasFolder  = $false
    foreach ($p in $InputPaths) {
        if (-not (Test-Path -LiteralPath $p)) { throw "Path not found: $p" }
        $item = Get-Item -LiteralPath $p
        if ($item.PSIsContainer) { $hasFolder = $true; break }
        if (Test-ImageExt $item.Extension) { $batchFiles.Add($item.FullName) }
    }
    if (-not $hasFolder -and $batchFiles.Count -ge 2) {
        $dst = '${SRC_DIR}/${SRC_NAME}_converted.jpg'
        $a = [System.Collections.Generic.List[string]]::new()
        $a.Add('resize') | Out-Null
        foreach ($f in $batchFiles) { $a.Add('--src'); $a.Add($f) }
        $a.AddRange(@(
                '--dst', $dst, '--format', 'jpg', '-q', '88', '--fit', 'inside', '--no-cache', '--job-ui'
            ))
        Invoke-MediaImg $a
        exit 0
    }
}

if ($InputPaths.Length -gt 1) {
    foreach ($p in $InputPaths) {
        Process-OnePath $p
    }
    exit 0
}

Process-OnePath $InputPaths[0]
exit 0
