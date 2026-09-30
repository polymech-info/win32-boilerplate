

-----------
Full Report
-----------


# macOS / SSH build helpers (Windows → VMware guest)

Run **`cmake`** for the parent [packages/media/cpp](../) tree **on a Mac** from **this Windows** checkout. Typical setup: the repo is a **VMware shared folder** on the guest, e.g.

`/Volumes/VMware Shared Folders/<share>/Desktop/…/packages/media/cpp`

and you connect with **`ssh`** (e.g. Host `osx` in `~/.ssh/config`). See [../docs/ssh-key-auth.md](../docs/ssh-key-auth.md) and [../docs/osx-vm.md](../docs/osx-vm.md) §4.1.

## CLI first (no Win32 UI)

**Goal on macOS:** get **`pm-image`** (CLI, `serve`, `ipc`, …) working with the same **`src/`** + **`packages/`** code as Windows. **`--ui-next`**, **`--ui`**, and the Win32++ ribbon are behind **`#if defined(_WIN32)`**; a **native Mac UI** is a **separate** future target (AppKit / SwiftUI), not a port of that surface. See [../docs/osx.md](../docs/osx.md) and [../docs/osx-app.md](../docs/osx-app.md).

After **`npm run build`**, the binary is written to **`dist-osx/pm-image`** under the **source** tree on **macOS** (see root [CMakeLists.txt](../CMakeLists.txt): `PM_DIST_DIR` is **`dist-osx/`** on Apple, **`dist/`** elsewhere). On a shared folder that path is still on **HGFS**; only the **CMake build tree** and **FetchContent** are forced to **local** guest disk (below).

| On the guest | Path |
|--------------|------|
| **Source** | Shared folder, e.g. `…/packages/media/cpp` |
| **Build / Ninja** | **`~/.pm-media-build/macos-release`** (local VMware disk) |
| **FetchContent** | **`~/.pm-media-fetchcontent`** |
| **Binary (macOS CMake)** | **`…/packages/media/cpp/dist-osx/pm-image`** (next to sources) |
| **App bundle (optional)** | **`…/dist-osx/pm-image-gui.app`** — `npm run configure:gui` then `npm run build:gui` (see [../docs/osx-ui.md](../docs/osx-ui.md)) |
| **CodeEdit (vendored)** | **`…/dist-osx/CodeEdit.app`** — `npm run configure:codeedit` then `npm run build:codeedit` (xcodebuild; [CodeEdit/CMakeLists.txt](CodeEdit/CMakeLists.txt) stages `pm-media` to `dist-osx/polymech-cmake-libs/` for Xcode) |

Run the CLI over SSH: **`npm run run:pm-image`** (forwards to `dist-osx/pm-image` on the guest; add args via **`npm run ssh -- dist-osx/pm-image --help`**).

## Environment

| Variable | Meaning | Default (example) |
|----------|---------|-------------------|
| `PM_OSX_SSH` | First argument to `ssh` (Host alias or `user@ip`); also accepts `local` to never use ssh | `osx` (Windows); ignored on **macOS** unless `PM_OSX_FORCE_SSH=1` |
| `PM_OSX_CPP_ROOT` | Absolute path to **`packages/media/cpp`** on the machine that runs the build | On a **Mac** (Node `darwin`), defaults to the clone that contains `osx/` (no env needed) |
| `PM_LOCAL` | `1` — run the shell script **locally** (no `ssh`) even on Windows (rare) | unset |
| `PM_USE_SSH` | `0` — same as no ssh | unset |
| `PM_OSX_FORCE_SSH` | `1` — on a **Mac**, still use `ssh` to the host in `PM_OSX_SSH` (e.g. test from laptop into another box) | unset |

**Copy [env.example](env.example) values into your user or shell profile** and adjust **`PM_OSX_CPP_ROOT`** to your share name and layout.

On the **Mac**, add **`dist-osx`** to your shell **`PATH`** so `pm-image` works from any directory (after you have built at least once). In **`~/.zshrc`** or **`~/.bash_profile`**:

```bash
export PATH="$PM_OSX_CPP_ROOT/dist-osx:$PATH"
```

Use your real clone path inside the quotes if you do not set **`PM_OSX_CPP_ROOT`** (see [env.example](env.example)).

Requires **OpenSSH** `ssh` on Windows (`%SystemRoot%\System32\OpenSSH\ssh.exe`).

## On the Mac (once)

- **Xcode CLT** — if missing: `xcode-select --install` (see [../docs/osx.md](../docs/osx.md)).
- **Homebrew** is assumed already installed. Then run **[install.sh](install.sh)** in this directory (or `bash install.sh`):
  ```bash
  cd /path/to/polymech-mono/packages/media/cpp/osx
  chmod +x install.sh
  ./install.sh
  ```
  This installs **cmake**, **pkg-config**, **vips**, and **ninja** via `brew install`.
- The **`release-macos`** preset ([CMakePresets.json](../CMakePresets.json)) keeps **Ninja/CMakeCache** and **object files** on **`$HOME/.pm-media-build/macos-release`** (guest **local** disk), not on the shared folder, so VMware **HGFS** does not break the build. **FetchContent** uses **`~/.pm-media-fetchcontent`**. Reconfigure after big preset changes: **`npm run clean:all`** then **`npm run configure`**.

**From Windows** (over SSH): `npm run install:mac` runs [install.sh](install.sh) on the guest.

## Usage (from this `osx/` directory)

```powershell
cd path\to\polymech-mono\packages\media\cpp\osx
npm run configure
npm run build
npm run rebuild
```

- **`npm run ssh` —** `node ./scripts/run-ssh.cjs -- <remote command>`

Example:

```powershell
npm run ssh -- cmake --version
```

(Remote commands run with **`cd` to `PM_OSX_CPP_ROOT`** (source) first.)

## Parity with repo root [package.json](../package.json)

All cmake npm scripts use preset **`release-macos`**.

| `osx` script | Remote intent |
|--------------|----------------|
| `configure` | `cmake --preset release-macos -G Ninja` |
| `build` / `buildf` | `cmake --build --preset release-macos` |
| `rebuild` | configure + build in one **SSH** session |
| `rebuild:retail` | same as root `build:retail` flags, remote |
| `build:lib` | `--target pm-media` |
| `configure:gui` | like `configure` but adds **`-DPM_BUILD_MACOS_APP=ON`** (AppKit `.app`) |
| `build:gui` | `--target pm-image-gui` → **`dist-osx/pm-image-gui.app`** (run **`configure:gui`** once first) |
| `configure:codeedit` | like `configure` + **`-DPM_BUILD_CODEEDIT=ON`** (Xcode + vendored [CodeEdit](CodeEdit) tree) |
| `build:codeedit` | `--target pm-codeedit` → **`dist-osx/CodeEdit.app`**; copies **`libpm-media.a` (or dylib)** to **`polymech-cmake-libs/`** |
| `run:pm-image` | `dist-osx/pm-image` (pass more args with `npm run ssh -- dist-osx/pm-image …`) |
| `clean:build` | `cmake --build --target clean` |
| `clean:mac` | `rm -rf ~/.pm-media-build/macos-release` |
| `clean:fetch` | `rm -rf ~/.pm-media-fetchcontent` |
| `clean:all` | `clean:mac` + `clean:fetch` (does not delete Windows `build\release` on the host) |
| `test:all` | `ctest` in `~/.pm-media-build/macos-release` |
| `build:test-app` | `bash osx/pm-image/build_test_app.sh` — **standalone** Xcode `pm-image.app` (dock prototype; no CMake) |

`--config` is for multi-config generators; **Ninja** + this preset uses **`CMAKE_BUILD_TYPE=Release`**.

## Files

| | |
|--|--|
| [install.sh](install.sh) | `brew install` build deps (run on Mac or `npm run install:mac` from Windows) |
| [scripts/run-ssh.cjs](scripts/run-ssh.cjs) | `ssh` + `cd` to source + script on stdin (reliable `bash` on the guest) |
| [CMakeLists.txt](CMakeLists.txt) | Placeholder; real build uses **parent** CMake. |
| [package.json](package.json) | npm scripts (similar role to [../android/pm-image/package.json](../android/pm-image/package.json)) |

## pm-image Wxx docker (Xcode, `pm-image/pm-image`)

Isolated **Swift** AppKit shell under [pm-image/pm-image](pm-image/pm-image): **`WxxDockerKit.swift`** implements a Win32++-style dock/split workbench (see [../docs/osx-dockers.md](../docs/osx-dockers.md)), optional floating tool windows, and a **Log** panel. Build with [pm-image/build_test_app.sh](pm-image/build_test_app.sh) or **`npm run build:test-app`** (from this `osx/` directory, uses [scripts/run-ssh.cjs](scripts/run-ssh.cjs) the same way as other targets).

### Stack (high level)

| Layer | Role |
|-------|------|
| **Entry** | `pm_imageApp.swift`: `App` with `@NSApplicationDelegateAdaptor(AppDelegate)`; **`Settings { EmptyView() }`** is only so SwiftUI can own the app object — **no** SwiftUI window scene; all UI is AppKit. |
| **AppKit shell** | `AppDelegate` → `WxxDockAppLauncher.show()` → `WxxMainDockWindow` (one **NSWindow** + `WxxDockHost.splitController` as `contentViewController`). |
| **Layout** | Nested **`NSSplitViewController`**: horizontal strip = left \| **center** \| right; root split stacks that strip **above** a bottom strip. `DockContainerViewController` items host each side/bottom **panel** (`NSViewController`). |
| **Panels** | One `DemoPanelViewController` + `PanelTitlebarDragView` per `PanelID`; all owned by `WxxDockHost` / `DockPanel` (see below). |
| **Redock UI** | Separate **borderless `NSWindow`** (redock “L / R / bottom / cancel” overlay) above floats by **window level** (`.popUpMenu`), not by `order(_:relativeTo:)` to float windows. |

C++/CMake **does not** link this target; it is `xcodebuild` only (see the script above).

### Ownership (strong references)

- **`WxxDockHost`** (owned by `WxxMainDockWindow`, lives for the app session) holds:
  - `splitController` and the **three** `DockContainerViewController`s (left / right / bottom) + `CenterContentViewController`.
  - `panelById: [PanelID: DockPanel]` — every panel in the spec.
- **`DockPanel`**: **strong** `viewController: NSViewController` for the life of the app. Optional **`floatingPanel: NSWindow?`** for the “tool float” for that panel (only one at a time).
- **Main workbench `NSWindow`**: `contentViewController` = **root `NSSplitViewController`** (normal document shell). **Not** the same pattern as tool floats.
- **Tool float `NSWindow`**: does **not** use `contentViewController` for the panel. The float window’s `contentView` is a plain **`NSView`** with the panel’s **`vc.view`** constrained to edges (`installFloatingWindowContent`). The **`NSViewController` stays owned by `DockPanel`**; the window only hosts the view. This avoids reusing the same VC as a window’s `contentViewController` and then re‑embedding into the split, which is fragile in AppKit (historically tied to `objc_release` / pool-drain issues during teardown).

### View reparenting (dock ↔ float)

1. **Float (detach):** `DockPanel.floatingPanel` = new `NSWindow`; `installFloatingWindowContent` adds `viewController.view` to the float’s `contentView`; set title on the `NSWindow` explicitly.
2. **Dock (attach from float):** `clearFloatingWindowContent` (remove subviews, clear `contentView`), `orderOut`, `panel.floatingPanel = nil`, defer `close` on the empty shell; `DockContainerViewController.embed` does `addChild` + `addSubview` of the same `viewController` into the split’s container.
3. **`embed`:** if replacing a prior child in a slot, old child `removeFromSuperview` + `removeFromParent`; new child’s view stripped from any superview, `removeFromParent` only if `parent != nil`, then `addChild` + constraints.

### Events (input paths)

| Mechanism | Used for | Notes |
|-----------|----------|--------|
| **`NSApp.nextEvent(…, inMode: .eventTracking, …)`** loop started from **`mouseDown`** on the titlebar | **Drag to float / move float / redock** | Delivers `leftMouseDragged` / `leftMouseUp` while the user drags, including after `detach` moves the view to another **key** window. **Replaces** `NSEvent.addLocalMonitorForEvents` on purpose: the local monitor re-enters AppKit on every event and is easy to mis-pair with removeMonitor; a tracking loop has **no** long-lived “listener” registration. |
| **No** `NSEvent` local/global monitor for this path | | |
| **Redock overlay** | `setRedockHintVisible` / `updateRedockHintPointer` | Shown for floating drags; pointer drives `RedockHintGridView` hit-zones. |
| **`InAppLog`** | `Notification.Name("InAppLog.didChange")` | Buffer append, then **async** `NotificationCenter.post` on main so observers (e.g. Log’s `NSTextView`) do not run in the same synchronous stack as `embed` / reparent. |

`DockManager` is **`NSWindowDelegate`** for float windows: **`windowWillClose`** → `hide` for the matching panel (closing the float with the red traffic light).

## Troubleshooting

### clang errors, “null character”, “not valid UTF-8”, or **segfault** while compiling a `.cpp` on the **shared** path

**Cause:** **VMware HGFS** (host folder mounted in the guest) can confuse **file reads** for the compiler: garbage or stale bytes, and in bad cases **clang** crashes, often near the **end** of a large file.

**Fix:** build from a **copy of the tree on the guest’s local disk**, not from `/Volumes/VMware Shared Folders/...` as **`CMAKE_SOURCE_DIR`**.

1. In the **guest** (once):  
   `git clone <your-remote> ~/polymech-mono`  
   (or `rsync -a --delete` from the share into `~/polymech-mono` when you need to sync.)
2. Set **`PM_OSX_CPP_ROOT`** to `~/polymech/polymech-mono/packages/media/cpp` (or the path you use), **not** the VMware share.
3. Run **`npm run rebuild`** from Windows as usual. **Build** and **FetchContent** are already on **`$HOME/.pm-media-*`**.

You can still **edit** on the host; sync to the local clone when you need a clean build, or work in the clone in the guest over SSH + editor.

## Porting note

A future **macOS .app** can add targets beside this stub; see [../docs/osx-app.md](../docs/osx-app.md) and the UI backlog [../docs/osx-ui.md](../docs/osx-ui.md) (CMake-first, AppKit; Xcode optional).
