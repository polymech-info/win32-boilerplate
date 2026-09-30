#pragma once
//
// Win32 window introspection for debug.json — ideas adapted from WinSpy++’s style/class
// panes (ref/winspy/src/DisplayClassInfo.c, DisplayStyleInfo.c; MIT licence in ref/winspy/LICENCE.TXT).
//
#include <Windows.h>

#include <cstdint>

#include <nlohmann/json.hpp>
#include <string>

#include "theme.hpp" // ThemePalette + `IUIFramework` forward declaration (ribbon)

namespace pmui {

/// e.g. "#32770" → "#32770 (Dialog)" (same mapping as WinSpy VerboseClassName).
std::string verbose_class_name_utf8(const wchar_t* class_name);

/// Human-readable names for bits set in GWL_STYLE / GWL_EXSTYLE (subset used for dock/WebView diagnosis).
nlohmann::json style_bits_gwl_style(DWORD style);
nlohmann::json style_bits_gwl_exstyle(DWORD exstyle);

nlohmann::json colorref_json(COLORREF c);

/// Full WinSpy-style node for one HWND (rects, styles, DWM bits, parent/owner).
nlohmann::json hwnd_snapshot_json(HWND h);

/// Depth-limited child tree (breadth per node) for embedded hosts (WebView2, ribbon).
nlohmann::json hwnd_descendants_json(HWND root, int max_depth);

/// Read `UI_PKEY_Global*` on Quick Access Toolbar command id 701 (`cmdQAT`).
nlohmann::json ribbon_framework_debug_json(IUIFramework* fw);

/// Values `apply_ribbon_global_tint` derives from @p pal (packed + H/S/B fields).
nlohmann::json ribbon_global_tint_debug_json(const ThemePalette& pal);

} // namespace pmui
