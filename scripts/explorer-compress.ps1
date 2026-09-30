# Explorer context-menu helper: re-encode images to a smaller copy via media-img compress.
#
# Output: <name>_compressed.<ext>   (.jpg → MozJPEG, .png → PNG; auto by extension)
# Multi-select: one invocation with all paths. Folders are scanned recursively.
# Keep extensions in sync with register_explorer.cpp.
param(
    [Parameter(Mandatory)][string]$MediaImg,
    [Parameter()][ValidateSet('Auto', 'MozJPEG', 'PNG')][string]$Compressor = 'Auto',
    [Parameter()][int]$Quality = 82,
    [Parameter(Mandatory, ValueFromRemainingArguments = $true)][string[]]$InputPaths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$InputPaths = @($InputPaths | Where-Object { $_ -and $_.Trim() -ne '' })
if ($InputPaths.Length -eq 0) { exit 1 }

# Canonical list — keep in sync with register_explorer.cpp (k_canonical_ext).
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

function Compress-OneFile([string]$File) {
    $args = @('compress', $File)
    if ($Compressor -ne 'Auto') {
        $args += @('--compressor', $Compressor.ToLowerInvariant())
    }
    if ($Compressor -ne 'PNG') {
        $args += @('-q', "$Quality")
    }
    $args += '--job-ui'
    Invoke-MediaImg $args
}

function Process-OnePath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { exit 1 }
    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        $files = Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction Stop |
            Where-Object { Test-ImageExt $_.Extension }
        if (-not $files) { exit 0 }
        foreach ($f in $files) { Compress-OneFile $f.FullName }
    } else {
        if (-not (Test-ImageExt $item.Extension)) { exit 0 }
        Compress-OneFile $item.FullName
    }
}

foreach ($p in $InputPaths) { Process-OnePath $p }
exit 0
