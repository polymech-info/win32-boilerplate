// See win_ui_debug.hpp for WinSpy++ reference.
#include "win_ui_debug.hpp"

#include "helpers/text_conv.hpp"

#include <UIRibbon.h>
#include <UIRibbonPropertyHelpers.h>

#include <cstdio>

#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

namespace pmui {

// `detail::append_child_tree` recurses before this definition appears later in this TU.
nlohmann::json hwnd_snapshot_json(HWND h);

namespace detail {

struct FlagName {
    DWORD       mask;
    const char* name;
};

static const FlagName kGwlStyle[] = {
    { WS_POPUP,         "WS_POPUP" },
    { WS_CHILD,         "WS_CHILD" },
    { WS_MINIMIZE,      "WS_MINIMIZE" },
    { WS_VISIBLE,       "WS_VISIBLE" },
    { WS_DISABLED,      "WS_DISABLED" },
    { WS_CLIPSIBLINGS,  "WS_CLIPSIBLINGS" },
    { WS_CLIPCHILDREN,  "WS_CLIPCHILDREN" },
    { WS_MAXIMIZE,      "WS_MAXIMIZE" },
    { WS_BORDER,        "WS_BORDER" },
    { WS_DLGFRAME,      "WS_DLGFRAME" },
    { WS_VSCROLL,       "WS_VSCROLL" },
    { WS_HSCROLL,       "WS_HSCROLL" },
    { WS_SYSMENU,       "WS_SYSMENU" },
    { WS_THICKFRAME,    "WS_THICKFRAME" },
    { WS_MINIMIZEBOX,   "WS_MINIMIZEBOX" },
    { WS_MAXIMIZEBOX,   "WS_MAXIMIZEBOX" },
    { WS_GROUP,         "WS_GROUP" },
    { WS_TABSTOP,       "WS_TABSTOP" },
};

static const FlagName kGwlExStyle[] = {
    { WS_EX_DLGMODALFRAME,  "WS_EX_DLGMODALFRAME" },
    { WS_EX_NOPARENTNOTIFY,   "WS_EX_NOPARENTNOTIFY" },
    { WS_EX_TOPMOST,        "WS_EX_TOPMOST" },
    { WS_EX_ACCEPTFILES,    "WS_EX_ACCEPTFILES" },
    { WS_EX_TRANSPARENT,    "WS_EX_TRANSPARENT" },
    { WS_EX_MDICHILD,       "WS_EX_MDICHILD" },
    { WS_EX_TOOLWINDOW,     "WS_EX_TOOLWINDOW" },
    { WS_EX_WINDOWEDGE,     "WS_EX_WINDOWEDGE" },
    { WS_EX_CLIENTEDGE,     "WS_EX_CLIENTEDGE" },
    { WS_EX_CONTEXTHELP,    "WS_EX_CONTEXTHELP" },
    { WS_EX_RIGHT,          "WS_EX_RIGHT" },
    { WS_EX_LEFT,           "WS_EX_LEFT" },
    { WS_EX_RTLREADING,     "WS_EX_RTLREADING" },
    { WS_EX_LTRREADING,     "WS_EX_LTRREADING" },
    { WS_EX_LEFTSCROLLBAR,  "WS_EX_LEFTSCROLLBAR" },
    { WS_EX_RIGHTSCROLLBAR, "WS_EX_RIGHTSCROLLBAR" },
    { WS_EX_CONTROLPARENT,  "WS_EX_CONTROLPARENT" },
    { WS_EX_STATICEDGE,     "WS_EX_STATICEDGE" },
    { WS_EX_APPWINDOW,      "WS_EX_APPWINDOW" },
    { WS_EX_LAYERED,        "WS_EX_LAYERED" },
    { WS_EX_NOINHERITLAYOUT,"WS_EX_NOINHERITLAYOUT" },
    { WS_EX_LAYOUTRTL,      "WS_EX_LAYOUTRTL" },
    { WS_EX_COMPOSITED,     "WS_EX_COMPOSITED" },
    { WS_EX_NOACTIVATE,     "WS_EX_NOACTIVATE" },
};

nlohmann::json collect_bits(DWORD value, const FlagName* table, size_t n)
{
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < n; ++i) {
        if ((value & table[i].mask) != 0)
            arr.push_back(table[i].name);
    }
    return arr;
}

// QAT command id (RibbonUI.h; own-ribbon tab strip)
constexpr UINT kRibbonQatCommandId = 701;

nlohmann::json unpack_ui_hsbcolor(uint32_t packed)
{
    const int h = static_cast<int>(packed & 0xFFu);
    const int s = static_cast<int>((packed >> 8) & 0xFFu);
    const int b = static_cast<int>((packed >> 16) & 0xFFu);
    char hex[16]{};
    std::snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(packed));
    return nlohmann::json{
        {"packed_hex", std::string(hex)},
        {"packed", packed},
        {"h", h},
        {"s", s},
        {"b", b},
    };
}

nlohmann::json ribbon_property_read(IUIFramework* fw, REFPROPERTYKEY key, const char* label)
{
    nlohmann::json o;
    o["key"] = label;
    if (!fw) {
        o["error"] = "null_framework";
        return o;
    }
    PROPVARIANT pv{};
    const HRESULT hr =
        fw->GetUICommandProperty(kRibbonQatCommandId, key, &pv);
    o["hr"] = static_cast<int>(hr);
    if (SUCCEEDED(hr) && pv.vt == VT_UI4) {
        o.update(unpack_ui_hsbcolor(static_cast<uint32_t>(pv.ulVal)));
    } else {
        o["vt"] = static_cast<int>(pv.vt);
    }
    ::PropVariantClear(&pv);
    return o;
}

void append_child_tree(HWND parent, nlohmann::json& out, int depth, int max_depth)
{
    if (depth > max_depth)
        return;

    struct Pack {
        nlohmann::json* arr;
        int             depth;
        int             max_depth;
    } pack{};
    pack.arr       = &out;
    pack.depth     = depth;
    pack.max_depth = max_depth;

    ::EnumChildWindows(
        parent,
        [](HWND ch, LPARAM lp) -> BOOL {
            auto* p = reinterpret_cast<Pack*>(lp);
            nlohmann::json node = hwnd_snapshot_json(ch);
            node["tree_depth"] = p->depth;
            if (p->depth < p->max_depth) {
                nlohmann::json kids = nlohmann::json::array();
                append_child_tree(ch, kids, p->depth + 1, p->max_depth);
                if (!kids.empty())
                    node["children"] = std::move(kids);
            }
            p->arr->push_back(std::move(node));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pack));
}

} // namespace detail

std::string verbose_class_name_utf8(const wchar_t* class_name)
{
    if (!class_name || !class_name[0])
        return {};
    std::wstring w(class_name);
    if (_wcsicmp(class_name, L"#32770") == 0)
        w += L" (Dialog)";
    else if (_wcsicmp(class_name, L"#32768") == 0)
        w += L" (Menu)";
    else if (_wcsicmp(class_name, L"#32769") == 0)
        w += L" (Desktop window)";
    else if (_wcsicmp(class_name, L"#32771") == 0)
        w += L" (Task-switch window)";
    else if (_wcsicmp(class_name, L"#32772") == 0)
        w += L" (Icon title)";
    return pmui::wide_to_utf8(w);
}

nlohmann::json style_bits_gwl_style(DWORD style)
{
    return detail::collect_bits(style, detail::kGwlStyle, sizeof(detail::kGwlStyle) / sizeof(detail::kGwlStyle[0]));
}

nlohmann::json style_bits_gwl_exstyle(DWORD exstyle)
{
    return detail::collect_bits(exstyle, detail::kGwlExStyle, sizeof(detail::kGwlExStyle) / sizeof(detail::kGwlExStyle[0]));
}

nlohmann::json colorref_json(COLORREF c)
{
    char hex[16]{};
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return nlohmann::json{
        {"colorref", static_cast<uint32_t>(c)},
        {"hex",     std::string(hex)},
    };
}

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

nlohmann::json hwnd_snapshot_json(HWND h)
{
    nlohmann::json j;
    if (!h || !::IsWindow(h)) {
        j["valid"] = false;
        return j;
    }
    j["valid"]     = true;
    j["hwnd"]      = reinterpret_cast<uint64_t>(h);
    wchar_t cls[256]{};
    ::GetClassNameW(h, cls, 256);
    j["class"] = pmui::wide_to_utf8(cls);
    j["class_verbose"] = pmui::verbose_class_name_utf8(cls);
    wchar_t title[512]{};
    if (::GetWindowTextW(h, title, static_cast<int>(std::size(title))) > 0)
        j["title"] = pmui::wide_to_utf8(title);

    RECT wr{};
    ::GetWindowRect(h, &wr);
    j["window_rect_screen"] = { {"left", wr.left}, {"top", wr.top}, {"right", wr.right}, {"bottom", wr.bottom} };

    RECT cr{};
    ::GetClientRect(h, &cr);
    POINT tl{0, 0};
    ::ClientToScreen(h, &tl);
    j["client_size"]        = { {"w", cr.right - cr.left}, {"h", cr.bottom - cr.top} };
    j["client_origin_scr"]  = { {"x", tl.x}, {"y", tl.y} };

    const LONG_PTR st  = ::GetWindowLongPtrW(h, GWL_STYLE);
    const LONG_PTR ex  = ::GetWindowLongPtrW(h, GWL_EXSTYLE);
    j["style"]         = static_cast<uint64_t>(static_cast<ULONG_PTR>(st));
    j["ex_style"]      = static_cast<uint64_t>(static_cast<ULONG_PTR>(ex));
    j["style_bits"]    = pmui::style_bits_gwl_style(static_cast<DWORD>(st));
    j["ex_style_bits"] = pmui::style_bits_gwl_exstyle(static_cast<DWORD>(ex));
    j["has_ws_child"]  = (st & WS_CHILD) != 0;
    {
        const HWND parent = ::GetParent(h);
        j["parent_hwnd"]   = parent ? reinterpret_cast<uint64_t>(parent) : 0;
        const HWND owner   = ::GetWindow(h, GW_OWNER);
        j["owner_hwnd"]    = owner ? reinterpret_cast<uint64_t>(owner) : 0;
    }
    j["dpi"]           = static_cast<int>(::GetDpiForWindow(h));
    j["visible"]       = ::IsWindowVisible(h) ? true : false;

    COLORREF border{};
    if (SUCCEEDED(::DwmGetWindowAttribute(h, DWMWA_BORDER_COLOR, &border, sizeof(border)))) {
        j["dwm_border_color"] = colorref_json(border);
    }
    DWORD corner = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)))) {
        j["dwm_corner_pref"] = corner;
    }
    return j;
}

nlohmann::json hwnd_descendants_json(HWND root, int max_depth)
{
    nlohmann::json arr = nlohmann::json::array();
    if (!root || !::IsWindow(root))
        return arr;
    detail::append_child_tree(root, arr, 0, max_depth);
    return arr;
}

nlohmann::json ribbon_framework_debug_json(IUIFramework* fw)
{
    nlohmann::json j;
    j["qat_command_id"] = detail::kRibbonQatCommandId;
    if (!fw) {
        j["iui_framework"] = nullptr;
        return j;
    }
    j["iui_framework_non_null"] = true;
    j["read_back"] = {
        {"global_background", detail::ribbon_property_read(fw, UI_PKEY_GlobalBackgroundColor, "GlobalBackgroundColor")},
        {"global_highlight",  detail::ribbon_property_read(fw, UI_PKEY_GlobalHighlightColor, "GlobalHighlightColor")},
        {"global_text",       detail::ribbon_property_read(fw, UI_PKEY_GlobalTextColor, "GlobalTextColor")},
    };
    return j;
}

nlohmann::json ribbon_global_tint_debug_json(const ThemePalette& pal)
{
    const RibbonGlobalTint t = ribbon_global_tint_from_palette(pal);
    return nlohmann::json{
        {"resolved_from_palette", true},
        {"dark_palette", pal.dark},
        {"background", detail::unpack_ui_hsbcolor(t.background)},
        {"highlight",  detail::unpack_ui_hsbcolor(t.highlight)},
        {"text",       detail::unpack_ui_hsbcolor(t.text)},
        {"source_colors", {
            {"caption_bg",  colorref_json(pal.caption_bg)},
            {"control_bg",  colorref_json(pal.control_bg)},
            {"window_fg",   colorref_json(pal.window_fg)},
        }},
    };
}

} // namespace pmui
