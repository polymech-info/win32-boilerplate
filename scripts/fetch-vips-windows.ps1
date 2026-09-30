# Download official libvips MSVC dev bundle (x64, "all" loaders) into third_party/.
# https://github.com/libvips/build-win64-mxe/releases
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$third = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force -Path $third | Out-Null

$version = if ($env:MEDIA_VIPS_VERSION) { $env:MEDIA_VIPS_VERSION } else { '8.18.2' }
$name = "vips-dev-x64-all-$version.zip"
$url = "https://github.com/libvips/build-win64-mxe/releases/download/v$version/$name"
$zip = Join-Path $third $name

Write-Host "Downloading $url ..."
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
Write-Host "Extracting to $third ..."
Expand-Archive -Path $zip -DestinationPath $third -Force
Write-Host "Done. CMake will pick up third_party/vips-dev-* automatically."
