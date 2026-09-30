## cross-platform shortcuts (scripts/platform-build.mjs)

| script                    | what it does                                              |
|---------------------------|-----------------------------------------------------------|
| `npm run configure`       | cmake configure for current OS                            |
| `npm run build:cli`       | incremental build — CLI (`pm-image-cli` on Windows, `pm-image` elsewhere) |
| `npm run build:ui`        | incremental build — GUI (pm-codeedit / win32 full)        |
| `npm run build:all`       | incremental build — web embeds, libs, CLI, and UI         |
| `npm run configure:osx`   | SSH → mac:  git pull + configure                          |
| `npm run build:cli:osx`   | SSH → mac:  git pull + build:cli                          |
| `npm run build:ui:osx`    | SSH → mac:  git pull + build:ui (pm-codeedit)             |
| `npm run configure:linux` | SSH → linux-vm: git pull + configure                      |
| `npm run build:cli:linux` | SSH → linux-vm: git pull + build:cli                      |
| `npm run build:remote`    | SSH → both remotes sequentially, build:cli                |

Or call directly with any command + target:
```
node scripts/platform-build.mjs <configure|build:cli|build:ui|build:all> [--remote osx|linux|all]
```

---

## osx

host: mc007@mac
dir : /Users/mc007/Desktop/mono-next/packages/media/cpp
build: npm run build:cli:osx  (or build:ui:osx for PixlWiz)
pre  : git pull  — works with dirty tree (ff=only; repo-local rebase=true removed)
note : credentials stored in ~/.git-credentials (Gitea token, credential.helper=store)

## linux

host: polymech@linux-vm
dir : ~/Desktop/mono-next/packages/media/cpp
build: npm run build:cli:linux
pre  : git pull
note : CLI-first release work uses `pm-image`; Linux GUI remains available via `build:ui`.

## win

this host!
dir : C:\Users\zx\Desktop\polymech\polymech-mono\packages\media\cpp
build: npm run build:cli  (builds `dist\pm-image-cli.exe`; use `build:all` for web + libs + CLI + `pm-image.exe`)
pre  : git pull

Windows ships two front doors:

- `pm-image.exe` is the GUI-subsystem application for Explorer/open-with/UI launches.
- `pm-image-cli.exe` is the console-subsystem CLI for CI, pipes, `--help`, stdout/stderr, exit codes, and Ctrl+C.
- `pmi.cmd` is a thin convenience alias to `pm-image-cli.exe`.

---

## Linux Brew — CLI-first release TODOs

Reference: `brew.md` (full cross-platform plan).

### 1. CMake install rules

- [x] `install(TARGETS pm-image RUNTIME DESTINATION bin)` in CMakeLists.txt
- [ ] verify `cmake --install` puts the binary into `dist/install/bin/pm-image`
- [x] optional: `install(FILES README.md DESTINATION share/doc/pm-image)` (root `LICENSE` not present)

### 2. CPack — .tar.gz first

- [x] create `cmake/Packaging.cmake` (or inline in CMakeLists.txt)
- [x] set `CPACK_GENERATOR "TGZ"` for Linux
- [x] set `CPACK_PACKAGE_NAME`, `CPACK_PACKAGE_VENDOR`, `CPACK_PACKAGE_VERSION`
- [x] set deterministic artifact name:
      `pm-image-<version>-linux-x86_64.tar.gz`
      (`CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"`)
- [x] `include(cmake/Packaging.cmake)` from root CMakeLists.txt
- [ ] verify: `cpack --config build/CPackConfig.cmake` produces the tarball

### 3. npm script: package step

- [x] add `"package:linux"` npm script that runs cpack after build
- [ ] verify full pipeline: `npm run build:linux && npm run package:linux`
- [x] tarball lands in a predictable path (e.g. `dist/` or build root)

### 4. CLI --version support

- [x] `pm-image --version` prints version string (from CMake `project(... VERSION x.y.z)`)
- [ ] Homebrew `test` block can call `pm-image --version`

### 5. Homebrew tap + formula

- [ ] create tap repo: `polymech/homebrew-tap` (or `polymech/homebrew-pm-image`)
- [ ] write formula (`pm-image.rb`):
  - `url` → GitHub Release tarball (source or binary)
  - `sha256`
  - `depends_on "cmake" => :build` (if building from source)
  - `def install` → cmake configure/build/install with `std_cmake_args`
  - `test` → `system "#{bin}/pm-image", "--version"`
- [ ] decide: source formula vs bottle (pre-built binary tarball)
  - source: simpler, works on any arch; slow first install
  - bottle: fast install, needs CI to build per-arch bottles
- [ ] test locally: `brew install --build-from-source ./pm-image.rb`

### 6. GitHub Release artifacts

- [ ] tag-triggered workflow (or manual) builds linux tarball
- [ ] upload `pm-image-<ver>-linux-x86_64.tar.gz` to GitHub Release
- [ ] generate sha256 checksum file alongside
- [ ] Homebrew formula url/sha256 point to this release asset

### 7. Portable binary considerations

- [ ] static link libstdc++/libgcc or build on older LTS (Ubuntu 20.04+)
  to avoid glibc version issues on user machines
- [ ] test binary on a clean Ubuntu/Debian/Fedora (docker or VM)
- [ ] `ldd dist/install/bin/pm-image` — check runtime deps

### 8. Later (not now)

- [ ] `.deb` / `.rpm` via CPack generators
- [ ] AppImage (if GUI added)
- [ ] AUR package for Arch
- [ ] Linuxbrew bottle CI (pre-built bottles per arch)
- [ ] arm64/aarch64 cross-build
