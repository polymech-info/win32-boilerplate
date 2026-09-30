# `--ui-next` startup time & layout flicker — investigation notes

This document is an **architecture-level investigation** of why the Win32++ ribbon UI can take on the order of **2–4 seconds** to feel ready and why users see **layout / repaint flicker** during that window. It is meant to line up with the behaviour described in [win32xx-ui.md](win32xx-ui.md) and to point at concrete code paths to profile or change next.

No ETW/CPU traces were captured in-repo for this pass; treat the list below as **ranked hypotheses** to validate with a profiler (Visual Studio *CPU Usage*, *Events Viewer* for `Microsoft-Windows-DWM*`, or WPA) and a stopwatch around candidate sites.

---

## 1. End-to-end startup order (what runs before the first idle frame)

High-level flow (see [win32xx-ui.md §4](win32xx-ui.md) for the full diagram):

1. **CLI** → `media::win::launch_ui_next()` (`src/win/ui_next/launch_ui_next.cpp`)
   - `CoInitializeEx` (apartment)
   - `vips_init("")` — loads **libvips** and **GLib** type system on the main thread (required; see comment in file)
   - `pmui::ui_font_init_from_settings()` — reads `settings.json` for font extras
   - `pmui::theme_init_from_settings()` — reads `settings.json` again for theme
   - `pmui::enable_app_dark_mode(true)` — `uxtheme` ordinals, must run before first `HWND` (per comment)
   - Optional: UI singleton mutex + `WM_COPYDATA` bridge
2. **`CPmImageApp::InitInstance()`** (`src/win/ui_next/App.cpp`) — only calls `m_frame.Create()`.
3. **`CMainFrame` constructor** (`Mainfrm.cpp`) — `LoadPresets()` parses **entire** `settings.json` again (transform `prompt_presets`).

At this point the frame’s `Create()` path runs: registry settings for the frame, then `CRibbonDockFrame::Create` **or** `CDockFrame::Create` (own-ribbon). Win32++ eventually raises **`UWM_WINDOWCREATED`** and **`OnInitialUpdate()`** (`Mainfrm_layout.cpp`).

4. **`OnInitialUpdate()`** (heavy):
   - Restore dock topology from registry **or** `BuildDefaultDockLayout()` — many child dock `HWND`s
   - Lazy-adopt **Find / Duplicate / Chat** docks if missing from older profiles
   - `SetupDockContainers` / caption heights
   - **`LoadLayout()`** — another full **`load_settings_utf8`** + `SetWindowPlacement` + `ApplySavedPanelVisibility` (hide panels per JSON) + Explorer folder seed
   - **`ApplyAppearance()`** — full theme + font + per-dock `RedrawWindow` (see §3)
   - Log sink target, command bridge, **`RebuildStatusBarParts()`**
   - **`SetTimer(..., 150ms)`** → `OnDeferredPostLayoutInit` (session replay, `FlushPendingStartupIfAny`, file-tree browse)

Shell **Explorer** (`FEATURE_FILETREE`): `CExplorerBrowserView` posts **`WM_EB_INIT`** so `IExplorerBrowser` work is not fully synchronous with the first client layout (see `FileTreePanel.h` / `FileTreePanel.cpp`).

**Chat / Web** (`FEATURE_CHAT_WEB`): `CDockChatWeb` is **created and hidden** at startup, but the **WebView2** host’s `CChatWebView::OnCreate` still runs and starts **`CreateCoreWebView2EnvironmentWithOptions`** (async, but not free). That can move wall-clock time and compete with the main thread’s first paints.

---

## 2. Why 2–4 seconds is plausible (suspected cost centers)

| Area | What to verify | Code / notes |
|------|-----------------|----------------|
| **Repeated `settings.json` I/O** | Count decrypt + JSON parse in one cold start | `load_settings_utf8` is invoked from: `ui_font_init_from_settings` → `load_appearance`; `theme_init_from_settings` → `load_appearance` **again**; `LoadPresets` (full parse); `ApplyAppearance` calls both font + theme init **again**; `load_window_layout` → another full read. PME1 path: file read + `sodium` + `crypto_secretbox_open` (`settings_store.cpp`). |
| **libvips + GLib** | Cost of `vips_init` on a cold cache | `launch_ui_next.cpp` |
| **Many HWNDs** | `SettingsPanel` alone builds a large static control set per mode; docks × list views × edits | `SettingsPanel_core.cpp` |
| **Ribbon** | Stock: `IUIApplication` + BML. Own: `BuildRibbonLayout` + toolbar bitmaps | `OnCreate` in `Mainfrm.cpp` (`FEATURE_USE_OWN_RIBBON`) |
| **DWM / dark mode** | `DwmSetWindowAttribute`, `allow_dark_mode_for_window_tree`, per-docker title bars | `ApplyAppearance` |
| **Shell** | `CoCreateInstance(CLSID_ExplorerBrowser)` + `Initialize` + first navigation | `FileTreePanel.cpp` on `WM_EB_INIT` |
| **GDI+** | Central `CFileViewer` — GDI+ startup in view lifetime | `FileViewer.cpp` |
| **WebView2** (if enabled) | Environment + controller, even for a **hidden** chat dock | `ChatWebPanel.cpp` `CChatWebView::OnCreate` |

**Micro-profiling idea:** add temporary `QueryPerformanceCounter` around `launch_ui_next` phases, `CMainFrame` ctor, `CDockFrame::Create`, `OnInitialUpdate`, `LoadLayout`, `ApplyAppearance`, and the first `WM_PAINT` for the main HWND — no need to ship it; dev-only logging is enough to split “before first paint” vs “after”.

---

## 3. Why the UI “flickers” (layout / paint churn)

### 3.1 Multiple layout and visibility passes

- **Registry-driven docks** are built and shown; then **`LoadLayout()`** applies **`SetWindowPlacement`** and **`ApplySavedPanelVisibility`**, which **hides** panels that should be closed. Any frame that was briefly visible in an intermediate state can **jump** (size/position and which columns exist).
- **`OnDeferredPostLayoutInit`** fires **150 ms** later to finish queue seeding and (when applicable) file-tree navigation — a **second** visible layout/selection pass (`kPostLayoutInitTimerId` in `Mainfrm_layout.cpp` / `Mainfrm.h`).

### 3.2 `ApplyAppearance()` is a full visual reset

`ApplyAppearance` ( `Mainfrm.cpp` ) intentionally:

- Re-reads font + theme from disk (`ui_font_init_from_settings` / `theme_init_from_settings`).
- Walks **every dock** with `apply_font_to_tree` + **`RedrawWindow(..., RDW_INVALIDATE \| RDW_ALLCHILDREN \| RDW_ERASE)`** — a **full subtree erase + invalidate** (comments nearby explain why synchronous `RDW_UPDATENOW` was avoided for *runtime* theme changes; on **first** show it can still look like a flash as everything repaints in one go).
- Calls `apply_window_theme_recursive` on large subtrees (Explorer dock excluded; Chat WebView has extra chrome refresh).

There is an **`InvalidateRect` on the main frame** at the end. Together with dock invalidations, the user can perceive **one global flicker** right when the window becomes usable.

### 3.3 Explorer panel fills asynchronously

The file list is not guaranteed to be populated on the first frame: deferred **`WM_EB_INIT`**, then Shell view creation and navigation. Users often see an **empty or partial** tree/list, then **content** — that reads as flicker or “late layout” even when the dock geometry is stable.

### 3.4 Status bar and timers

- **`RebuildStatusBarParts`** resizes status segments (`SB_SETPARTS`); on **`WM_SIZE`** the handler calls it again (`Mainfrm.cpp` WndProc path) — normal, but it means **status layout work tracks window resize** during the initial show.
- A **5 s** stats timer starts in `OnInitialUpdate`; not the first-order flicker cause, but it adds steady background work after startup.

### 3.5 Own-ribbon strip

For **`FEATURE_USE_OWN_RIBBON`**, `OnCreate` sizes the custom strip and calls **`RecalcLayout()`** once; `GetViewRect` then excludes the strip. Any mismatch between first client layout and `OnInitialUpdate`’s final dock recalculation can add **one vertical jump** of the content area (worth checking with/without own ribbon in A/B).

---

## 4. Redundant work that is safe to target first

These are **low-risk to identify** in code review and **high impact** if optimized:

1. **Single `settings.json` load per process start** (or per frame `Create`) — pass parsed `AppearanceSettings` + `WindowLayout` + `prompt_presets` into `ApplyAppearance` / `LoadLayout` / `LoadPresets` instead of 5+ separate `load_settings_utf8` paths.
2. **Avoid re-calling `theme_init_from_settings` + `ui_font_init_from_settings` inside `ApplyAppearance` on the first run** if launch already set global state — or make the second call a **no-op** when a fingerprint (mtime / hash of decrypted JSON) has not changed.
3. **Defer WebView2** until the user first opens the Chat panel (larger refactor: create the dock on demand) — only relevant for `FEATURE_CHAT_WEB` builds.
4. **Optional: suppress intermediate paints** during `OnInitialUpdate` (e.g. `WM_SETREDRAW` on the frame, or `ShowWindow(SW_SHOWNA)` only after `LoadLayout`+`ApplyAppearance` complete) — must be tested; can interact badly with Win32++ dock sizing.

---

## 5. Relationship to [win32xx-ui.md](win32xx-ui.md)

That doc’s **§4 (Startup sequence)** and **§9 (Persistence — Restore)** describe the *intended* order: registry docks → JSON placement/visibility → `ApplyAppearance`. This note adds **where time goes** and **why the visual result can feel janky** (multiple full repaints, deferred Shell/WebView, timer-based follow-up). Any improvement should preserve the invariants there (e.g. `enable_app_dark_mode` before the first `HWND`, `vips_init` on the main thread).

---

## 6. Suggested validation checklist (for the next pass)

- [ ] CPU sample: `launch_ui_next` → first `OnIdle` / stable frame
- [ ] Count `load_settings_utf8` / `decode_settings_raw_bytes` on one cold start
- [ ] Optional: `FEATURE_USE_OWN_RIBBON` vs stock ribbon compare
- [ ] Optional: `FEATURE_CHAT_WEB` on vs off — WebView2 dominates?
- [ ] DWM/Resize: one screen recording with **“Show window contents while dragging”** off to see if live-resize is amplifying flicker
- [ ] User-perceived: time until Explorer shows **any** file row vs time until main window appears

This file is **investigation only**; implementation work should be tracked in issues/PRs with before/after timings from the same machine.
