# Single source for product identity: consumed by configure_file (src/pm_branding_config.hpp.in)
# and by CMake target / output names. Win11 sparse Appx package template: branding/AppxManifest.xml.in.
# Change values here, then re-configure the build.
#
# Technical basenames (exe/dll) intentionally keep the historic "pm-image" / "pm-image-execute" names
# for COM registration, ProgId, and shell defaults unless you rebrand the filesystem artifacts too.

if(DEFINED _PM_BRANDING_CMAKE_LOADED)
  return()
endif()
set(_PM_BRANDING_CMAKE_LOADED 1)

# ── Human-facing (registry UI, MUI, About) ─────────────────────────────────
set(PM_BRAND_VENDOR "PolyMech")
set(PM_BRAND_APP_DISPLAY "PM-Image")
# Windows Capabilities / Default Programs: short description
set(PM_BRAND_APP_DESCRIPTION "PolyMech PM-Image")
# "Open in …" shell verb (Unicode ellipsis … U+2026)
set(PM_BRAND_VERB_OPEN_IN "Open in Workbench…")

# WebView2 virtual hosts (SetVirtualHostNameToFolderMapping):
# Hostnames must be valid for URL parsing (no empty DNS labels — avoid "m.." / "c.." or fetch() gets
# net::ERR_FILE_NOT_FOUND). Use the reserved ".invalid" TLD (RFC 6761): NXDOMAIN is typically fast and
# avoids the multi-second stalls some setups see with "*.local" before the folder mapping applies.
set(PM_CHAT_WEB_VHOST "pm-chat.invalid")
set(PM_VIEWER_WEB_VHOST "pm-vw.invalid")
set(PM_MARKDOWN_ASSETS_VHOST "pm-md.invalid")

# ── Technical slugs (file stems, ProgId suffix) ─────────────────────────────
set(PM_APP_ID_SLUG "pm-image")
# DLL stem next to the exe (no .dll); output is ${PM_IEXECUTE_BASENAME}.dll
set(PM_IEXECUTE_BASENAME "pm-image-execute")
# Win11 File Explorer (IExplorerCommand + sparse/MSIX manifest) — see docs/win11.md, branding/AppxManifest.xml.in
set(PM_EXPLORER11_BASENAME "pm-image-explorer11")
set(PM_EXPLORER11_STUB_BASENAME "pm-image-explorer11-stub")
# ProgId for "Open with" = vendor.slug
set(PM_OPEN_WITH_PROGID "${PM_BRAND_VENDOR}.${PM_APP_ID_SLUG}")
# Second ProgId: same exe, viewer workbench (`--ui-preset=viewer`); see register_open_with_for_user
set(PM_OPEN_WITH_VIEWER_PROGID "${PM_BRAND_VENDOR}.${PM_APP_ID_SLUG}.viewer")
set(PM_BRAND_OPEN_WITH_VIEWER_DISPLAY "PM Viewer")

# CMake logical target names (must stay valid as CMake identifiers / target names)
set(MEDIA_TARGET_PM_IMAGE "${PM_APP_ID_SLUG}")
set(MEDIA_TARGET_IEXECUTE "${PM_IEXECUTE_BASENAME}")
# Hyphenated target for Explorer11 (matches pm-image-execute)
set(MEDIA_TARGET_EXPLORER11 "pm-image-explorer11")
set(MEDIA_TARGET_EXPLORER11_STUB "pm-image-explorer11-stub")

# Dist EXE (same stem as target; RUNTIME matches target name on Windows)
set(PM_CMAKE_OUTPUT_NAME "${PM_APP_ID_SLUG}")

# Byte-for-byte copy of the main EXE at build time (`${PM_VIEWER_EXE_STEM}.exe` next to `${PM_APP_ID_SLUG}.exe`).
# Windows “Open with” often dedupes by Applications\*.exe identity; a second filename yields a second row (PM Viewer).
set(PM_VIEWER_EXE_STEM "pm-viewer")

message(STATUS "Branding: ${PM_BRAND_APP_DISPLAY} (vendor ${PM_BRAND_VENDOR}; id ${PM_APP_ID_SLUG}; IExecute ${MEDIA_TARGET_IEXECUTE}; Win11 IExplorer ${MEDIA_TARGET_EXPLORER11}; viewer dup ${PM_VIEWER_EXE_STEM}.exe)")

