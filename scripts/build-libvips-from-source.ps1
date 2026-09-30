# Clone libvips and run Meson for -Ddefault_library=static.
# On Windows, Meson will almost always *fail* without GLib/pkg-config in PKG_CONFIG_PATH.
# Prefer: WSL  bash scripts/build-libvips-static-wsl.sh
# Or: keep third_party vips-dev-*.zip (MSVC DLL bundle) from fetch-vips / CMake.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts/build-libvips-from-source.ps1
$ErrorActionPreference = 'Stop'
$version = if ($env:LIBVIPS_VERSION) { $env:LIBVIPS_VERSION } else { '8.18.2' }
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root 'third_party/libvips-src'
$build = Join-Path $root 'third_party/libvips-meson-build-win'
$prefix = Join-Path $root 'third_party/vips-static-prefix-win'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Write-Error "git is required." }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { Write-Error "ninja is required." }

$runMeson = {
  param($arguments)
  if (Get-Command meson -ErrorAction SilentlyContinue) {
    return & meson @arguments
  }
  $py = $null
  if (Get-Command py -ErrorAction SilentlyContinue) { $py = "py" }
  elseif (Get-Command python -ErrorAction SilentlyContinue) { $py = "python" }
  if (-not $py) { Write-Error "Install meson: pip install meson, or add meson to PATH." }
  return & $py -m mesonbuild.mesonmain @arguments
}

if (-not (Test-Path (Join-Path $src '.git'))) {
  Write-Host "Cloning libvips v$version ..."
  git -c advice.detachedHead=false clone --depth 1 --branch "v$version" https://github.com/libvips/libvips.git $src
} else {
  Push-Location $src
  try {
    git fetch --depth 1 origin "v$version" 2>$null
    git -c advice.detachedHead=false checkout "v$version"
  } finally { Pop-Location }
}

if (Test-Path $build) { Remove-Item -Recurse -Force $build }
New-Item -ItemType Directory -Force -Path $prefix | Out-Null

$setup = @(
  'setup', $build, $src, '--prefix', $prefix, '-Ddefault_library=static', '-Dbuildtype=release', '-Dintrospection=disabled'
)
if ($env:MESON_EXTRA) { $setup += $env:MESON_EXTRA -split '\s+' }

# MSVC environment for Meson
$vcvars = $null
$vsw = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -find 'VC\Auxiliary\Build\vcvars64.bat' 2>$null
if ($vsw) { $vcvars = $vsw[-1] }

if ($vcvars) {
  Write-Host "Meson (with $vcvars) + compile ..."
  $mesonline = "meson " + ($setup -join ' ')
  $c = "call `"$vcvars`" && $mesonline"
  if (-not (Get-Command meson -ErrorAction SilentlyContinue)) {
    $c = "call `"$vcvars`" && py -3 -m mesonbuild.mesonmain " + ($setup -join ' ')
  }
  cmd /c $c
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  $c2 = if (Get-Command meson -ErrorAction SilentlyContinue) {
    "call `"$vcvars`" && meson compile -C `"$build`""
  } else { "call `"$vcvars`" && py -3 -m mesonbuild.mesonmain compile -C `"$build`"" }
  cmd /c $c2
} else {
  Write-Warning "vcvars64.bat not found; Meson will use the current environment."
  & $runMeson $setup
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & $runMeson @('compile', "-C$build")
}

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host ""
Write-Host "Build dir: $build"
Write-Host "If the above failed on Windows, use WSL:  bash scripts/build-libvips-static-wsl.sh"
Write-Host "To install into prefix and use with CMake, run:"
if (Get-Command meson -ErrorAction SilentlyContinue) {
  Write-Host "  meson install -C `"$build`""
} else {
  Write-Host "  py -3 -m mesonbuild.mesonmain install -C `"$build`""
}
Write-Host "  setx VIPS_ROOT $prefix"
Write-Host "  setx PKG_CONFIG_PATH (see scripts/build-libvips-static-wsl.sh header) if pkg-config is available."
