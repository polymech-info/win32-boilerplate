Add-Type -AssemblyName System.Drawing

function Convert-PngToBmp {
    param([string]$pngPath, [string]$bmpPath, [int]$targetSize)

    if (-not (Test-Path $pngPath)) {
        Write-Warning "Missing: $pngPath"
        return
    }

    $src = [System.Drawing.Bitmap]::FromFile($pngPath)
    $bmp = New-Object System.Drawing.Bitmap($targetSize, $targetSize, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src, 0, 0, $targetSize, $targetSize)
    $g.Dispose()
    $src.Dispose()

    $bmp.Save($bmpPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
    Write-Output "  $bmpPath"
}

$resDir = $PSScriptRoot + '\res'

Write-Output "Converting icons from famfamfam silk set..."

# Add Files -> image_add.png
Convert-PngToBmp "$resDir\32x32_0500\image_add.png" "$resDir\AddFilesL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0500\image_add.png" "$resDir\AddFilesS.bmp" 16

# Add Folder -> folder_add.png
Convert-PngToBmp "$resDir\32x32_0440\folder_add.png" "$resDir\AddFolderL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0440\folder_add.png" "$resDir\AddFolderS.bmp" 16

# Clear -> cross.png
Convert-PngToBmp "$resDir\32x32_0300\cross.png" "$resDir\ClearL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0300\cross.png" "$resDir\ClearS.bmp" 16

# Resize -> arrow_out.png
Convert-PngToBmp "$resDir\32x32_0060\arrow_out.png" "$resDir\ResizeL.bmp" 32

# AI Transform -> paintbrush.png
Convert-PngToBmp "$resDir\32x32_0700\paintcan.png" "$resDir\TransformL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0700\paintbrush.png" "$resDir\TransformS.bmp" 16

# Model -> cog.png
Convert-PngToBmp "$resDir\32x32_0240\cog.png" "$resDir\ModelL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0240\cog.png" "$resDir\ModelS.bmp" 16

# Aspect -> shape_handles.png
Convert-PngToBmp "$resDir\32x32_0800\shape_handles.png" "$resDir\AspectL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0800\shape_handles.png" "$resDir\AspectS.bmp" 16

# Size -> magnifier_zoom_in.png
Convert-PngToBmp "$resDir\32x32_0560\magnifier_zoom_in.png" "$resDir\SizeL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0560\magnifier_zoom_in.png" "$resDir\SizeS.bmp" 16

# Presets -> book.png
Convert-PngToBmp "$resDir\32x32_0100\book.png" "$resDir\PresetsL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0100\book.png" "$resDir\PresetsS.bmp" 16

# Prompt -> comment_edit.png
Convert-PngToBmp "$resDir\32x32_0260\comment_edit.png" "$resDir\PromptL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0260\comment_edit.png" "$resDir\PromptS.bmp" 16

# Run -> control_play_blue.png
Convert-PngToBmp "$resDir\32x32_0300\control_play_blue.png" "$resDir\RunL.bmp" 32
Convert-PngToBmp "$resDir\16x16_0300\control_play_blue.png" "$resDir\RunS.bmp" 16

Write-Output "Done."
