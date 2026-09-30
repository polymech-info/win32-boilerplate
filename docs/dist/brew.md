Use this as handoff prompt:

````markdown
# Task: Cross-platform CMake/CPack publishing setup for C++ CLI first

We are developing a C++ project primarily on **Windows/Win32**, but building/releasing for:

- Windows
- Linux
- macOS

Current orchestration is done through **central npm scripts**. We want to keep npm as the top-level command runner, but use proper native tooling underneath:

- CMake for configure/build/install
- CPack for packaging
- GitHub Releases for artifacts
- Later: Homebrew, Scoop/winget, Linux packages

Focus on **CLI first**. GUI/app bundle support can come later.

---

# Goal

Create a clean, chapter-by-chapter implementation plan for cross-platform build + package + publish.

The plan should cover:

1. Project layout
2. CMake install rules
3. CPack setup
4. npm script orchestration
5. Windows build/package
6. Linux build/package
7. macOS build/package
8. GitHub Actions release workflow
9. Future package manager publishing

Keep it practical and copy-paste friendly.

---

# Current assumptions

- C++ project
- Uses CMake
- Developer mainly works on Windows
- Linux/macOS builds happen locally or in CI
- npm scripts are used as central commands
- CLI binary is the first release target
- GUI app may be added later
- Prefer simple artifacts first:
  - Windows: `.zip`
  - Linux: `.tar.gz`
  - macOS: `.tar.gz`
- Later:
  - Windows: Scoop / winget
  - Linux: `.deb`, `.rpm`, AppImage
  - macOS: Homebrew formula / cask

---

# Chapter 1 — Recommended project layout

Propose a clean layout like:

```txt
project/
  CMakeLists.txt
  package.json
  src/
  include/
  cmake/
    Packaging.cmake
  scripts/
    build.mjs
    package.mjs
    release.mjs
  dist/
  build/
````

Explain what belongs where.

---

# Chapter 2 — CMake basics

Create a minimal but production-friendly `CMakeLists.txt`.

Requirements:

* `project(... VERSION x.y.z)`
* `add_executable(mytool ...)`
* C++ standard setting
* platform-safe output name
* proper `install(TARGETS ...)`
* optional `install(FILES README.md LICENSE ...)`

Example target:

```cmake
add_executable(mytool
  src/main.cpp
)

target_compile_features(mytool PRIVATE cxx_std_20)

install(TARGETS mytool
  RUNTIME DESTINATION bin
)
```

---

# Chapter 3 — CPack setup

Create `cmake/Packaging.cmake`.

Start simple:

* Windows: ZIP
* Linux: TGZ
* macOS: TGZ

Example:

```cmake
set(CPACK_PACKAGE_NAME "mytool")
set(CPACK_PACKAGE_VENDOR "PolyMech")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "hello@polymech.io")

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "TGZ")
elseif(UNIX)
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
```

Then include it from root `CMakeLists.txt`:

```cmake
include(cmake/Packaging.cmake)
```

---

# Chapter 4 — npm scripts as central interface

Create `package.json` scripts that wrap CMake.

Example:

```json
{
  "scripts": {
    "clean": "node scripts/clean.mjs",
    "configure": "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release",
    "build": "cmake --build build --config Release",
    "install:local": "cmake --install build --prefix dist/install",
    "package": "cpack --config build/CPackConfig.cmake",
    "release:local": "npm run configure && npm run build && npm run package"
  }
}
```

Mention Windows multi-config generators vs Linux/macOS single-config generators.

Important distinction:

```bash
cmake --build build --config Release
```

is needed for Visual Studio / multi-config generators.

---

# Chapter 5 — Windows CLI packaging

Explain Windows-specific details:

* MSVC / Visual Studio generator
* `.exe` output
* runtime DLLs if dynamically linked
* ZIP as first package format
* later NSIS / WiX / MSI

Recommended command:

```bash
npm run configure
npm run build
npm run package
```

Potential Windows-specific CMake options:

```cmake
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```

Discuss static vs dynamic runtime briefly.

---

# Chapter 6 — Linux CLI packaging

Explain Linux-specific details:

* GCC/Clang
* `.tar.gz` first
* later `.deb`, `.rpm`, AppImage
* avoid assuming glibc compatibility if building on newest Ubuntu
* for portable binaries, build on older LTS or container

Example generators:

```cmake
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "PolyMech")
```

But recommend starting with TGZ only.

---

# Chapter 7 — macOS CLI packaging

Explain macOS-specific details:

* arm64 vs x86_64 vs universal binaries
* unsigned CLI binaries usually work, but signing/notarization is better for public distribution
* Homebrew formula later
* GUI `.app` later needs signing/notarization seriously

CMake options:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
```

Universal:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

Mention that universal builds depend on dependencies also supporting both architectures.

---

# Chapter 8 — GitHub Actions matrix

Create a GitHub Actions workflow that builds and packages on:

* `windows-latest`
* `ubuntu-latest`
* `macos-latest`

Use npm as the entry point.

Example flow:

```yaml
name: release

on:
  push:
    tags:
      - "v*"

jobs:
  build:
    strategy:
      matrix:
        os: [windows-latest, ubuntu-latest, macos-latest]

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 22

      - name: Configure
        run: npm run configure

      - name: Build
        run: npm run build

      - name: Package
        run: npm run package

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: package-${{ matrix.os }}
          path: |
            *.zip
            *.tar.gz
            *.dmg
            *.pkg
```

Then add release upload step using `softprops/action-gh-release`.

---

# Chapter 9 — Artifact naming

Recommend deterministic artifact names:

```txt
mytool-v1.0.0-windows-x64.zip
mytool-v1.0.0-linux-x64.tar.gz
mytool-v1.0.0-macos-arm64.tar.gz
mytool-v1.0.0-macos-universal.tar.gz
```

Show how to set this via CPack:

```cmake
set(CPACK_PACKAGE_FILE_NAME
  "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
```

Then normalize names if needed from npm scripts.

---

# Chapter 10 — Future Homebrew formula

Explain that Homebrew should consume GitHub Release artifacts or source tarballs.

Formula example:

```ruby
class Mytool < Formula
  desc "PolyMech CLI tool"
  homepage "https://polymech.io"
  url "https://github.com/polymech/mytool/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "..."

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    system "#{bin}/mytool", "--version"
  end
end
```

Recommend own tap first:

```txt
polymech/homebrew-tap
```

---

# Chapter 11 — Future Scoop / winget

Explain Windows options:

## Scoop

Good for CLI tools.

Manifest points to GitHub Release ZIP:

```json
{
  "version": "1.0.0",
  "url": "https://github.com/polymech/mytool/releases/download/v1.0.0/mytool-v1.0.0-windows-x64.zip",
  "bin": "bin/mytool.exe",
  "hash": "..."
}
```

## winget

Better for mainstream Windows distribution, installers, GUI apps.

Can be added later.

---

# Chapter 12 — Future Linux packages

Start with `.tar.gz`.

Later add:

* `.deb` for Debian/Ubuntu
* `.rpm` for Fedora/RHEL
* AppImage for desktop GUI apps
* AUR for Arch users

Do not over-invest early unless users request it.

---

# Chapter 13 — CLI-first release checklist

Create final checklist:

```txt
[ ] CMake project version set
[ ] CLI supports --version
[ ] install(TARGETS ...) works
[ ] cpack works locally
[ ] npm run release:local works
[ ] GitHub Actions builds all platforms
[ ] Release artifacts uploaded
[ ] Checksums generated
[ ] Homebrew formula prepared
[ ] Scoop manifest prepared
```

---

# Chapter 14 — Later GUI support

Briefly explain changes when adding GUI:

## macOS

* `MACOSX_BUNDLE`
* `.app`
* `.dmg`
* codesign
* notarization
* Homebrew cask

## Windows

* `.exe`
* icon/resource file
* installer
* signing certificate
* winget

## Linux

* AppImage
* `.desktop` file
* icon install rules

But keep this chapter brief. CLI first.

---

# Output style

Return:

* chapter-by-chapter guide
* copy-paste ready snippets
* pragmatic recommendations
* avoid overengineering
* assume CLI first
* keep npm as orchestration layer

```
```
