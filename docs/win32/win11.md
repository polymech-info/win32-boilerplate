# Windows 11 File Explorer: context menu roadmap

This document is the **implementation checklist** for getting PM-Image commands (resize, Presets, Chat, **Open in …**, etc.) onto the **modern** Windows 11 File Explorer right‑click menu, without making **PowerShell static verbs** the default. For how things work **today** (`IExecute` + `register-explorer`), see [explorer.md](explorer.md).

## Problem (short)

- **Current stack:** `pm-image-execute.dll` implements **`IExecuteCommand`**; `register-explorer` writes **per‑user** `HKCU\Software\Classes\…` with **`DelegateExecute`**, plus **Open with** / ProgId support.
- **What works:** Full merge in **classic** menu hosts (e.g. many third‑party file managers) and the **Open with** surface in Explorer.
- **What is inconsistent:** **Windows 11 File Explorer’s** new context menu is built on a **different** shell path. It does **not** treat legacy static `shell\…` + **`IExecuteCommand`** the same way as a classic `DefView` host. “Show more options” is not guaranteed to show the same set as Windows 10 or as other apps.
- **Product direction (agreed):** Do **not** “fix” this by defaulting to **`explorer-*.ps1`** static registration. The long‑term fix is the **app‑model** path Explorer 11 is designed for.

**Microsoft’s direction (read first):**

- [Extending the context menu and share dialog in Windows 11](https://blogs.windows.com/windowsdeveloper/2021/07/19/extending-the-context-menu-and-share-dialog-in-windows-11/) (Windows Developer Blog, 2021) — `IExplorerCommand`, packaging, grouping.
- [Integrate a packaged desktop app with File Explorer](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer) (Microsoft Learn) — package extensions, manifest.
- [Grant identity to non-packaged desktop apps (sparse package)](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/grant-identity-to-apps-by-using-sparse-packaging) (Microsoft Learn) — if you keep an **NSIS-style** loose layout but need **package identity**.

## What “new interface + identity” means

| Term | Meaning |
|------|--------|
| **New interface (for this menu)** | Implement **`IExplorerCommand`** (and the usual COM **class factory** / `DllGetClassObject` stack) in a way **registered in the app manifest** for **File Explorer** (not only under `…\shell\…\Command` in classes). The old **`IExecuteCommand`**-only path remains valid for other hosts; it is **not** the Win11 *primary* Explorer menu’s intended contract. |
| **Identity** | A **signed `AppxManifest`**, **package family name**, and registration so Windows classifies the extension as part of a **packaged** or **sparse** app, not a random unsigned DLL. Without identity, the **new** menu often will not list or group your commands like first‑party entries. |
| **Sparse package** | A middle ground: your EXE/DLLs can still live in an **existing install tree**; you ship/copy a small **manifest + registration** to get identity. **Full MSIX** is the other option (everything inside the package). **Choose in design:** upgrade path, signing, and CI constraints. |
| **CMake’s role** | **Orchestrate** the build: produce the new DLL, **stamp** the manifest, run **`MakeAppx`**, **SignTool** (release), and optional post‑steps. CMake does not replace the **Windows SDK**; it wires **when** and **where** the tools run. You can develop on **Windows 10**; you **validate** the Explorer menu on **Windows 11**. |

## Essentials in this repo (sparse + `IExplorerCommand`)

These pieces are **in the tree** for the Win11 path; they are **additive** to the existing `register-explorer` + `IExecuteCommand` flow (see [explorer.md](explorer.md)).

| Item | Location / detail |
|------|-------------------|
| **COM server** | `src/win/explorer11/pm_image_explorer11.cpp` — **`IExplorerCommand`**, **`GetTitle` / `GetIcon` / `GetToolTip` / `GetState` / `Invoke` / `GetFlags` / `EnumSubCommands`**, in-proc class factory, **`DllGetClassObject` / `DllCanUnloadNow`**. **CLSID** `{A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D}` = studio (`resize --ui-next --src`), **`{B2B3C4D5-E6F7-4A6B-9D8E-1F2A3B4C5D6E}`** = viewer (`--ui-preset=viewer --src`); both must match manifest + CMake. |
| **DEF** | `src/win/explorer11/pm_image_explorer11.def` |
| **Sparse manifest template** | `branding/AppxManifest.xml.in` → CMake **`configure_file`** → `dist/sparse/AppxManifest.xml` (`com:InprocServer`, `desktop4:FileExplorerContextMenus` for **`.jpg` / `.jpeg` / `.png`**, `runFullTrust`, `pm-image.exe` as the application). |
| **Build** | `cmake/Branding.cmake` — `MEDIA_TARGET_EXPLORER11` / `pm-image-explorer11`. **`PM_BUILD_EXPLORER11_DLL`** (default **ON**). A **`pm-image` `POST_BUILD`** step copies **`pm-image.exe`** and **`pm-image-explorer11.dll`** into **`dist/sparse/`** after a successful build. |
| **Dev Authenticode (self‑signed)** | `npm run build:sign` → `scripts/codesign-dev.ps1` — PFX at **`dist/certs/pm-image-dev-codesign.pfx`**. **Password:** `PM_CODESIGN_PFX_PASSWORD`, or **`dist/certs/.pm-codesign-pfx.password`** (auto‑created on first PFX export; **gitignored**), or an interactive prompt. |
| **Local registration** | `scripts/register-sparse-explorer11.ps1` — `Add-AppxPackage -Register -Path` to `dist\sparse\AppxManifest.xml` (default layout root: repo `dist\sparse`; override with **`-LayoutRoot`**). |

**Still TBD for shipping:** signing and **publisher** alignment with `Identity@Publisher@`, `MakeAppx` / store or sideload policy, **NSIS** post-install hook, and a **real Win11** Explorer right‑click smoke test. Registration may require **Developer settings** (or an approved cert workflow) on the test machine; manifest schema can vary by **Windows / SDK** target.

## Next steps (ordered checklist)

### 1. Architecture decision

- [ ] **Sparse vs full MSIX:**  
  - **Sparse:** Closer to today’s **NSIS** + loose files; register package after install.  
  - **MSIX:** Stricter layout; often a separate distribution; may interact with the existing `installer.nsi` flow and dual-artifact release planning.
- [ ] **One DLL or two:** Either a **new** `pm-image-explorercommand.dll` (or similar) implementing **`IExplorerCommand`**, or refactor shared **spawn/selection** logic with `pm_image_iexecute.cpp` into a **static** library used by both DLLs. Reuse the same “payload” idea (command kind, width, preset id) where possible; avoid duplicating business rules.

### 2. Implement the COM server

- [ ] Implement **`IExplorerCommand`**, typically plus **`IObjectWithSite`**, following the blog / SDK samples, with **`GetFlags`**, **`GetTitle`**, **`GetIcon`**, **`GetState`**, and **`Invoke`** (delegate to the same `pm-image.exe` launch paths as IExecute, or a thin shared module).
- [ ] Support **per‑command** or **hierarchical** commands: Explorer may show a **submenu** (e.g. “PM-Image” with child actions). Map this to your existing **PM-Media** / Presets / resize matrix without exceeding practical menu limits.
- [ ] **CLSIDs, threading model, registration:** Follow manifest‑driven **COM** registration; avoid conflicting with the existing **`IExecute` CLSIDs** in the registry.
- [ ] **Security:** Match **AppContainer** / integrity expectations; test **low‑IL** Explorer as host.

### 3. Manifest and packaging

- [ ] Add **`AppxManifest.xml`** with the correct **extensions** for `windows.fileExplorerContextMenus` (and **`windows.comServer`** for the COM class, as required by the schema version you target). **Verify** the **exact** element names for your **minimum** Windows 11 / SDK build.
- [ ] Define **package identity** (name, version, publisher **subject** that matches the signing cert, processor architecture).
- [ ] Create a **CMake** target (or `add_custom_target`) that:  
  - copies the built DLL + manifest (and any resources) into a **package layout**;  
  - runs **`MakeAppx` / bundle** as appropriate;  
  - calls **`SignTool`** in **release** (store cert, or internal CA).
- [ ] **Developer inner loop:** script **`Add-AppxPackage -Register` / `-Path`** (sparse) for local testing; document the exact command in this repo (or in `package.json` as an optional `npm` script that shells out).

### 4. Coexistence with the current `register-explorer` flow

- [ ] Decide: **IExplorerCommand** is **added** for Win11, while **`register-explorer`** + **`IExecute`** remain for **Open with** and for **hosts that use classic merge**; or a **phased** migration. Document **uninstall** (unregister package + existing `register-explorer --unregister` behavior).
- [ ] **Do not** remove `IExecute` in the first pass unless a later milestone proves full replacement; avoid breaking Salamander / Win10 / classic users.

### 5. Installer (NSIS) and distribution

- [ ] Either ship a **.msix** (or **sparse** register payload) as an extra artifact, or run **Add-AppxPackage** from the installer (needs **user context**, signing, and sometimes **developer** mode for loose registration — **validate** supported scenarios).
- [ ] Update `installer.nsi` and **post‑install** steps: copy manifest layout, run registration **once** per user or machine per your policy.
- [ ] **Upgrade:** version bumps in the manifest, clean upgrade path.

### 6. Build / CI

- [ ] **Windows 10/11** SDK and **WDK**-aligned signing on the build host.
- [ ] **CI:** build the package **artifact**; optional **signed** build on a secure agent. **No** headless VM can fully replace a **Win11** manual test for “does Explorer show the menu?”.
- [ ] **Nightly/QA:** one **Win11** VM to smoke‑test: right‑click on `.jpg` / `Directory` / **Library** paths (known Explorer quirks; see e.g. community write‑ups on **Libraries** and context menus).

### 7. User‑visible documentation (for support)

- [ ] **Until** this ships: one paragraph in user‑facing docs: Win11’s **new** menu may not list all commands; **workarounds** = classic menu OS tweak, **Shift+right‑click**, or a **file manager** that still merges classic verbs. Link here for **engineers** only, not end‑user promise of dates.

## References (bookmark list)

- [IExplorerCommand (Windows)](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorercommand)
- [IExecuteCommand (Windows)](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexecutecommand) — what we use today; keep for other hosts
- [Support legacy context menus (MSIX) — package manifest](https://learn.microsoft.com/en-us/windows/msix/packaging-tool/support-legacy-context-menus) — related to classic vs new surface for **packaged** apps

- [Sign an app package](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool) — `SignTool`

## See also

- [explorer.md](explorer.md) — current `register-explorer` and `pm-image-execute.dll` behavior
- [integration.md](integration.md) — broader product integration notes (if you surface Explorer there)

When this work is **done**, replace or shorten this file with a short “done” section and point the main [explorer.md](explorer.md) at the **IExplorerCommand** + package flow as the **primary** Win11 story.
