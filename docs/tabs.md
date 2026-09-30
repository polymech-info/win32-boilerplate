# FileViewer Tabs Todo

Goal: keep the centre `CFileViewer` as the default single-view surface, but let it convert into file tabs in the same place when the user pins a preview or opens another tab. Explorer selection updates the last open / active unpinned tab. When that tab is pinned, the next Explorer-driven preview opens/updates a new unpinned tab. Pinning that tab repeats the cycle.

## Terms

- Pinned tab: stable viewer instance. Explorer selection no longer replaces its content.
- Live tab: the one unpinned viewer tab that tracks Explorer selection changes.
- Explicit open: user action that creates/selects a tab intentionally, for example "open in new tab" or pinning current preview.
- Same place: keep the current centre preview area; do not move the feature to a side dock.
- Collapsed mode: normal current behavior, one centre `CFileViewer`, no visible tab strip.
- Tabbed mode: centre view is wrapped by a tab host once two or more tabs exist.
- Tabbed toggle: top menu `View -> Tabbed` command that explicitly converts the centre view into tabbed mode or collapses it back when safe.

## Architecture Todos

- [ ] Add a Win32++ centre tab host for `CFileViewer` instances.
  - Candidate: a `CTab`/tab-control backed host that owns multiple `CFileViewer` pages and lives in the exact current centre-view slot.
  - The host should be a normal `SetView(...)` client replacement, not a dock panel, unless Win32++ layout constraints force a different adapter.
  - Avoid moving the centre preview into the existing dock tree unless that becomes an explicit product decision.

- [ ] Preserve collapsed single-view behavior by default.
  - Main/viewer workbenches still feel like today with one centre view.
  - First preview does not show a tab strip.
  - Pinning the first preview converts the centre view into tab-capable state.
  - `View -> Tabbed` also converts the centre view into tabbed mode explicitly.
  - Toggling `View -> Tabbed` off collapses back only when there is one unpinned tab, or after asking/deciding what to do with pinned extra tabs.
  - Closing tabs back down to one unpinned tab collapses back to the plain centre view.

- [ ] Keep a single live unpinned tab.
  - Explorer selection routes to the last open / active unpinned tab.
  - If no live tab exists, create one.
  - If the current live tab is pinned, create a fresh live tab for the next Explorer selection.
  - If the active tab is unpinned, keep updating that tab until it is pinned.

- [ ] Add tab metadata.
  - File path / title.
  - Pinned flag.
  - Closeable flag if the live tab needs special treatment.
  - Dirty or generated state later if needed.
  - Preview source, if useful for debugging.

- [ ] Keep existing viewer routing intact.
  - Preserve `CPreviewCoordinator::Request(...)` policy gating.
  - Replace direct `m_fileViewer` targeting with an accessor like `ActivePreviewViewer()` or `LivePreviewViewerFor(source)`.
  - Do not duplicate viewer loading logic outside `CFileViewer::OpenFile(...)`.

- [ ] Define source behavior.
  - Explorer selection: update live unpinned tab.
  - CLI/startup `--src`: open live tab and select source in FileTree.
  - Queue row click: probably update live tab unless user explicitly opens in a new tab.
  - Chat/generated file click: likely explicit tab behavior, but decide before implementation.
  - Recent file: likely explicit tab behavior.

## UI Todos

- [ ] Add tab strip actions.
  - Pin/unpin tab, rendered next to close on each tab.
  - Close tab.
  - Select tab.
  - Optional: close others / close unpinned.

- [ ] Add `View -> Tabbed` toggle.
  - Add or confirm `Resource.rc` menu entry under `View`.
  - Add a command id and checked-state update.
  - Toggle on: convert the current centre `CFileViewer` into the first tab in the same place.
  - Toggle off: restore/collapse to the plain centre view when safe.
  - Reflect tabbed mode state in the menu checkmark.

- [ ] Decide when tabs are visible.
  - Hide tabs in collapsed mode.
  - Show tabs once there are two tabs, or once the only tab is pinned and a new live tab is expected.
  - Show tabs immediately when `View -> Tabbed` is toggled on, even before a second file exists.
  - Collapse back to the plain centre view after closing other tabs, when only one unpinned live tab remains.

- [ ] Add tab titles.
  - Use filename.
  - Show pin state with the per-tab pin toggle.
  - Consider tooltip with full path.

- [ ] Define pin button behavior.
  - Pinning a tab prevents Explorer selection from replacing that tab's content.
  - If the active tab is pinned, the next Explorer selection creates/selects a fresh live unpinned tab.
  - Unpinning a tab can make it the live Explorer-tracking tab if it is active.
  - Ensure the pin hit target does not conflict with close or select.

- [ ] Preserve current viewer chrome.
  - Image toolbar.
  - WebView2 viewer-next panels.
  - File info text.
  - Theme refresh.
  - Fullscreen behavior.

## Implementation Todos

- [ ] Phase 1: introduce `ViewerManager` without tabs.
  - Wrap/manage the existing `CMainFrame::m_fileViewer`.
  - Keep the current centre view as-is.
  - Expose `ViewerManager::ActiveView()` returning the existing viewer.
  - Move centre preview routing to `ViewerManager::ActiveView()` first.
  - No new tab UI in this phase.
  - Success condition: opening/updating through `ViewerManager.ActiveView()` behaves exactly like direct `m_fileViewer` today.

- [ ] Introduce a centre viewer tab model.
  - `ViewerTab { id, path, pinned, unique_ptr<CFileViewer> }` or equivalent Win32++-safe ownership.
  - Track active tab id and live tab id.

- [ ] Introduce a centre viewer host class.
  - Own Win32++ tab strip + active `CFileViewer` child only in tabbed mode.
  - Support adopting the existing centre `m_fileViewer` into the first tab when converting to tabbed mode.
  - Support returning to plain `m_fileViewer` when collapsing back to one unpinned tab.
  - Handle resize/layout.
  - Expose `ActiveViewer()` and `LiveViewerForExplorer()`.
  - Save/restore enough host state to survive layout persistence.

- [ ] Wire `View -> Tabbed` command.
  - Add command id in `Resource.h`.
  - Add menu item in `Resource.rc` under `View`.
  - Handle command in `CMainFrame::OnCommand`.
  - Add checked-state refresh alongside other view menu checks.
  - Delegate implementation to `ViewerManager`/centre host instead of embedding tab policy in the menu handler.

- [ ] Update workbench attachment.
  - Main/viewer workbenches should initially keep `SetView(m_fileViewer)` or an equivalent collapsed host.
  - Switch to a tab host only when tabbed mode is needed.
  - Ensure switching between collapsed and tabbed uses the same centre client area and does not disturb left/right/bottom dock panels.
  - Chat workbench keeps chat centre; optional dock viewer can stay separate for now.

- [ ] Update main frame call sites.
  - Route preview coordinator calls to the selected tab viewer.
  - Replace direct `m_fileViewer.LoadPicture/OpenFile/Clear*` calls where they affect centre preview.
  - Keep `m_pDockViewerPanel->GetFileViewer()` behavior separate.

- [ ] Update `PreviewCoordinator`.
  - Keep policy and status logic.
  - Consider storing tab id or target viewer identity in preview state for debugging.

- [ ] Update startup and FileTree selection behavior.
  - `--ui-preset=main|viewer --src ...` should create/update the live tab and select the file in FileTree.
  - Preserve current `SelectPathInFileTree(...)` flow.

- [ ] Wire WebView2 bus for all centre viewer instances.
  - Current code wires `m_fileViewer`.
  - New tab viewers need `SetBusManager(&m_webViews)` when created.

- [ ] Handle cleanup safely.
  - Closing a tab must tear down RAW threads, WebView2 child panels, folder mappings, and image resources through normal `CFileViewer` destruction.
  - Avoid destroying a viewer while async preview work is still targeting it.

## Persistence Todos

- [ ] Decide persistence scope.
  - Session-only first is safest.
  - Persist pinned tabs later if needed.
  - The `View -> Tabbed` toggle state itself should save/restore with the UI layout once implemented.

- [ ] Persist centre-tab layout state.
  - Whether centre view is currently tabbed.
  - Active tab id/path.
  - Pinned tab paths and order.
  - Live tab path only if we decide the explicit `View -> Tabbed` workspace should restore exactly.
  - Avoid restoring missing files as broken tabs unless there is a clear placeholder UI.

- [ ] If path tabs are persisted, store only pinned tabs by default.
  - Paths.
  - Active pinned tab.
  - Do not persist live unpinned Explorer-tracking tab by default.

- [ ] Integrate with existing save/load layout.
  - Save tabbed-centre state near the existing Win32++ dock layout/settings payload.
  - Restore after the centre view and workbench are created, before startup `--src` preview routing if possible.
  - Define precedence when startup `--src` is present: likely restore pinned tabs, then route startup file to the live tab.

## Risk Todos

- [ ] Audit all direct `m_fileViewer` usages.
- [ ] Audit all `PreviewPathW()` consumers.
- [ ] Verify image navigation keys target the active tab.
- [ ] Verify theme refresh updates tab strip and all viewer instances.
- [ ] Verify fullscreen behavior with multiple viewer instances.
- [ ] Verify viewer workbench startup latch still protects startup preview.
- [ ] Verify chat workbench does not accidentally steal centre preview routing.
- [ ] Verify Win32++ `SetView` replacement does not break dock restore/re-dock behavior.
- [ ] Verify `View -> Tabbed` checked state survives save/load layout.
- [ ] Verify per-tab pin/close buttons work with dark theme and high DPI.
- [ ] Verify Explorer selection creates a new live tab after pinning the active tab.

## Suggested First Slice

- [ ] Create `ViewerManager`.
- [ ] Store a pointer/reference to the existing `m_fileViewer`.
- [ ] Add `ActiveView()` and route a small set of centre-preview call sites through it.
- [ ] Expand routing until all centre-preview updates use `ViewerManager.ActiveView()`.
- [ ] Verify behavior is unchanged.
- [ ] After this refactor, design/implement tab state on top of `ViewerManager`.
