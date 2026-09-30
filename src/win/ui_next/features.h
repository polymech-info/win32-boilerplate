#pragma once
// ── Feature flags for pm-image --ui-next (Win32++ ribbon + dock UI) ──────────
//
// Features are compile-time gates controlled by CMake options.
// Each feature:
//   • Has an  option(FEATURE_XXX "..." ON/OFF)  in CMakeLists.txt.
//   • When ON: target_compile_definitions(pm-image PRIVATE FEATURE_XXX=1).
//   • Guards panel headers/sources, dock IDs, ribbon commands, and
//     settings persistence fields with  #ifdef FEATURE_XXX / #endif.
//
// ── Adding a new feature ─────────────────────────────────────────────────────
//  1. Add option() + target_compile_definitions() in CMakeLists.txt
//     (see FEATURE_NODES for the pattern).
//  2. Document it below.
//  3. Create panel files; guard with  #ifdef FEATURE_XXX.
//  4. In Mainfrm.h:  add DOCK_ID_XXX + CDockXxx* m_pDockXxx  under #ifdef.
//  5. In Mainfrm_layout.cpp: NewDockerFromID, OnInitialUpdate, ResetLayout, SaveLayout,
//     ApplySavedPanelVisibility, DebugDockState. In workbench: default dock layout + SetupDockContainers.
//  6. In Mainfrm.cpp: Execute (view toggle), IsToggleSelected.
//  7. In RibbonUI.h + RibbonUI.rc (+ locales/RibbonStrings_i18n.rc): add command id, strings,
//     and bitmap resources for a new toolbar button or View toggle.
//  8. In Resource.h: add IDC_CMD_VIEW_XXX if not already in RibbonUI.h.
//  9. In win/LayoutStore.hpp: add pv_xxx to WindowLayout.
//     Update LayoutStore parsing / serialization in win/LayoutStore.cpp.
//
// ── Current features ─────────────────────────────────────────────────────────

// FEATURE_NODES  (cmake -DFEATURE_NODES=ON, default OFF)
// NodeHub graph editor embedded via DX11 + Dear ImGui.
// Heavy dependency: D3D11, imgui, packages/nodehub sources.

// FEATURE_SPLASH  (cmake -DFEATURE_SPLASH=ON, default ON)
// GDI+ startup window: RCDATA `dist/branding/splash-1.png` + `dist/branding/logo.png` (Resource.rc, docs/splash.md,
// helpers/splash_window.cpp). Dismiss: `OnDeferredPostLayoutInit` (150ms) except chat workbench + WebView2,
// which waits for JS `kind:ready` and uses a 10s `WM_TIMER` watchdog (fade splash and continue on timeout);
// `--ui-chat` web: `ready` as before; native chat: OnCreate.
// Disable: cmake -DFEATURE_SPLASH=OFF

// FEATURE_RAW_PREVIEW  (cmake -DFEATURE_RAW_PREVIEW=ON, default ON)
// Fast preview of camera RAW files (ARW, CR2, NEF, DNG, …) via vips_thumbnail.
// vips_thumbnail extracts the camera-embedded JPEG internally (libraw under the hood),
// producing a preview in ~50–300 ms — no separate libraw headers/linking needed.
// The raw extensions are also accepted in the file queue drag-drop and recursive scan.
// Disable: cmake -DFEATURE_RAW_PREVIEW=OFF

// FEATURE_PNG_COMPRESSOR  (cmake -DFEATURE_PNG_COMPRESSOR=ON, default OFF)
// Post-resize PNG optimisation pipeline: libimagequant (palette quantisation,
// lossy) + libpng (high-compression lossless encode).
// Zero runtime DLL dependencies beyond vips-dev (which already ships libpng
// and libimagequant DLLs); static libraries built from source via FetchContent.
// Optional companion flag: FEATURE_PNG_ZOPFLI (default OFF) adds zopfli
// ultra-DEFLATE on top (lossless, 5–20 % smaller, 10–50× slower).

// FEATURE_PNG_ZOPFLI  (cmake -DFEATURE_PNG_ZOPFLI=ON)
// Requires FEATURE_PNG_COMPRESSOR.  Adds google/zopfli re-compression of PNG
// DEFLATE streams via ZopfliPNGOptimize (bundled lodepng for PNG I/O).

// FEATURE_RAW_VIEW  (cmake -DFEATURE_RAW_VIEW=ON, default ON)
// Full-quality Bayer decode in a background thread (vips_image_new_from_file).
// After FEATURE_RAW_PREVIEW shows the embedded JPEG (~instant), a worker thread
// decodes the full 14-bit sensor data via libvips → replaces the fast preview
// with the demosaiced result a few seconds later.
// Requires FEATURE_RAW_PREVIEW to be ON.
// Disable: cmake -DFEATURE_RAW_VIEW=OFF

// FEATURE_USE_OWN_RIBBON  (cmake, default ON; only supported host — stock UIRibbon + Ribbon.bml removed)
// Win32++ CTab + `COwnRibbonToolStrip` (see OwnRibbonTab.cpp). `RibbonUI.h` + `RibbonUI.rc` are
// source files for toolbar command ids, strings, and BMPs (no uicc / Ribbon.xml).

// FEATURE_D2D_GALLERY  (cmake -DFEATURE_D2D_GALLERY=ON, default OFF)
// Replaces the GDI+ image draw path in CFileViewer with a Direct2D DC render
// target, and adds a cover-flow filmstrip dock at the bottom of the image view.
// The filmstrip source lives in src/win/ui_next/gallery/filmstrip/ (copied and
// adapted from apps/win32-mini/filmstrip/; owned by this tree).
// Design: Option A internal delegate — CFileViewer's public API is unchanged;
// Gallery is a rendering delegate for image-mode only. GDI+ m_pImage is kept
// alive for tools, crop, and save; Gallery renders display + filmstrip only.
// D2D is software (DC render target) so no DXGI/airspace issues. Effects
// (ID2D1Effect) require upgrading to a hardware device context later.
// New link deps (gated): d2d1 dwrite windowscodecs.
// Requires: MSVC / Windows 10+ SDK. Disable: cmake -DFEATURE_D2D_GALLERY=OFF

// FEATURE_CONSOLE  (cmake -DFEATURE_CONSOLE=ON, default ON)
// Dockable WebView2 terminal / console panel.  Hosts the xterm.js shell web
// bundle (`dist/shared/shell.html`) inside a `CDockWebConsole` / `CWebConsoleContainer`
// docked at the bottom of the workbench.  Accessible via View → Console
// (`IDC_CMD_VIEW_CONSOLE`).  For development, set the environment variable
// `PM_CONSOLE_URL=http://localhost:5173/` to connect to the Vite dev server;
// the ConPTY native bridge will be attached to `onMessage` in a later step.
// Disable: cmake -DFEATURE_CONSOLE=OFF

// FEATURE_SVG_BUTTONS  (cmake_dependent_option: default ON when FEATURE_USE_OWN_RIBBON is ON)
// Rasters Tabler `icons/filled/*.svg` with ThorVG for the own-ribbon toolbar — `currentColor` is replaced
// per-button. Pass `-D FEATURE_SVG_BUTTONS=OFF` to fall back to ribbon BMP resources; if own ribbon is OFF, SVG is forced OFF.
