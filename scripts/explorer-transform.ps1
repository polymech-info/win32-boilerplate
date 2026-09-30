# Explorer: AI transform using a preset id (settings.json explorer_presets or chat_web quick action).
# Invokes: media-img transform <file> --preset-id <id>
# Appends to %APPDATA%\PolyMech\pm-image\pm-image.log (see register-explorer / app logs in the same dir).
# Requires IMAGE_TRANSFORM_GOOGLE_API_KEY (or Google key in settings) like `transform` CLI.
param(
    [Parameter(Mandatory)][string]$MediaImg,
    [Parameter(Mandatory)][string]$PresetId,
    [Parameter(Mandatory, ValueFromRemainingArguments = $true)][string[]]$InputPaths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Explorer / command line can pass --preset-id with literal ' or " in the value; media-img needs a clean id.
$PresetId = $PresetId.Trim()
while ($PresetId.Length -gt 0 -and ($PresetId[0] -eq [char]39 -or $PresetId[0] -eq [char]34)) {
    $PresetId = $PresetId.Substring(1)
}
while ($PresetId.Length -gt 0 -and ($PresetId[$PresetId.Length - 1] -eq [char]39 -or $PresetId[$PresetId.Length - 1] -eq [char]34)) {
    $PresetId = $PresetId.Substring(0, $PresetId.Length - 1)
}

function Write-ExplorerTransformLog([string]$Message) {
    try {
        $dir = Join-Path $env:APPDATA 'PolyMech\pm-image'
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        $log  = Join-Path $dir 'pm-image.log'
        $ts   = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        $line = "$ts [explorer-transform] $Message"
        Add-Content -LiteralPath $log -Value $line -Encoding utf8
    }
    catch {
        [Console]::Error.WriteLine("[explorer-transform] $Message (log write failed: $($_.Exception.Message))")
    }
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
        Write-ExplorerTransformLog "media-img: $([string]::Join(' ', $ArgList))"
        $winStyle = if ($ArgList -contains '--job-ui') { 'Normal' } else { 'Hidden' }
        $p = Start-Process -FilePath $MediaImg -ArgumentList $ArgList -Wait -PassThru `
            -WindowStyle $winStyle -RedirectStandardOutput $outF -RedirectStandardError $errF
        if ($p.ExitCode -ne 0) {
            $out = if (Test-Path -LiteralPath $outF) { Get-Content -Raw -LiteralPath $outF } else { '' }
            $e   = if (Test-Path -LiteralPath $errF) { Get-Content -Raw -LiteralPath $errF } else { '' }
            Write-ExplorerTransformLog "media-img exit=$($p.ExitCode) stdout=$out stderr=$e"
            throw "media-img failed (exit $($p.ExitCode)): $e"
        }
    }
    finally {
        Remove-Item -LiteralPath $outF, $errF -ErrorAction SilentlyContinue
    }
}

function Transform-OneFile([string]$File) {
    Invoke-MediaImg @('transform', $File, '--preset-id', $PresetId, '--job-ui')
}

function Process-OnePath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-ExplorerTransformLog "path not found: $Path"
        throw "Path not found: $Path"
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        $files = Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction Stop |
            Where-Object { Test-ImageExt $_.Extension }
        if (-not $files) {
            Write-ExplorerTransformLog "no image files under folder: $Path"
            return
        }
        Write-ExplorerTransformLog "folder $Path - $($files.Count) image file(s) to transform"
        foreach ($f in $files) { Transform-OneFile $f.FullName }
    }
    else {
        if (-not (Test-ImageExt $item.Extension)) {
            Write-ExplorerTransformLog "skip non-image file: $Path (ext=$($item.Extension))"
            return
        }
        Transform-OneFile $item.FullName
    }
}

$InputPaths = @($InputPaths | Where-Object { $_ -and $_.Trim() -ne '' })
if ($InputPaths.Length -eq 0) {
    Write-ExplorerTransformLog 'ERROR: no input paths (empty %1 or args)'
    exit 1
}

try {
    Write-ExplorerTransformLog "start PresetId=$PresetId MediaImg=$MediaImg pathCount=$($InputPaths.Length) paths=$([string]::Join(' | ', $InputPaths))"

    # Two or more plain image files (not folders) → one pm-image process, one job window (same as IExecute --src).
    if ($InputPaths.Length -ge 2) {
        $batchFiles = [System.Collections.Generic.List[string]]::new()
        $hasFolder  = $false
        foreach ($p in $InputPaths) {
            if (-not (Test-Path -LiteralPath $p)) { throw "Path not found: $p" }
            $item = Get-Item -LiteralPath $p
            if ($item.PSIsContainer) {
                $hasFolder = $true
                break
            }
            if (-not (Test-ImageExt $item.Extension)) {
                Write-ExplorerTransformLog "skip non-image in batch: $p"
            }
            else {
                $batchFiles.Add($item.FullName)
            }
        }
        if (-not $hasFolder -and $batchFiles.Count -ge 2) {
            $arg = [System.Collections.Generic.List[string]]::new()
            $arg.Add('transform') | Out-Null
            foreach ($f in $batchFiles) {
                $arg.Add('--src') | Out-Null
                $arg.Add($f) | Out-Null
            }
            $arg.AddRange(@('--preset-id', $PresetId, '--job-ui'))
            Write-ExplorerTransformLog "batched transform: $($batchFiles.Count) file(s) in one process"
            Invoke-MediaImg $arg
            Write-ExplorerTransformLog 'done (ok)'
            exit 0
        }
    }

    foreach ($p in $InputPaths) {
        Process-OnePath $p
    }
    Write-ExplorerTransformLog 'done (ok)'
    exit 0
}
catch {
    Write-ExplorerTransformLog "ERROR: $($_.Exception.Message)"
    exit 1
}
