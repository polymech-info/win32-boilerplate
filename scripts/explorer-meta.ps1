# Explorer context-menu helper: generate <name>.md + <name>.json next to each
# image using `media-img meta` (Google Gemini).
#
# Requires IMAGE_TRANSFORM_GOOGLE_API_KEY to be set in the user / system
# environment. Multi-select / folders supported. Outputs land next to source.
# Keep extensions in sync with register_explorer.cpp.
param(
    [Parameter(Mandatory)][string]$MediaImg,
    [Parameter()][string]$Model = 'gemini-2.5-flash',
    [Parameter()][int]$ResizeWidth = 512,
    [Parameter(Mandatory, ValueFromRemainingArguments = $true)][string[]]$InputPaths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$InputPaths = @($InputPaths | Where-Object { $_ -and $_.Trim() -ne '' })
if ($InputPaths.Length -eq 0) { exit 1 }

if (-not $env:IMAGE_TRANSFORM_GOOGLE_API_KEY) {
    # No API key configured — surface this as a clean exit code; the script
    # runs hidden so we can't pop a UI message. The launcher (NSIS, dev runs)
    # documents the env var requirement.
    exit 2
}

$imageExt = @(
    '.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp', '.tiff', '.tif', '.jpe', '.jfif',
    '.avif', '.arw'
)

function Test-ImageExt([string]$ext) {
    return $script:imageExt -contains $ext.ToLowerInvariant()
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

function Meta-OneFile([string]$File) {
    Invoke-MediaImg @(
        'meta', $File,
        '--model', $Model,
        '--resize-width', "$ResizeWidth",
        '--job-ui'
    )
}

function Process-OnePath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { exit 1 }
    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        $files = Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction Stop |
            Where-Object { Test-ImageExt $_.Extension }
        if (-not $files) { exit 0 }
        foreach ($f in $files) { Meta-OneFile $f.FullName }
    } else {
        if (-not (Test-ImageExt $item.Extension)) { exit 0 }
        Meta-OneFile $item.FullName
    }
}

# Two or more plain image files (not folders) → one pm-image run, one job window (same as IExecute).
if ($InputPaths.Length -ge 2) {
    $batchFiles = [System.Collections.Generic.List[string]]::new()
    $hasFolder  = $false
    foreach ($p in $InputPaths) {
        if (-not (Test-Path -LiteralPath $p)) { exit 1 }
        $item = Get-Item -LiteralPath $p
        if ($item.PSIsContainer) { $hasFolder = $true; break }
        if (Test-ImageExt $item.Extension) { $batchFiles.Add($item.FullName) }
    }
    if (-not $hasFolder -and $batchFiles.Count -ge 2) {
        $a = [System.Collections.Generic.List[string]]::new()
        $a.Add('meta') | Out-Null
        foreach ($f in $batchFiles) { $a.Add($f) | Out-Null }
        $a.AddRange(@(
                '--model', $Model, '--resize-width', ([string]$ResizeWidth), '--job-ui'
            ))
        Invoke-MediaImg $a
        exit 0
    }
}

foreach ($p in $InputPaths) { Process-OnePath $p }
exit 0
