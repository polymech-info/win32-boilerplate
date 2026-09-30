#ifndef PM_UI_RESOURCE_H
#define PM_UI_RESOURCE_H

#include "default_resource.h"
#include "RibbonUI.h"

// Keep resource-script feature gates aligned with the C++ defaults in `constants.hpp`.
#ifndef FEATURE_REGISTER_EXPLORER
#define FEATURE_REGISTER_EXPLORER 1
#endif

// Menu fallback IDs (when ribbon is unavailable)
#define IDM_RESIZE              200
#define IDM_CLEAR_QUEUE         201
#define IDM_ADD_FILES           202
#define IDM_ADD_FOLDER          203
#define IDM_SETTINGS            204
#define IDM_EXIT                205
#define IDM_ABOUT               206
// Classic File menu — recent lists (max 15 each; populated in WM_INITMENUPOPUP).
// Placeholders required by rc.exe (empty submenus are rejected); stripped on first INIT.
#define IDM_RECENT_MENU_DUMMY_FILE    4598
#define IDM_RECENT_MENU_DUMMY_FOLDER  4599
#define IDM_RECENT_FILE_FIRST   4600
#define IDM_RECENT_FILE_LAST    4614
#define IDM_RECENT_FOLDER_FIRST 4615
#define IDM_RECENT_FOLDER_LAST  4629
// Classic menu: Pixlwiz (see Resource.rc / Resource_ui_i18n.rc)
#define IDM_PIXLWIZ_LOGIN  4630
#define IDM_PIXLWIZ_SHARE  4631
#define IDM_PIXLWIZ_LOGOUT 4632
/// File menu — save cropped centre preview (`CFileViewer`, `Mainfrm.cpp`).
#define IDM_FILE_SAVE        4633
#define IDM_FILE_SAVE_AS     4634
/// File menu — layout save/load (exports/imports workbench.window + win32_dock subtree).
#define IDM_FILE_SAVE_LAYOUT 4635
#define IDM_FILE_LOAD_LAYOUT 4636

// Settings controls (for settings dock)
#define IDC_COMBO_PRESET_OUT    600
#define IDC_EDIT_OUT_DIR        601
#define IDC_BTN_BROWSE_OUT      602
#define IDC_EDIT_MAX_W          603
#define IDC_EDIT_MAX_H          604
#define IDC_COMBO_FIT           605
#define IDC_EDIT_QUALITY        606   // kept for WriteOptions compat
#define IDC_CHK_ENLARGE         607
#define IDC_CHK_AUTOROT         608
#define IDC_CHK_STRIP           609
// Extended settings controls
#define IDC_COMBO_RES_PRESET    680   // resolution preset combo
#define IDC_COMBO_RATIO         681   // aspect ratio combo
#define IDC_SLIDER_QUALITY      682   // trackbar (1–100)
#define IDC_LBL_QUALITY         683   // live "85" label beside slider
#define IDC_COMBO_KERNEL        684   // resampling kernel combo
#define IDC_COMBO_FORMAT        685   // output format combo (auto / jpg / png / webp / tiff / avif)

// AI Transform dialog controls
#define IDD_TRANSFORM_PROMPT    700
#define IDC_EDIT_PROMPT         701
#define IDC_STATIC_PROMPT_LABEL 702

// Pixlwiz Share — post title / description / success (see PixlwizSharePostDlg.cpp)
#define IDD_PIXLWIZ_SHARE_POST        730
#define IDC_PIXLWIZ_SHARE_TITLE       731
#define IDC_PIXLWIZ_SHARE_DESC        732
#define IDC_PIXLWIZ_SHARE_PRIVATE     733 ///< BS_AUTOCHECKBOX — only you (`private`).
#define IDC_PIXLWIZ_SHARE_IN_FEEDS    734 ///< BS_AUTOCHECKBOX — feeds/home when not private (`public` vs `listed`).
#define IDD_PIXLWIZ_SHARE_SUCCESS     736
#define IDC_PIXLWIZ_SUCCESS_STATUS    737 ///< Static: "Pictures attached: N" (set in code).
#define IDC_PIXLWIZ_SUCCESS_OPEN      738 ///< "Open in browser" — ShellExecute URL.

// AI Transform settings controls
#define IDC_COMBO_AI_MODEL      710
#define IDC_COMBO_AI_ASPECT     711
#define IDC_COMBO_AI_SIZE       712

// Provider Settings dialog (IDC_PROVDLG_* defined in ProviderDlg.cpp)
// Buttons inside the Settings panel (Transform mode) — forwarded to the
// frame via SendMessage(GA_ROOT, WM_COMMAND, ...). No longer on the ribbon.
#define IDC_CMD_PROVIDER_KEYS   346
#define IDC_CMD_PRESETS         345
// Resize/Run lived on the AI ribbon tab, now also sourced from this header.
#define IDC_CMD_RESIZE          300
#define IDC_CMD_RUN             312

// Compress button (Home tab, Actions group)
#define IDC_CMD_COMPRESS        313

// Meta button (Home tab, Actions group) — generate description / JSON / EXIF via LLM
#define IDC_CMD_META            314

// Transform button (Home tab, Actions group) — switch to AI Transform mode
#define IDC_CMD_TRANSFORM       315

// Find button (Home tab, Actions group) — switch to Find mode
#define IDC_CMD_FIND            316

// Compress settings panel controls (620-639)
#define IDC_CMP_DEST            620   // output destination combo
#define IDC_CMP_DIR             621   // custom folder edit
#define IDC_CMP_BROWSE          622   // browse button
#define IDC_CMP_FORMAT          623   // compressor combo (PNG / MozJPEG)
// ── PNG section ─────────────────────────────────────────
#define IDC_CMP_LEVEL_SLIDER    624   // DEFLATE level trackbar
#define IDC_CMP_LEVEL_LBL       625   // live level label "9"
#define IDC_CMP_QUANTIZE        626   // [x] palette quantise
#define IDC_CMP_COLORS          627   // palette colours combo
#define IDC_CMP_QUAL_SLIDER     628   // quantise quality trackbar
#define IDC_CMP_QUAL_LBL        629   // live quality label
#define IDC_CMP_ZOPFLI          630   // [x] zopfli
// ── MozJPEG section ─────────────────────────────────────
#define IDC_CMP_JPEG_SLIDER     631   // JPEG quality trackbar
#define IDC_CMP_JPEG_LBL        632   // live JPEG quality label
#define IDC_CMP_PROGRESSIVE     633   // [x] progressive JPEG
#define IDC_CMP_TRELLIS         634   // [x] trellis quantisation
// ── Common ───────────────────────────────────────────────
#define IDC_CMP_STRIP           635   // [x] strip metadata

// Compress worker messages (progress re-uses UWM_QUEUE_PROGRESS with states 4/5/6)
#define UWM_COMPRESS_DONE       (WM_USER + 112)

// Meta panel controls (660-679)
#define IDC_META_OUT_DIR        660
#define IDC_META_BROWSE         661
#define IDC_META_OUT_MD         662   // [x] Generate <stem>.md
#define IDC_META_OUT_JSON       663   // [x] Generate <stem>.json
#define IDC_META_UPDATE_EXIF    664   // [x] Update EXIF ImageDescription
#define IDC_META_RESIZE_FIRST   665   // [x] Resize first (in memory) before sending
#define IDC_META_RESIZE_W       666   // resize width combo / edit
#define IDC_META_PROVIDER       667   // provider combo (google)
#define IDC_META_MODEL          668   // model combo
#define IDC_META_PROMPT         669   // multi-line prompt edit
#define IDC_META_PRESET         670   // prompt preset combo
#define IDC_META_COLLECTION     671   // replicate collection combo
#define IDC_META_REFRESH        672   // replicate model refresh
#define IDC_TF_PROVIDER         673   // transform provider combo
#define IDC_FIND_PROVIDER       674   // find/meta provider combo
#define IDC_TF_COLLECTION       675   // transform replicate collection combo
#define IDC_TF_REFRESH          676   // transform replicate refresh
#define IDC_FIND_COLLECTION     677   // find replicate collection combo
#define IDC_FIND_REFRESH        678   // find replicate refresh

// Transform — reference list + optional pre-resize (715-720)
#define IDC_TF_REF_LIST         715   // listbox of selected reference images
#define IDC_TF_REF_ADD          716   // [+ Add reference…] button
#define IDC_TF_REF_CLEAR        717   // [Clear] button
#define IDC_TF_RESIZE_FIRST     718   // [x] pre-resize in memory (like Meta)
#define IDC_TF_RESIZE_W         719   // long-edge width combo
#define IDC_TF_RESIZE_RAW       720   // [x] only camera RAW/HEIC get explicit pre-resize

// Find settings panel controls (740-769)
#define IDC_FIND_PROMPT         740   // multi-line edit (search query)
#define IDC_FIND_LLM            741   // [x] semantic LLM mode
#define IDC_FIND_BYPASS_CACHE   742   // [x] re-generate even when sidecar exists
#define IDC_FIND_NO_GENERATE    743   // [x] only consult existing cache
#define IDC_FIND_MATCH_FOLDERS  744   // [x] name mode: match parent folder names
#define IDC_FIND_RECURSIVE      745   // [x] recurse into subfolders
#define IDC_FIND_USE_MD         746   // [x] read sidecar .md
#define IDC_FIND_USE_JSON       747   // [x] read sidecar .json
#define IDC_FIND_USE_EXIF       748   // [x] read libvips EXIF
#define IDC_FIND_MAX            749   // max results edit
#define IDC_FIND_MODEL          750   // model combo (LLM mode)
#define IDC_FIND_RESIZE_W       751   // resize-width combo (LLM generation)
#define IDC_FIND_RESIZE_FIRST   762   // [x] pre-resize in memory (LLM, like Meta)
// Reference image picker (LLM mode — sent as multimodal parts in each judge call)
#define IDC_FIND_REF_LIST       752   // listbox of selected reference image paths
#define IDC_FIND_REF_ADD        753   // [+ Add reference…] button
#define IDC_FIND_REF_CLEAR      754   // [Clear] button

// Meta worker messages — uses UWM_QUEUE_PROGRESS states 7/8/9
#define UWM_META_DONE           (WM_USER + 114)

// View tab — panel visibility toggles
#define IDC_CMD_VIEW_QUEUE      350
#define IDC_CMD_VIEW_LOG        351
#define IDC_CMD_VIEW_SETTINGS   352
// 353 was IDC_CMD_VIEW_FILEINFO — retired (metadata is in the central preview).
// 354 / 367 were optional second-preview / gen strip commands — removed.
#define IDC_CMD_RESET_LAYOUT    355
#define IDC_CMD_DEBUG_STATE     356
#define IDC_CMD_SAVE_AS         357
#define IDC_CMD_VIEW_NODES      360
#define IDC_CMD_VIEW_FILETREE   361
#define IDC_CMD_VIEW_FINDRESULTS 362
#define IDC_CMD_VIEW_TABBED     369

// App Settings (theme + font) — global modal launched from the View tab.
#define IDC_CMD_APP_SETTINGS    370
// File application menu: "Options…" (same handler as App Settings).
#define IDC_CMD_FILE_OPTIONS    371
// File application menu: web settings popup.
#define IDC_CMD_FILE_SETTINGS   372
/// Dock panel chrome (e.g. Log clear) — outside ribbon 300–430 dispatch range.
#define IDC_LOG_PANEL_CLEAR     450
#define IDC_LOG_PANEL_COPY      451
/// Log dock filter row (`CLogContentHost`) — CBS_DROPDOWNLIST combos.
#define IDC_LOG_FILTER_LEVEL    452
#define IDC_LOG_FILTER_SOURCE   453
/// Log dock detail pane — read-only multiline EDIT below the list.
#define IDC_LOG_DETAIL_EDIT     454

// User messages
#define UWM_QUEUE_PROGRESS      (WM_USER + 100)
#define UWM_QUEUE_DONE          (WM_USER + 101)
#define UWM_IMAGELOADED         (WM_USER + 102)
#define UWM_QUEUE_ITEM_CLICKED  (WM_USER + 103)
#define UWM_TRANSFORM_PROGRESS  (WM_USER + 104)
#define UWM_TRANSFORM_DONE      (WM_USER + 105)
#define UWM_LOG_MESSAGE         (WM_USER + 106)
#define UWM_GENERATED_FILE      (WM_USER + 107)
// Posted by CExplorerBrowserView when the shell selection changes.
// wparam = new std::vector<std::wstring> — freed by CMainFrame::OnExplorerSelection.
// lparam = non-zero if VK_CONTROL was down when the selection was captured (Ctrl+additive pick).
#define UWM_EXPLORER_SELECTION  (WM_USER + 108)
// Posted by the RAW quality-decode background thread when it finishes.
// wparam = (std::vector<unsigned char>*) — heap-allocated JPEG bytes
//          produced by vips_jpegsave_buffer; ALWAYS deleted by the handler
//          (whether the generation matches or not).
// lparam = (int) generation counter — stale results are discarded.
//
// Why bytes, not Gdiplus::Image*: the worker thread used to call
// Gdiplus::Image::FromStream itself and was *detached* on navigation /
// destruction. That raced with Gdiplus::GdiplusShutdown in
// CFileViewer::~CFileViewer and could crash mid-decode. The handler
// now does the GDI+ work on the UI thread, so workers never touch GDI+
// and can be safely detached on rapid navigation.
#define UWM_RAW_DECODED         (WM_USER + 109)
// Posted when Stage 1 (fast vips_thumbnail) finishes for a RAW file.
// Same wparam contract as UWM_RAW_DECODED — heap std::vector<unsigned char>*
// of JPEG bytes (or nullptr on decode failure), always deleted by the
// handler. lparam = fast-preview generation counter.
#define UWM_RAW_PREVIEW_READY   (WM_USER + 111)
// Posted by the preview panel's "Full" overlay button and by F11 / ALT+F.
// Handled by CMainFrame::WndProc → ToggleFullscreen().
#define UWM_TOGGLE_FULLSCREEN   (WM_USER + 110)
// Delete key on the file queue — handled by CMainFrame (blocked while processing).
#define UWM_QUEUE_DELETE_SELECTION (WM_USER + 113)

// ── Find panel messages ──────────────────────────────────────────────────────
// Posted by the Find worker (or web chat `image_find`) when results are ready.
//   wparam = (std::vector<FindProgressRow>*) — owned, freed by frame handler.
//   lparam = 0 = append to list (Find command cleared the panel at start);
//            non-0 = ClearAll + replace (e.g. chat find).
#define UWM_FIND_PROGRESS          (WM_USER + 115)
// Posted when the worker finishes. wparam = (bool)success, lparam = (int)count.
#define UWM_FIND_DONE              (WM_USER + 116)
// Posted by the Find panel when the user clicks a row. wparam = item index.
#define UWM_FIND_ITEM_CLICKED      (WM_USER + 117)
// Posted by the Find panel context menu — frame deletes selected rows.
#define UWM_FIND_DELETE_SELECTION  (WM_USER + 118)
// Posted by the Find panel context menu — frame navigates the Explorer dock
// to the parent folder of wparam = (std::wstring*) — owned, frame deletes.
#define UWM_FIND_REVEAL_IN_EXPLORER (WM_USER + 119)

// Duplicates results: wparam = (DuplicatesUiResult*) on DONE (frame deletes).
#define UWM_DUPLICATES_DONE        (WM_USER + 133)
#define UWM_DUPLICATES_ITEM_CLICKED (WM_USER + 134)
#define UWM_DUPLICATES_DELETE_SELECTION (WM_USER + 135)
#define UWM_DUPLICATES_SAVE_SESSION  (WM_USER + 136)
#define UWM_DUPLICATES_OPEN_SESSION  (WM_USER + 137)
// wparam = listview row index; lparam = heap std::wstring* (frame deletes).
// Workers pass the row; no broadcast / heuristics in the frame.
#define UWM_QUEUE_OP_STATUS          (WM_USER + 138)

// ── Chat dock (in-app LLM agent over our path-mode tools) ───────────────────
// Ribbon Home tab: cmdChat opens / focuses the Chat dock.
// Ribbon View tab: cmdViewChat toggles its visibility.
#define IDC_CMD_CHAT             317
#define IDC_CMD_VIEW_CHAT        363
// View tab — dockable secondary file preview panel (FileViewerPanel.h).
#define IDC_CMD_VIEW_VIEWER_PANEL 368
// FEATURE_BROWSER: general-purpose WebView2 popup (View → Web Browser).
#define IDC_CMD_VIEW_BROWSER          375
// FEATURE_BROWSER: transparent viewer popup (View → Viewer Browser).
#define IDC_CMD_VIEW_VIEWER_BROWSER   376
// Centre viewer tabs: pin/unpin the active preview tab.
#define IDC_CMD_VIEW_PIN_TAB          380
// FEATURE_CONSOLE: dockable WebView2 terminal / console panel (View → Console).
#define IDC_CMD_VIEW_CONSOLE          377
// FEATURE_HOME_PAGE: centre FileViewer WebView2 home app (View → Home).
#define IDC_CMD_VIEW_HOME             378
// Own ribbon: global Light/Dark override toggle (refreshes native + web hosts).
#define IDC_CMD_TOGGLE_THEME          379

// Duplicates settings (800-819) — CSettingsView MODE_DUPLICATES
#define IDC_DUP_MODE         800
#define IDC_DUP_MIN_GROUP    801
#define IDC_DUP_RECURSIVE    802
#define IDC_DUP_MAX_HAM      803
#define IDC_DUP_FP_SAME_SIZE 804
#define IDC_DUP_USE_MD       805
#define IDC_DUP_USE_JSON     806
#define IDC_DUP_USE_EXIF     807
#define IDC_DUP_META_PROMPT  808
#define IDC_DUP_META_LLM     809
#define IDC_DUP_MIN_SIM      810
#define IDC_DUP_LLM_SOURCE   811
#define IDC_DUP_LLM_INFO     812
#define IDC_DUP_IMPLICIT_META 813

// Chat panel controls (770-789).
#define IDC_CHAT_TRANSCRIPT      770
#define IDC_CHAT_INPUT           771
#define IDC_CHAT_SEND            772
#define IDC_CHAT_STOP            773
#define IDC_CHAT_STATUS_LBL      774
#define IDC_CHAT_SETTINGS        775
#define IDC_CHAT_CLEAR           776

// Chat worker -> UI-thread messages (legacy; retained for potential future use).
#define UWM_CHAT_APPEND_TEXT     (WM_USER + 130)   // wparam = std::wstring*
#define UWM_CHAT_TURN_DONE       (WM_USER + 131)   // wparam = bool ok (1/0), lparam = std::wstring* error

// CChatWebView -> CMainFrame (synchronous SendMessage). Asks the frame to push
// the current Explorer selection + folder into the chat view via SetContext,
// so the next Send sees fresh context. No payload.
#define UWM_CHAT_REQUEST_CONTEXT (WM_USER + 132)

// ── App commands (`pm-image app <verb>`) ────────────────────────────────────
// Posted by the WM_COPYDATA bridge in src/win/ui_singleton.cpp (dwData == 2)
// after an external `pm-image app <verb>` invocation forwards a command name
// to the running UI. wparam = std::string* (heap, freed by the handler in
// CMainFrame::OnAppCommand).
//
// The same dispatch path is also used by in-app keyboard shortcuts (ALT+P
// for `takescreenshot`). See src/win/app_commands.{hpp,cpp} for the verb
// catalog and CMainFrame::RunAppCommand for the per-verb implementations.
#define UWM_APP_COMMAND          (WM_USER + 150)

// Own-ribbon (FEATURE_USE_OWN_RIBBON): CTab selection → frame. wparam = tab index (0=Home, 1=View).
#define UWM_OWN_RIBBON_TAB       (WM_USER + 165)

// ── Release preview file locks (Shell delete or in-place batch I/O) ─────────
// SENT (synchronously) with wparam = const std::vector<std::wstring>* (caller-
// owned, stack lifetime; no delete on receive). lparam: 0 = I/O only (e.g.
// resize worker); non-zero = delete-to-recycle (also prune m_explorerSelectionPaths).
// Also used from batch workers before in-place read/write so libvips does not
// race a memory-mapped preview.
//
// CMainFrame::OnReleasePreviewForPaths drops m_fileViewer's GDI+ / WebBrowser / EDIT
// — held file handles would otherwise make IFileOperation fail with a sharing
// violation, or in-place encodes can fail on Windows.
#define UWM_RELEASE_PREVIEW_FOR_PATHS (WM_USER + 151)

// Chat worker -> CMainFrame. Posted after image_transform produces output
// files. The frame mirrors the FIRST generated file into the central preview
// and uses the full list as the implicit
// "selection" for the next chat turn — this is how iteration ("now make it
// warmer") works without the user re-picking files in Explorer.
//   wparam = std::vector<std::wstring>* (generated paths; freed by handler)
#define UWM_CHAT_GENERATED       (WM_USER + 142)

// ── Batch queue control (pause / resume / cancel / save-session / load-session)
// Home tab — Run group (alongside cmdRun).
#define IDC_CMD_PAUSE         319
#define IDC_CMD_RESUME        320
#define IDC_CMD_CANCEL        321
// Home tab — Session group.
#define IDC_CMD_SAVE_SESSION  322
#define IDC_CMD_LOAD_SESSION  323

// Posted by worker threads to signal batch lifecycle events.
// UWM_BATCH_PAUSED    — wparam = new BatchState* (partial state at pause point; freed by handler)
// UWM_BATCH_RESUMED   — wparam = 0 (no payload)
// UWM_BATCH_CANCELLED — wparam = new BatchState* (partial state; freed by handler)
// UWM_BATCH_STATE_UPDATE — wparam = new BatchState* (final complete state; freed by handler)
//                          Posted on clean completion and also on pause/cancel paths.
#define UWM_BATCH_PAUSED        (WM_USER + 160)
#define UWM_BATCH_RESUMED       (WM_USER + 161)
#define UWM_BATCH_CANCELLED     (WM_USER + 162)
#define UWM_BATCH_STATE_UPDATE  (WM_USER + 163)
// wparam = heap std::wstring* (current Explorer folder path); handler frees.
#define UWM_EXPLORER_FOLDER_PATH (WM_USER + 164)
// After registry dock restore, apply tab order / active tab in tabbed CDockContainer (deferred; see wxx_docking)
#define UWM_PM_LOAD_DOCK_CONTAINERS (WM_USER + 166)
/// CSettingsView: run LoadCommandProviderOverrides + provider changed handlers off the
/// NewDocker+AddDockedChild path so settings docker creation is not several seconds of cache/JSON.
#define UWM_SETTINGS_DEFERRED_PROVIDER (WM_USER + 167)
/// `CSettingsView`: `lparam` = heap `SettingsRepinitPayload*` (handler deletes) — background Replicate fetch finished.
#define UWM_SETTINGS_REPLICATE_INIT_DONE (WM_USER + 173)
/// heap `pmui::QueueToolCallRowW*` (handler deletes) — CQueueListView::AddToolCallRow
#define UWM_QUEUE_TOOL_CALL   (WM_USER + 168)
/// Pixlwiz Share worker: `wparam` = queue row or -1; `lparam` = heap `std::wstring*` status (handler deletes).
#define UWM_PIXLWIZ_SHARE_PROGRESS (WM_USER + 169)
/// Pixlwiz Share worker: `lparam` = heap `PixlwizShareDoneMsg` (Mainfrm.cpp; handler deletes).
#define UWM_PIXLWIZ_SHARE_DONE     (WM_USER + 170)
/// `lparam` = heap `pmui::PixlwizLoginDonePayload*` (handler deletes) — menu Pixlwiz → Login (headless child)
#define UWM_PIXLWIZ_LOGIN_DONE (WM_USER + 171)
/// Posted when a `CDockPanelBase` caption X hides a panel (or `TogglePanelView` hides). Main frame
/// runs `SaveDockLayout` + `SaveLayout` so `workbench.<slot>.window.panels` matches the active workbench.
#define UWM_PM_SAVE_WORKBENCH_LAYOUT (WM_USER + 172)
/// Posted by ChatWebPanel to select a file path in the File Tree explorer.
/// wparam = heap std::wstring* (path to select); handler deletes and navigates/selects in FileTreePanel.
#define UWM_SELECT_PATH_IN_EXPLORER (WM_USER + 174)
/// Posted by ChatWebPanel to open a file path in the internal centre preview viewer.
/// wparam = heap std::wstring* (path to open); handler deletes and calls previewCoord.Request(ChatGenerated).
#define UWM_OPEN_PATH_INTERNAL (WM_USER + 175)

/// Agent tool (write_file / patch_file / append_file) finished writing a file.
/// wparam = `std::wstring*` (heap-allocated absolute UTF-16 path; receiver deletes).
/// Handled by `CMainFrame::OnToolFileWritten`: reloads the centre viewer if the
/// currently-previewed path matches.
#define UWM_TOOL_FILE_WRITTEN  (WM_USER + 176)
// Async OpenSCAD compile completed for viewer preview.
#define UWM_OPENSCAD_COMPILE_DONE (WM_USER + 177)
// XBlox worker -> main frame: run a JSON console request in the docked terminal.
// wparam = heap std::string* { line, newShell, closeOnExit }; handler deletes.
#define UWM_XBLOX_RUN_CONSOLE (WM_USER + 178)

// RCDATA: `dist/branding/logo.png` + `dist/branding/splash-1.png` (Resource.rc; FEATURE_SPLASH in splash_window.cpp)
#define IDB_PM_LOGO_PNG       5001
#define IDB_PM_SPLASH_1_PNG   5002

#endif // PM_UI_RESOURCE_H
