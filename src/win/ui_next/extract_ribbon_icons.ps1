#requires -version 5
# Extracts coloured ribbon tiles from one or more reference mockup PNGs and
# emits per-icon 32x32 (large) and 16x16 (small) 32-bpp BMP files for use
# with the win32xx Ribbon (CRibbon::IUIImageFromBitmap).
#
# Output:
#   src\win\ui_next\res\Ribbon<Name>L.bmp   (32x32, BGRA)
#   src\win\ui_next\res\Ribbon<Name>S.bmp   (16x16, BGRA)
#
# Each $mockups entry lists the coloured tiles, in left-to-right order, that
# the source PNG contains. The tile bounds are auto-detected by scanning a
# probe row near the vertical centre for runs of saturated pixels.

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $root 'res'
$refDir = [System.IO.Path]::GetFullPath((Join-Path $root '..\..\..\ref'))

if (-not (Test-Path $outDir)) { [void](New-Item -ItemType Directory -Path $outDir) }

# --- Mockups to process ---------------------------------------------------
# Each entry: File = PNG name in ref/, Names = tile labels left-to-right.
$mockups = @(
    [pscustomobject]@{
        File  = 'screen_create_a_modern_flat_looking_ribbon_butt.PNG'
        Names = @('Resize','Compress','Meta','Transform','Find','Chat','Run')
    }
    [pscustomobject]@{
        File  = 'screen_create_a_modern_flat_looking_ribbon_butt_2.png'
        Names = @('AddFiles','AddFolder','Clear','SaveAs')
    }
)

$Argb = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb

function Save-Bmp32 {
    param([System.Drawing.Bitmap]$bmp, [string]$path)
    $tmp = [System.Drawing.Bitmap]::new([int]$bmp.Width, [int]$bmp.Height, $Argb)
    $g   = [System.Drawing.Graphics]::FromImage($tmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($bmp, 0, 0, $bmp.Width, $bmp.Height)
    $g.Dispose()
    $tmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $tmp.Dispose()
}

function Render-Square {
    param([System.Drawing.Bitmap]$srcBmp, [int]$x, [int]$y, [int]$w, [int]$h, [int]$side)
    $square = [System.Drawing.Bitmap]::new([int]$side, [int]$side, $Argb)
    $g = [System.Drawing.Graphics]::FromImage($square)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $dstX = [int](($side - $w) / 2)
    $dstY = [int](($side - $h) / 2)
    $srcRect = [System.Drawing.Rectangle]::new([int]$x, [int]$y, [int]$w, [int]$h)
    $dstRect = [System.Drawing.Rectangle]::new([int]$dstX, [int]$dstY, [int]$w, [int]$h)
    $g.DrawImage($srcBmp, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
    return $square
}

function Resize-Bmp {
    param([System.Drawing.Bitmap]$srcBmp, [int]$size)
    $dst = [System.Drawing.Bitmap]::new([int]$size, [int]$size, $Argb)
    $g   = [System.Drawing.Graphics]::FromImage($dst)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($srcBmp, 0, 0, $size, $size)
    $g.Dispose()
    return $dst
}

function Process-Mockup {
    param([string]$pngPath, [string[]]$names, [string]$outDir)

    if (-not (Test-Path $pngPath)) { Write-Warning "Skipped (missing): $pngPath"; return }

    Write-Host ""
    Write-Host "==> $pngPath"

    $src  = [System.Drawing.Bitmap]::FromFile($pngPath)
    $rect = [System.Drawing.Rectangle]::new(0, 0, [int]$src.Width, [int]$src.Height)
    $data = $src.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $Argb)
    $stride = $data.Stride
    $bytes  = New-Object byte[] ($stride * $src.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $src.UnlockBits($data)

    $isSat = {
        param([int]$x, [int]$y)
        $i = $y * $stride + $x * 4
        $b = $bytes[$i]; $g = $bytes[$i + 1]; $r = $bytes[$i + 2]
        $maxC = [Math]::Max($r, [Math]::Max($g, $b))
        $minC = [Math]::Min($r, [Math]::Min($g, $b))
        return ($maxC - $minC) -gt 40 -and $maxC -gt 80
    }

    # Try several probe rows: tiles can sit at different y offsets per mockup.
    $candidateRows = @(0.50, 0.45, 0.40, 0.55, 0.35, 0.60, 0.30, 0.65, 0.70) |
                     ForEach-Object { [int]($src.Height * $_) }

    $spans  = @()
    $probeY = -1
    foreach ($py in $candidateRows) {
        $tmpSpans = @()
        $inRun = $false; $start = 0
        for ($x = 0; $x -lt $src.Width; $x++) {
            if (& $isSat $x $py) {
                if (-not $inRun) { $start = $x; $inRun = $true }
            } elseif ($inRun) {
                if (($x - $start) -ge 30) { $tmpSpans += ,@($start, ($x - 1)) }
                $inRun = $false
            }
        }
        if ($inRun -and (($src.Width - $start) -ge 30)) {
            $tmpSpans += ,@($start, ($src.Width - 1))
        }
        if ($tmpSpans.Count -eq $names.Count) {
            $spans = $tmpSpans
            $probeY = $py
            break
        }
    }
    if ($spans.Count -ne $names.Count) {
        throw ("Expected {0} tiles in {1} but found {2} (best probe). Tweak thresholds." -f
               $names.Count, (Split-Path -Leaf $pngPath), $spans.Count)
    }
    Write-Host ("Probe row: {0}  ({1} tiles)" -f $probeY, $spans.Count)

    $tiles = @()
    for ($i = 0; $i -lt $spans.Count; $i++) {
        $span = $spans[$i]
        [int]$x0 = $span[0]
        [int]$x1 = $span[1]

        # Walk vertically along several x positions inside the tile and take
        # the union, so the white inner icon glyph doesn't truncate the bbox.
        [int]$tw = $x1 - $x0 + 1
        $probeXs = @(
            ($x0 + [int]($tw * 0.05)),
            ($x0 + [int]($tw * 0.15)),
            ($x0 + [int]($tw * 0.85)),
            ($x1 - [int]($tw * 0.05))
        ) | Where-Object { $_ -gt 0 -and $_ -lt ($src.Width - 1) } | Select-Object -Unique

        $top = $probeY; $bot = $probeY
        foreach ($px in $probeXs) {
            $t = $probeY
            while ($t -gt 0 -and (& $isSat $px ($t - 1))) { $t-- }
            $b = $probeY
            while ($b -lt ($src.Height - 1) -and (& $isSat $px ($b + 1))) { $b++ }
            if ($t -lt $top) { $top = $t }
            if ($b -gt $bot) { $bot = $b }
        }

        $w = $x1 - $x0 + 1
        $h = $bot - $top + 1
        $tiles += ,([pscustomobject]@{ Name = $names[$i]; X = $x0; Y = $top; W = $w; H = $h })
        Write-Host ("  {0,-10} x={1,4} y={2,4} w={3,3} h={4,3}" -f $names[$i], $x0, $top, $w, $h)
    }

    # Use the largest detected dimension as the canonical square so every tile
    # is preserved at full resolution before downscaling.
    $tileSide = ($tiles | ForEach-Object { [Math]::Max($_.W, $_.H) } | Measure-Object -Maximum).Maximum

    $srcBmp = [System.Drawing.Bitmap]::FromFile($pngPath)
    foreach ($t in $tiles) {
        $square = Render-Square $srcBmp $t.X $t.Y $t.W $t.H $tileSide
        $large  = Resize-Bmp $square 32
        $small  = Resize-Bmp $square 16
        $largePath = Join-Path $outDir ("Ribbon{0}L.bmp" -f $t.Name)
        $smallPath = Join-Path $outDir ("Ribbon{0}S.bmp" -f $t.Name)
        Save-Bmp32 $large $largePath
        Save-Bmp32 $small $smallPath
        Write-Host ("    -> {0}" -f $largePath)
        Write-Host ("    -> {0}" -f $smallPath)
        $large.Dispose(); $small.Dispose(); $square.Dispose()
    }
    $srcBmp.Dispose()
}

Write-Host "Output : $outDir"
foreach ($m in $mockups) {
    Process-Mockup (Join-Path $refDir $m.File) $m.Names $outDir
}
Write-Host ""
Write-Host "Done."
