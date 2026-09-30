# CodeEdit + Polymech (macOS) — current integration notes

This file tracks what has been ported into the vendored `CodeEdit.app` under `osx/CodeEdit`, especially the Polymech workflow and Chat surface.

## Scope and intent

- Keep the same C++ media stack as CLI/Win32 for image operations (via `polymech_bridge` and `pm-media`).
- Mirror the Windows workflow model in CodeEdit:
  - select files/folders in explorer,
  - choose a command in toolbar,
  - adjust settings in inspector,
  - save,
  - run.
- Add a Chat entry point in macOS that opens the web chat UI from `apps/chat`, and pass explorer context (selection/folder) into that UI.

## What is already implemented

### Image preview (navigator)

- [`CodeEdit/Features/Editor/Views/ImageFileView.swift`](CodeEdit/Features/Editor/Views/ImageFileView.swift): zoomable preview with **viewport fit**, deferred initial fit for large assets, **pan** when zoomed, and **ImageIO-based** loading (thumbnail / scaled decode via `PolymechImagePreviewLoader`) to avoid blocking the UI on huge or RAW files.
- Cancel / teardown paths aim to behave safely if the user navigates away while a decode is in flight.

### Status bar (image tab)

- [`CodeEdit/Features/StatusBar/ViewModels/StatusBarViewModel.swift`](CodeEdit/Features/StatusBar/ViewModels/StatusBarViewModel.swift): `imagePreviewMagnification` (zoom % relative to fit), `requestImageViewportFit()`, `clearImagePreviewZoomState()`.
- [`CodeEdit/Features/StatusBar/Views/StatusBarItems/StatusBarFileInfoView.swift`](CodeEdit/Features/StatusBar/Views/StatusBarItems/StatusBarFileInfoView.swift): shows resolution/size plus zoom **%** and a **Fit** control wired to the host view.

### Cold launch, session restore, Welcome vs scratch workspace

- [`CodeEdit/AppDelegate.swift`](CodeEdit/AppDelegate.swift):
  - **`scheduleLaunchOpenHandling()`** waits for `NSApplication.didFinishRestoringWindowsNotification` (with a short fallback delay) so session restoration is not skipped when `NSApp.windows` is still empty.
  - **`recover.workspaces`**: if present, reopens listed workspace paths that still exist; otherwise falls through to Welcome/scratch logic.
  - **`presentWelcomeOrScratchWorkspace()`**: if **Welcome recents** are non-empty, runs normal `handleOpen()` (Welcome / open panel per settings); if **no recents**, opens a scratch folder via **`openDefaultScratchWorkspace()`** (`./tmp-workspace` when cwd is usable, else `~/tmp-workspace`), creates it if needed, and records it in recents.
  - Dock **reopen** with no visible windows uses the same Welcome/scratch helper.

### Where “recent projects” live

- SPM **WelcomeWindow**: `RecentsStore` / `recentProjectBookmarks` in `UserDefaults` (see also [`CodeEditTests/Features/Welcome/RecentProjectsTests.swift`](CodeEdit/CodeEditTests/Features/Welcome/RecentProjectsTests.swift)).

## Polymech command workflow in CodeEdit

- `PolymechCommand` now includes:
  - `resize`, `meta`, `compress`, `find`, `transform`, `chat`.
- `PolymechWorkflowState`:
  - tracks navigator selection (`navigatorSelectionURLs`),
  - computes effective image inputs / find roots,
  - gates Run availability per command (`canRun`),
  - runs image commands through `PolymechImageEngine`,
  - runs `chat` by opening the Chat tab (`workspace.openPolymechWebChatTab()`).
- Toolbar chips and Run button:
  - `PolymechImageToolbarView` shows all commands including `Chat`.
- Inspector:
  - `PolymechImageInspectorView` includes command-specific forms and chat info section.
- Auto-switch inspector to Polymech tab when a command is armed.

## Main toolbar wiring (Polymech)

Primary files and responsibilities:

- [`CodeEdit/Features/Documents/Controllers/CodeEditWindowController+Toolbar.swift`](CodeEdit/CodeEdit/Features/Documents/Controllers/CodeEditWindowController+Toolbar.swift)
  - Adds the activity/toolbar host item and injects environment objects:
    - `WorkspaceDocument`
    - `EditorManager`
    - `PolymechImageJobCenter.shared`
    - `workspace.polymechWorkflow`
  - This is the main injection point that makes the Polymech toolbar controls stateful.
- [`CodeEdit/Features/ActivityViewer/ActivityViewer.swift`](CodeEdit/CodeEdit/Features/ActivityViewer/ActivityViewer.swift)
  - Hosts the title-bar style accessory area where Polymech controls are rendered.
- [`CodeEdit/Features/Polymech/PolymechImageToolbarView.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechImageToolbarView.swift)
  - Command chips (`Resize`, `Meta`, `Compress`, `Find`, `Transform`, `Chat`) and `Run`.
  - `Run` delegates to `PolymechWorkflowState.run(jobs:)`.
- [`CodeEdit/Features/Polymech/PolymechWorkflowState.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechWorkflowState.swift)
  - Core command state machine and command execution dispatcher.

Behavior summary:

- Command chip arms/disarms operation.
- Inspector is expected to edit JSON/options and Save.
- Run executes with latest saved settings.
- `Chat` Run path opens/focuses the chat tab (`openPolymechWebChatTab()`), while image commands go through `PolymechImageEngine`.

## Inspector wiring and extension points

Primary files:

- [`CodeEdit/Features/InspectorArea/Views/InspectorAreaView.swift`](CodeEdit/CodeEdit/Features/InspectorArea/Views/InspectorAreaView.swift)
  - Declares default tabs and appends extension-provided inspector tabs.
  - Auto-switches to `.polymech` tab when a Polymech command is armed.
- [`CodeEdit/Features/InspectorArea/Models/InspectorTab.swift`](CodeEdit/CodeEdit/Features/InspectorArea/Models/InspectorTab.swift)
  - Declares `InspectorTab.polymech` and maps it to `PolymechImageInspectorView()`.
  - Defines icon/title/body mapping for built-in + extension tabs.
- [`CodeEdit/Features/Polymech/PolymechImageInspectorView.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechImageInspectorView.swift)
  - Renders command-specific forms and Save/reset controls.
  - Contains the chat inspector info section for the `Chat` command.
- [`CodeEdit/Features/Polymech/PolymechSettingsFormViews.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechSettingsFormViews.swift)
  - Structured settings forms (Win32-aligned shape).
- [`CodeEdit/Features/Polymech/PolymechFormModels.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechFormModels.swift)
  - JSON <-> form model conversion.

Where to extend inspector next:

- Add/modify built-in tabs: edit `InspectorTab`.
- Add new Polymech command section UI: edit `PolymechImageInspectorView.commandForms`.
- Add external extension inspector tabs: use the existing `ExtensionManager` sidebar item path already consumed in `InspectorAreaView.updateTabs()`.

## View menu integration

- `ViewCommands` includes a `Chat` command:
  - opens the same chat tab action as workflow Run,
  - disabled if chat HTML is not currently discoverable.

## Menu extension points (including `View -> Chat`)

Current `Chat` menu insertion:

- [`CodeEdit/Features/WindowCommands/ViewCommands.swift`](CodeEdit/CodeEdit/Features/WindowCommands/ViewCommands.swift)
  - Added as a `Button("Chat")` inside `CommandGroup(after: .toolbar)`.
  - Calls `windowController?.workspace?.openPolymechWebChatTab()`.
  - Availability is gated by `canOpenPolymechWebChatTab()`.

Where to add more menu entries:

- Global command registration root:
  - [`CodeEdit/Features/WindowCommands/CodeEditCommands.swift`](CodeEdit/CodeEdit/Features/WindowCommands/CodeEditCommands.swift)
  - This is where menu command groups are aggregated (`MainCommands`, `FileCommands`, `ViewCommands`, etc.).
- View menu specific additions:
  - edit `ViewCommands`.
- Polymech-specific action routing:
  - prefer adding handlers in `WorkspaceDocument`/`PolymechWorkflowState`, then call from menu buttons.

Notes for future menu changes:

- Keep menu actions thin (UI trigger only).
- Put behavior in workflow/document methods so toolbar and menu can share the same code path.
- If a new menu action should only appear when workflow context is valid, mirror the same `disabled(...)` predicates used in toolbar Run gating.

## Settings story (CodeEdit + shared provider config)

Goal:

- Give users one CodeEdit settings surface for LLM providers that is compatible with existing Win32/CLI settings usage.
- Separate defaults by intent:
  - image/vision defaults for `meta` + `transform`,
  - text/chat defaults for Agent Chat,
  - while allowing command-level override later.

### Chapter 1 implemented (UI + import/export + page curation)

Primary files:

- [`CodeEdit/Features/Settings/Pages/AIProvidersSettings/AIProvidersSettingsView.swift`](CodeEdit/CodeEdit/Features/Settings/Pages/AIProvidersSettings/AIProvidersSettingsView.swift)
  - New **AI Providers** page.
  - Sections:
    - Storage path (reload/save),
    - defaults (image + text),
    - image providers list (name, api key, base url, model),
    - text providers list (name, api key, base url, model),
    - import/export controls.
- [`CodeEdit/Features/Settings/SettingsView.swift`](CodeEdit/CodeEdit/Features/Settings/SettingsView.swift)
  - Adds sidebar page entry for **AI Providers**.
  - Routes detail panel to `AIProvidersSettingsView`.
  - Hides CodeEdit pages for now:
    - `Language Servers`,
    - `Source Control`,
    - `Text Editing`,
    - `Accounts` (explicitly deferred for later return).
- [`CodeEdit/Features/Settings/Models/SettingsPage.swift`](CodeEdit/CodeEdit/Features/Settings/Models/SettingsPage.swift)
  - Adds `SettingsPage.Name.aiProviders`.
- [`CodeEdit/Features/Settings/Models/SettingsData.swift`](CodeEdit/CodeEdit/Features/Settings/Models/SettingsData.swift)
  - Adds search-key mapping for `aiProviders`.

### JSON contract and compatibility

This first chapter writes plain JSON on macOS to a path resolved by:

- the pm-image profile directory (`~/Library/Application Support/PolyMech/pm-image/settings.json` by default),
- or the process profile root when launched with `--config-dir`.

Keys written for compatibility with Win32/CLI conventions:

- `providers` (map: provider -> `{api_key, base_url, default_model}`),
- `active_provider` (default image provider),
- `chat` (active text provider projected into Win32-like chat shape).

Additional Polymech macOS metadata currently used:

- `polymech_text_providers`,
- `polymech_active_text_provider`.

Import/export currently operates on JSON files (object root), intended for sharing provider bundles between app instances/profiles.

### Scope boundaries (important)

- This chapter is **settings UI only**.
- Command execution wiring to consume these defaults in all paths is a separate chapter.
- Win32 encryption/DPAPI settings-store behavior is not replicated in this Swift-only page yet.

### Chapter 2 implemented (provider-default integration)

Primary files:

- [`CodeEdit/Features/Polymech/PolymechProviderSettingsStore.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechProviderSettingsStore.swift)
  - Central reader for provider defaults used by runtime and forms.
  - Resolves settings from the pm-image profile root (`--config-dir` for process-scoped overrides,
    otherwise `~/Library/Application Support/PolyMech/pm-image/settings.json`).
  - Exposes:
    - image providers / active image provider / provider-by-name,
    - text providers / active text provider.
- [`CodeEdit/Features/Polymech/PolymechImageEngine.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechImageEngine.swift)
  - `meta` and `transform` now merge image-provider defaults before bridge call:
    - fill `provider` from active image provider if missing,
    - fill `model` from provider default if empty,
    - fill `base_url` from provider default if empty,
    - fill `api_key` from provider row if missing
      (still preserving the existing global `providerApiKey` fallback merge logic).
- [`CodeEdit/Features/Polymech/PolymechSettingsFormViews.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechSettingsFormViews.swift)
  - Meta/Transform forms no longer hardcode `google`.
  - Provider pickers are sourced from configured image providers.
  - Model presets are provider-aware while still keeping manual model text editing.
- [`CodeEdit/Features/Polymech/PolymechWebChatView.swift`](CodeEdit/CodeEdit/Features/Polymech/PolymechWebChatView.swift)
  - Chat panel now surfaces active text-provider default in a system line.
  - Current placeholder `send` error text includes the active text provider context.

Behavior in practice:

- `meta` / `transform` command runs now honor the defaults set in **AI Providers** without removing per-command override capability.
- Chat panel reflects text-provider default selection, even before native turn execution is wired.

Remaining for full parity:

- Route chat `send/stop/settings` to a native host bridge that executes `media::llm::agent::run_turn`.
- Resolve final fallback order exactly against Win32/CLI behavior once chat bridge is connected.

## Chat WebView host in macOS

Implemented in `CodeEdit/Features/Polymech/PolymechWebChatView.swift`:

- Resolves `chat.html` from:
  - bundled app resource `Contents/Resources/PolymechChat/chat.html`,
  - or workspace-relative fallback:
    - `apps/chat/dist/chat.html`,
    - `packages/media/cpp/apps/chat/dist/chat.html`,
    - with parent directory walk-up.
- Uses `WKWebView` for chat pages.
- Injects a `window.chrome.webview` compatibility polyfill at document start, so existing `apps/chat/src/main.js` bridge code can run unchanged.
- Handles web->host messages (`ready`, `settings`, `send`, etc.).
- Pushes host context into web UI via `window.pmChat.setStatus({ selection, folder })`.
- Current `send` path returns a clear placeholder error in transcript:
  - native LLM turn execution (`media::llm::agent::run_turn`) is not wired yet on macOS.

## Editor routing to WebView

- `EditorAreaFileView` renders `PolymechWebChatContainerView` for chat HTML URLs (`/apps/chat/...*.html` or bundled `PolymechChat/chat.html`), instead of plain text/code view.

## Chat bundle copied into app

**CMake** (`osx/CodeEdit/CMakeLists.txt`, `pm-codeedit` target) copies `packages/media/cpp/dist/chat.html` (from `npm run build:chat-next:embed`) into `dist-osx/PixelWiz.app/Contents/Resources/PolymechChat/chat.html` after `xcodebuild`.

**Xcode (Run / Archive):** the PixelWiz target includes a **Run Script** phase *Copy PolymechChat (dist/chat.html)* that copies the same file from `${SRCROOT}/../../dist/chat.html` into the built app’s `Resources/PolymechChat/`. Without that step (or without opening a workspace that contains `packages/media/cpp/dist/chat.html`), `Bundle.main.url(forResource:…, subdirectory: PolymechChat)` is nil and **View → Chat** stays disabled and the inspector chat area cannot load.

If the script prints a warning, run `npm run build:chat-next:embed` from `packages/media/cpp` so `dist/chat.html` exists, then rebuild PixelWiz.

## apps/chat portability note

`apps/chat/src/main.js` `rewriteLocalPath()` now handles POSIX absolute paths:

- if path starts with `/`, it rewrites to `file://<path>`,
- in addition to existing Windows drive-path handling.

This is needed for macOS file paths in chat markdown image rendering.

## macOS VMware / dev VM tuning (optional)

Use on **throwaway guests** when UI feels heavy (Spotlight, Time Machine, background updates). Run in **Terminal on the VM** (not over SSH without a TTY if you expect an interactive `sudo` password).

**Apply (one password prompt per line if `NOPASSWD` is not set for that binary):**

```bash
sudo mdutil -a -i off
sudo mdutil -a -E
sudo tmutil disable
sudo defaults write /Library/Preferences/com.apple.SoftwareUpdate AutomaticCheckEnabled -bool false
```

Omit `sudo mdutil -a -E` if you do not want to erase/rebuild Spotlight indexes (can take a while).

**`NOPASSWD` in `visudo`:** allow **`/usr/sbin/mdutil`**, **`/usr/bin/tmutil`**, **`/usr/bin/defaults`** (and your username)—not `sudo sh -c '...'`, because `sudo` then runs `/bin/sh`, which is usually not whitelisted.

**Verify:**

```bash
mdutil -s /
tmutil status
defaults read /Library/Preferences/com.apple.SoftwareUpdate AutomaticCheckEnabled
```

**`visudo` save:** nano — `Ctrl+O`, Enter, `Ctrl+X`. vim — `Esc`, `:wq`, Enter.

## Build / run

From `packages/media/cpp/osx`:

- `npm run configure:codeedit`
- `npm run build:codeedit`

Or direct target from repo root:

- `cmake --preset release-macos -G Ninja -DPM_BUILD_CODEEDIT=ON`
- `cmake --build --preset release-macos --target pm-codeedit`

Result app:

- `dist-osx/CodeEdit.app`
