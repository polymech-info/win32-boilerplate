Task: Modernize a Win32 / Win32xx settings panel UI without rewriting the whole app.

Context:
- Existing app uses raw Win32 / Win32xx controls.
- Panel currently creates controls manually with `CreateWindowExW`.
- Controls include:
  - STATIC labels / section headers
  - COMBOBOX dropdowns
  - EDIT boxes
  - BUTTON checkboxes
  - TRACKBAR sliders
  - scrollable content host
- Current theming already handles:
  - `WM_CTLCOLORSTATIC`
  - `WM_CTLCOLORBTN`
  - `WM_CTLCOLOREDIT`
  - `WM_CTLCOLORLISTBOX`
  - custom separators
  - light/dark palette via `pmui::theme_palette()`

Goal:
Make the dropdowns, edit boxes, checkboxes, buttons, sliders, and scrollbars look more modern while staying native enough and avoiding heavy frameworks like Qt / GTK / .NET.

Important constraints:
- Keep Win32 / Win32xx.
- Prefer minimal dependencies.
- Must support dark mode.
- Must still work with existing message routing.
- Avoid owner-drawing everything unless necessary.
- Prefer incremental improvements.
- Code should be practical C++17 / Win32.

Please propose the best implementation strategy and provide ready-to-use code patterns.

Desired approach:

## 1. Use modern visual styles first

Enable common controls v6 via app manifest:

```xml
<dependency>
  <dependentAssembly>
    <assemblyIdentity
      type="win32"
      name="Microsoft.Windows.Common-Controls"
      version="6.0.0.0"
      processorArchitecture="*"
      publicKeyToken="6595b64144ccf1df"
      language="*" />
  </dependentAssembly>
</dependency>
```

Initialize common controls:

```cpp
INITCOMMONCONTROLSEX icc{};
icc.dwSize = sizeof(icc);
icc.dwICC =
    ICC_STANDARD_CLASSES |
    ICC_WIN95_CLASSES |
    ICC_BAR_CLASSES |
    ICC_LISTVIEW_CLASSES;
InitCommonControlsEx(&icc);
```

## 2. Use Explorer / dark-friendly themes where possible

Apply themes after creating controls:

```cpp
SetWindowTheme(hwndCombo, L"Explorer", nullptr);
SetWindowTheme(hwndEdit,  L"Explorer", nullptr);
SetWindowTheme(hwndButton,L"Explorer", nullptr);
SetWindowTheme(hwndTrack, L"Explorer", nullptr);
```

For dark mode builds, try:

```cpp
SetWindowTheme(hwndCombo, L"DarkMode_Explorer", nullptr);
SetWindowTheme(hwndEdit,  L"DarkMode_Explorer", nullptr);
SetWindowTheme(hwndButton,L"DarkMode_Explorer", nullptr);
```

But keep fallback to normal `Explorer`, because dark theme support varies by Windows version.

## 3. Fix combo boxes

Use `CBS_DROPDOWNLIST` instead of editable combo boxes where the user should only choose a preset:

```cpp
CreateWindowExW(
    0,
    WC_COMBOBOXW,
    nullptr,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
    CBS_DROPDOWNLIST | CBS_HASSTRINGS,
    x, y, w, 300,
    parent,
    (HMENU)id,
    hInst,
    nullptr
);
```

Avoid `CBS_SIMPLE`.

For dropdown height, the window height must be larger than the visible part:

```cpp
// visible control height is still normal, but dropdown gets space
MoveWindow(combo, x, y, w, 300, TRUE);
```

## 4. Use better spacing and sizing

Current UI looks old mostly because controls are cramped.

Recommended metrics:

```cpp
constexpr int padX = 16;
constexpr int labelW = 96;
constexpr int gap = 8;
constexpr int controlH = 24;
constexpr int rowGap = 8;
constexpr int sectionGap = 18;
constexpr int sectionTopGap = 12;
```

Use rows around 30–34 px high.

Avoid 16 px high labels; use 18–20.

## 5. Replace raw etched separators

Current custom separator is good. Keep it.

Use more whitespace around sections:

```cpp
Section title
8 px gap
controls
14–18 px gap
separator
12 px gap
next section
```

## 6. Modernize edit boxes

Use borderless or soft-bordered edit controls.

Option A: keep native border:

```cpp
CreateWindowExW(
    WS_EX_CLIENTEDGE,
    WC_EDITW,
    L"",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
    ES_AUTOHSCROLL,
    x, y, w, controlH,
    parent,
    (HMENU)id,
    hInst,
    nullptr
);
```

Option B: flatter modern edit:

```cpp
CreateWindowExW(
    0,
    WC_EDITW,
    L"",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
    ES_AUTOHSCROLL,
    x, y, w, controlH,
    parent,
    (HMENU)id,
    hInst,
    nullptr
);
```

Then subclass and paint a 1 px rounded-ish border in parent/background color.

## 7. Buttons

For small browse buttons, avoid `...` if possible.

Better:

```text
Browse…
```

or icon-only folder button.

Minimum width:

```cpp
constexpr int browseW = 72;
```

For tiny buttons, subclass and custom-paint only the button background/border/text.

## 8. Sliders

Trackbar is hard to make modern using only `WM_CTLCOLOR`.

Best options:

1. Accept native trackbar with visual styles.
2. Use custom lightweight slider control.
3. Use Direct2D only for sliders and modern buttons.

Recommended: create a small custom slider control with:

* rounded track
* accent-colored filled part
* round thumb
* keyboard support
* `WM_HSCROLL` compatible notification to parent

## 9. Scrollbar

Native Win32 scrollbars always look old.

Best options:

1. Accept it.
2. Hide native scrollbar and implement overlay scrollbar.
3. Use `ScrollWindowEx` + custom scrollbar child.

Recommended for this app:

* keep native scrollbar for now
* later replace with custom overlay scrollbar only for the settings panel

## 10. Dark mode

Use undocumented dark mode APIs only behind runtime checks.

Need helper:

```cpp
using fnShouldAppsUseDarkMode = BOOL(WINAPI*)();
using fnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);

void TryAllowDarkMode(HWND hwnd)
{
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;

    auto AllowDarkModeForWindow =
        reinterpret_cast<fnAllowDarkModeForWindow>(
            GetProcAddress(ux, MAKEINTRESOURCEA(133)));

    if (AllowDarkModeForWindow)
        AllowDarkModeForWindow(hwnd, TRUE);
}
```

Also try:

```cpp
BOOL dark = TRUE;
DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
```

Fallback safely if unsupported.

## 11. Combo popup dark mode

The dropdown listbox is tricky.

Current `WM_CTLCOLORLISTBOX` is needed and should stay:

```cpp
case WM_CTLCOLORLISTBOX:
{
    auto& pal = pmui::theme_palette();
    HDC hdc = reinterpret_cast<HDC>(wparam);
    SetTextColor(hdc, pal.control_fg);
    SetBkColor(hdc, pal.control_bg);
    return reinterpret_cast<LRESULT>(GetControlBrush(pal.control_bg));
}
```

But selected item colors may still be system-owned unless owner-drawn.

If dark combo dropdowns look bad, use owner-drawn fixed combo boxes.

## 12. Owner-drawn combo box fallback

Use only for combos where Windows theming fails.

Create combo:

```cpp
CreateWindowExW(
    0,
    WC_COMBOBOXW,
    nullptr,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
    CBS_DROPDOWNLIST |
    CBS_OWNERDRAWFIXED |
    CBS_HASSTRINGS,
    x, y, w, 300,
    parent,
    (HMENU)id,
    hInst,
    nullptr
);
```

Handle:

```cpp
case WM_MEASUREITEM:
{
    auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
    mi->itemHeight = 24;
    return TRUE;
}

case WM_DRAWITEM:
{
    auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
    if (di->CtlType != ODT_COMBOBOX) break;

    const auto& pal = pmui::theme_palette();

    bool selected = (di->itemState & ODS_SELECTED) != 0;
    COLORREF bg = selected ? pal.accent_bg : pal.control_bg;
    COLORREF fg = selected ? pal.accent_fg : pal.control_fg;

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(di->hDC, &di->rcItem, br);
    DeleteObject(br);

    if (di->itemID != (UINT)-1) {
        wchar_t text[512]{};
        SendMessageW(di->hwndItem, CB_GETLBTEXT, di->itemID, (LPARAM)text);

        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);

        RECT r = di->rcItem;
        r.left += 8;

        DrawTextW(
            di->hDC,
            text,
            -1,
            &r,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
        );
    }

    if (di->itemState & ODS_FOCUS)
        DrawFocusRect(di->hDC, &di->rcItem);

    return TRUE;
}
```

## 13. Better checkbox style

Use native checkbox but increase spacing.

```cpp
CreateWindowExW(
    0,
    WC_BUTTONW,
    L"EXIF autorotate",
    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
    BS_AUTOCHECKBOX,
    x, y, w, 24,
    parent,
    (HMENU)id,
    hInst,
    nullptr
);
```

Apply theme:

```cpp
SetWindowTheme(cb, L"Explorer", nullptr);
```

Keep existing `NM_CUSTOMDRAW` text-color workaround.

## 14. Recommended visual target

Use this layout:

```text
Dimensions
  Resolution   [ combo                         ]
  Ratio        [ combo                         ]
  Size         [ width     ] × [ height        ]
  Fit          [ combo                         ]

Output
  Destination  [ combo                         ]
  Folder       [ edit                  ][Browse]

Quality
  Quality      [ slider                 ] 85
  Kernel       [ combo                         ]

Options
  [x] EXIF autorotate
  [ ] Allow enlargement
  [x] Strip metadata on save
```

Improvements:

* use `Size` instead of separate `Width` / `H`
* align controls
* wider controls
* more vertical breathing room
* full-word button labels
* optional section cards / group backgrounds

## 15. Optional: modern panel card background

Instead of the whole view being flat gray, draw section cards:

```text
[ Dimensions card ]
[ Output card     ]
[ Quality card    ]
[ Options card    ]
```

Each card:

* slightly different background
* 1 px border
* 6–8 px radius if custom painting
* native child controls placed inside

This gives a modern look without replacing all controls.

## 16. Best staged roadmap

Stage 1:

* enable comctl32 v6 manifest
* apply `Explorer` / `DarkMode_Explorer`
* improve spacing
* use `CBS_DROPDOWNLIST`
* remove tiny controls
* use better fonts
* keep native scrollbar

Stage 2:

* owner-draw combo boxes only if dark mode is broken
* custom slider
* custom small buttons

Stage 3:

* custom overlay scrollbar
* card-style painted section backgrounds
* optional Direct2D rendering helper

Stage 4:

* replace settings panel with a small retained-mode custom UI layer, but keep Win32 host

## 17. Please provide code for

1. `ApplyModernTheme(HWND hwnd)`
2. `CreateModernCombo(...)`
3. `CreateModernEdit(...)`
4. `CreateModernCheckbox(...)`
5. optional owner-draw combo handlers
6. optional custom slider control
7. example refactor of one section: `Dimensions`

Code should fit into the existing style:

* C++17
* Win32
* no MFC
* compatible with Win32xx
* no large dependencies
* use `pmui::theme_palette()`
* keep existing `WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*` behavior

---

## Evaluation: current `CSettingsView` (2026)

This section maps the goals above to the implementation in `src/win/ui_next/SettingsPanel*.cpp` and shared theming.

| Area | Status | Notes |
|------|--------|--------|
| §1 Visual styles (comctl v6) | **Done** | `PerMonitorV2.manifest` / `media-img-win.manifest` include `Microsoft.Windows.Common-Controls` 6.0; Win32++ initializes common controls on startup. |
| §2 `SetWindowTheme` (Explorer / dark) | **Done (centralized)** | Not called inside each `Create*Controls` helper; `pmui::apply_window_theme_recursive()` in `helpers/theme.cpp` walks non–Win32++ children after `ApplyAppearance`, using `DarkMode_Explorer` or `Explorer` as appropriate. |
| §3 Combo boxes (`CBS_DROPDOWNLIST`, tall drop) | **Done** | All settings combos use `CBS_DROPDOWNLIST \| CBS_HASSTRINGS` with drop heights 150–300px (e.g. `SettingsPanel_Resize.cpp`). **Rounded look (v1):** `SetWindowRgn` + `CreateRoundRectRgn` on **single-line EDIT** only (subclass; multiline skipped). Combos use the **post-`WM_PAINT` dark hack** below instead of a region. Not WinUI — see **Backlog** below. |
| §4 Spacing / metrics | **Done** | `pmui::settings_controls::Layout` + `CreateControls` share `pad_x=16`, `label_w=96`, `gap=8`, `control_h=24`, `row_dy=32` (`helpers/ui_constants.hpp`). Field column width uses `SettingsContentClientWidth()` so the content track matches the scroll host when a vertical scrollbar is present. |
| §5 Separators | **Done** | `AddThemedSep` + `SettingsPanelHorzSepProc` custom hairline; avoids `SS_ETCHEDHORZ` in dark mode. |
| §6 Edits / combos (settings) | **Flatter** | `create_preset_combo` / `create_single_line_edit` use **no** `WS_EX_CLIENTEDGE`. **Dark:** `COMBOBOX` → **`L""` + ctlcolor subclass + post-`WM_PAINT` overpaint** (Explorer / CFD / `DarkMode_Explorer` all tend to a light field, white edge, or bright chevron; see below). **Light:** `COMBOBOX` → **`Explorer`**. The walk still strips 3D edges. Multiline edits / listboxes: no client edge. |
| §7 Browse / small buttons | **Done** | Folder rows: `create_browse_button` + i18n. Replicate `↻` / presets / API / refs: `InstallSettingsPanelTooltips()` + `TransformStrings` / `FindStrings` / `MetaStrings` `tt_*`. |
| §8 Sliders | **Native (backlog: custom)** | `TRACKBAR_CLASSW` + `TBS_NOTICKS`. **Dark:** `msctls_trackbar32` uses **`L""`** in the theme walk (Explorer / `DarkMode_Explorer` often leaves a near-white track channel). Custom painted slider remains optional. |
| §9 Scrollbar | **Native (backlog: overlay)** | `WS_VSCROLL` + `ContentHostSubclass`. Overlay scrollbar not implemented. |
| §10–11 Dark mode + listbox | **Done** | Frame + `WM_CTLCOLORLISTBOX` / `WM_CTLCOLOREDIT` via content-host forward. |
| §12 Owner-draw combo | **On demand** | No `CBS_OWNERDRAWFIXED` until a build shows broken dropdown selection colours. |
| §13 Checkboxes | **Done** | `BS_AUTOCHECKBOX` + `NM_CUSTOMDRAW` workaround in `CSettingsView::WndProc`. |
| Replicate “Collection” row | **Done** | `SyncReplicateRowVisibilityForMode()` runs after `SetMode`; deferred provider updates only call `set_row_visible(..., true)` when the active mode is Transform / Find / Meta, so the row does not leak onto other tabs. |
| §14–15 Layout / cards | **Done** | i18n `lbl_size` row (W×H) on Resize; field column `clientW - field_x() - pad_x`. Section title rows are full-width `STATIC` controls with width `sepW` (not short fixed pixel widths) across mode panels. `AddSectionCard` / `SettingsPanelCardProc` remain in the tree for potential reuse, but **active mode UIs do not use opaque card backdrops** (they overlapped controls / conflicted with z-order); see comment in `SettingsPanel_Resize.cpp`. |
| §17 Helper APIs | **Done** | `settings_controls.hpp` + `helpers/ui_constants.hpp` + `settings_widgets.hpp`; `SetWindowTheme` stays centralized in `theme.cpp`. |

### Backlog — rounded “pill” inputs (native Win32)

Win32 has **no** corner-radius style for `COMBOBOX` / `EDIT`. The current approach is a **clipping** workaround applied inside [`apply_window_theme_recursive`](../src/win/ui_next/helpers/theme.cpp) (`input_field_*` helpers): **eligible** = **single-line** `EDIT` only (combos were removed — region + default combo paint clashed with soft borders; see **post-`WM_PAINT` overpaint** below). **Out of scope for v1:** dropdown list window (`ComboLBox`), multiline fields, true focus-ring / border match to VS / Fluent, HiDPI edge cases. **Pick up later:** tune ellipse size, scope (all docks vs. settings-only), remove if regressions, or replace with owner-draw / a different host — **deferred; not final UX.**

### Shell file tree vs. app-owned inputs vs. dock toolstrips (separate theme paths)

| Surface | Code | What we do |
|--------|------|------------|
| **Settings / Queue / Log / …** (standard `COMBOBOX`, `EDIT`, …) | [`helpers/theme.cpp`](../src/win/ui_next/helpers/theme.cpp) `apply_window_theme_recursive` | `SetWindowTheme` per class; **dark** `COMBOBOX` / push `BUTTON` / `msctls_trackbar32` often → **`L""`** + see **post-`WM_PAINT` overpaint** below; strip sunken `WS_EX_*`; optional `SetWindowRgn` on **single-line** `EDIT` for pill-like corners. |
| **In-app file list** (`IExplorerBrowser`) | [`FileTreePanel.cpp`](../src/win/ui_next/FileTreePanel.cpp) `CExplorerBrowserView::InitBrowser` | Always **`EBO_NOWRAPPERWINDOW \| EBO_NOBORDER`** — removes Shell’s extra wrapper and **inner list border** so the dock host owns the edge. Optional `EBO_SHOWFRAMES` for address bar (often stays light). |
| **Dock header toolstrips** (log / settings icon row) | [`dock_panel_toolstrip.cpp`](../src/win/ui_next/helpers/dock_panel_toolstrip.cpp) `CDockPanelToolStrip` | `CToolBar` in a small host: **`ApplyTheme` + `NMTBCUSTOMDRAW`** and `WM_ERASEBKGND` (strip + hairline from `theme_palette()`). **Not** part of the global `apply_window_theme_recursive` pass — the container calls `ApplyTheme` when the frame refreshes. |

`CMainFrame::ApplyAppearance` **does not** call `apply_window_theme_recursive` on the File tree docker: Shell COM recreates and hit-tests inner windows; a full theme walk was a bad fight (see comment in `Mainfrm.cpp` next to `m_pDockFileTree`). So: **file tree** = Shell options + exclude walk; **settings inputs** = `theme.cpp` (including the hack below) + `CSettingsView` `WM_CTLCOLOR*`; **dock toolstrip** = explicit `ApplyTheme` in `dock_panel_toolstrip.cpp`.

### Dark mode: the “other hack” — `WM_CTLCOLOR*` is not enough (post-`WM_PAINT` overpaint)

`SetWindowTheme` to Explorer / `DarkMode_*` plus a parent (or child) that returns the right brush on `WM_CTLCOLOREDIT`, `WM_CTLCOLORLISTBOX`, `WM_CTLCOLORSTATIC`, or `WM_CTLCOLORBTN` is **necessary** but often **insufficient** on comctl v6. The control’s own **`WM_PAINT` can still run after** the ctl-color phase and **draw a light 1px frame, a light dropdown button shell, a system “selection / focus” tint, a white track channel, or a gray bevel on push buttons** — it reads like light-theme chrome dropped on a dark panel.

**Implemented pattern** (reusable for other host-owned controls that fight the palette):

1. **Clear the visual-style app id** for that concrete `HWND` when a themed dark name looks wrong: e.g. **`SetWindowTheme(h, L"", L"")`** (classic GDI) so you are not stuck with `DarkMode_Explorer`’s read-only list fill or Explorer’s light track.
2. **Keep ctlcolor** where it applies: a **`SetWindowSubclass`** on the **control** so you see messages **before** the default wndproc. For **`CBS_DROPDOWNLIST`**, the static / list / embedded-edit pieces parent to the **combo `HWND`**, not the settings panel — **subclass the combo** and return `control_bg` from `WM_CTLCOLOREDIT` / `WM_CTLCOLORSTATIC` / `WM_CTLCOLORLISTBOX` in that subclass. The content host can still forward the panel’s `WM_CTLCOLOREDIT` for real **`EDIT` children** only.
3. **Post-`WM_PAINT` overpaint** (the actual “hack”): in the same subclass, **after** `DefSubclassProc(hwnd, WM_PAINT, wParam, lParam)` returns, `GetDC(hwnd)` and
   * **`COMBOBOX`:** `GetComboBoxInfo` → fill `rcItem` and `rcButton` with `ThemePalette::control_bg`, redraw the static label with `control_fg`, draw a small chevron in the button, then a **1px** outline with `ThemePalette::caption_pen` (same family as hairline separators; avoids a harsh white `FrameRect` from the default).
   * **Push `BUTTON` (`BS_PUSHBUTTON` / `BS_DEFPUSHBUTTON`):** fill the client with `control_bg`, `DrawTextW` the caption centered (use `caption_fg_inactive` if `!IsWindowEnabled`), 1px `caption_pen` border, optional **2px** inset `accent` loop if focused (instead of a flaky `DrawFocusRect` XOR in some stacks).
4. **Trackbar:** no cheap ctlcolor; **unthemed** `L""` in dark often yields a grayer track than `DarkMode_Explorer`. If that is still wrong on a build, the next step is `NM_CUSTOMDRAW` on the parent, not more `SetWindowTheme` string guessing.

**Where in tree:** all of the above is centralized in [`helpers/theme.cpp`](../src/win/ui_next/helpers/theme.cpp) (subclass id **100** = combo ctlcolor + post-paint, **102** = push post-paint) and invoked from `apply_combobox_subclass_to_descendants` + the per-class `SetWindowTheme` block inside `apply_window_theme_recursive` — not in each `Create*Controls` file. **Re-use elsewhere:** if a new `COMBOBOX` / `BUTTON` / `TRACKBAR` in another docker still “blooms” light, ensure it goes through the same walk (or call `pmui::apply_window_theme_recursive(root, dark)` on that subtree after you create the control), then add another **message** to the same subclass (e.g. `WM_NCPAINT`) or a one-off `SetWindowSubclass` for that class only if the default paint order differs (owner-draw, split buttons, `ComboLBox` popup, etc.).

**Content host:** `CSettingsView`’s scroll content host (`ContentHostSubclass` in `SettingsPanel_core.cpp`) **forwards** `WM_CTLCOLOR*` to the outer view so `WM_CTLCOLOREDIT` for a direct child `EDIT` still works; the combo’s **internal** static/listbox/edit messages are **delivered to the `COMBOBOX` window**, so the **combo** must be **subclassed** on the combo `HWND` — the panel’s `WM_CTLCOLOREDIT` is never the right `HWND` for the closed `CBS_DROPDOWNLIST` field.

---

## TODO list (by chapter)

Work items below follow the numbered sections in this document. Check them off as you complete them.

### §1 — Visual styles

- [x] Rely on comctl32 v6 manifest (verify when adding a new EXE target).

### §2 — Explorer / dark themes

- [x] Ensure new child control classes in the settings stack are covered by `theme.cpp` (or document an exception if a control must stay unthemed).

### §3 — Combo boxes

- [x] Preset-only fields use `CBS_DROPDOWNLIST` (not `CBS_SIMPLE` / editable dropdowns where inappropriate).
- [x] Audited: `ProviderDlg` keeps `CBS_DROPDOWN` for non-Replicate providers (custom model id); documented in code.
- [ ] **Revisit** rounded `SetWindowRgn` inputs: polish, limits, or product decision to keep/drop (see **Backlog — rounded “pill” inputs** above).

### §4 — Spacing and sizing

- [x] `CreateControls` + `pmui::settings_controls::Layout` use §4-style metrics (`pad_x` 16, `gap` 8, `control_h` 24, `row_dy` 32); re-test after layout changes.
- [x] `Layout::label_h` 18px for field labels (`MakeLabel`, Meta/Compress `create_field_label` helpers, Duplicates `label` lambda, etc.).
- [x] `SettingsContentClientWidth()` + `m_settingsLayoutContentW` keep mode panel geometry aligned with the visible scroll content width.

### §5 — Separators

- [x] Keep custom themed separators; adjust vertical gaps only if §4 changes.

### §6 — Modern edits

- [x] Settings panes: **Option B–style** — no `WS_EX_CLIENTEDGE` on factory-built edits/combos. **Dark** combos: `L""` + ctlcolor subclass + post-`WM_PAINT` overpaint in `theme.cpp` (see **Dark mode: the “other hack”**). **Light** combos: `Explorer`. Custom 1px border on combo via `WM_NCPAINT`: not implemented; hairline in post-paint is the current story.

### §7 — Buttons

- [x] Resize / Compress / Meta output rows: `create_browse_button` + i18n `browse_button_caption` (min width 72px).
- [x] Transform / Find / Meta: `InstallSettingsPanelTooltips()` (shared `m_hTooltipSettings`) for refresh, presets, API keys, reference add/clear; Find `m_hFindApiKeys` wired for `tt_api_keys`.

### §8 — Sliders

- [x] **Backlog:** optional custom slider (rounded track, accent) — not implemented; native trackbar is the supported path.

### §9 — Scrollbar

- [x] **Backlog:** optional overlay scrollbar for the settings content host — not implemented; native `WS_VSCROLL` remains.

### §10 — Dark mode APIs

- [x] Window-level dark mode handled in main frame; no duplicate `AllowDarkModeForWindow` in `CSettingsView` required if global path suffices.

### §11–12 — Combo popup / owner-draw

- [x] **N/A for now** — add owner-draw (`WM_DRAWITEM` / `WM_MEASUREITEM`) only if a Windows build regresses combo dropdown colours.

### §13 — Checkboxes

- [x] Keep `NM_CUSTOMDRAW` workaround; re-verify when changing `SetWindowTheme` for `BUTTON`.

### §14 — Recommended layout

- [x] i18n `ResizeStrings::lbl_size` + single “Size” row (width × height) on Resize.
- [x] Field column: `cw = clientW - field_x() - pad_x` in `CreateControls` (symmetric right margin).

### §15 — Section cards

- [x] **API:** `CSettingsView::AddSectionCard` + `SettingsPanelCardProc` (rounded-rect + border) still exist; `WM_CTLCOLORSTATIC` / `kSettingsCardUserData` as implemented.
- [x] **Current product UI:** mode panels (Resize, Compress, …) **do not** mount these card statics; section grouping is hairline separators + full-width `sepW` section header statics only.

### §16 — Staged roadmap

- [x] **Stage 1:** done (see above).
- [x] **Stage 2 (partial / backlog):** owner-draw combos on demand only; custom slider + extra polish = backlog. Browse: done (§7).
- [x] **Stage 3 (partial / backlog):** optional GDI section cards = **not** used in shipped mode panels (see §15); overlay scrollbar = backlog.
- [x] **Stage 4 — backlog:** retained-mode custom UI layer — not started; only if the product reopens scope.

### §17 — Code helpers

- [x] `settings_controls.hpp`: `Layout`, `create_preset_combo`, `create_single_line_edit`, `create_autocheck`, `create_browse_button`, `field_width_with_browse` / `browse_button_x`.
- [x] Mode panels: `create_field_label` / `create_preset_combo` / `create_autocheck` + struct params where applicable (including Compress field labels; Meta/Transform/Find multiline + list fields avoid `WS_EX_CLIENTEDGE` for softer chrome).

## Policy

- Central layout constants: [`helpers/ui_constants.hpp`](../src/win/ui_next/helpers/ui_constants.hpp) (`pmui::ui::SettingsPaneLayout`). `settings_controls.hpp` aliases it as `Layout` for existing call sites.
- Control factories: [`settings_controls.hpp`](../src/win/ui_next/settings_controls.hpp) — prefer **struct** parameters (`PresetComboParams`, `LineEditParams`, `FieldLabelParams`, …) over long positional argument lists.
- Optional thin holders: [`settings_widgets.hpp`](../src/win/ui_next/settings_widgets.hpp) — `PresetCombo`, `LineEdit`, `Autocheck` (own `HWND`; **notifications stay** `WM_COMMAND` / `WM_NOTIFY` to the parent, not virtual handlers here).

## Steps

- **Build:** from `packages/media/cpp`, `npm run buildf` (incremental Release).
- **Screenshot:** with the UI already running, `.\dist\pm-image.exe app takescreenshot -o last.png` (or default path under `screenshots\`). Check `last.png` against spec; refine and repeat. The window should show explorer, chat, and resize settings — improve settings panes first.

## References

C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\wingdi.h
