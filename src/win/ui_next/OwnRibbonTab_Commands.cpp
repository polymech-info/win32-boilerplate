// Own ribbon custom command loader (`commands.json`).
#include "stdafx.h"

#ifdef FEATURE_USE_OWN_RIBBON

#ifndef FEATURE_CUSTOM_COMMANDS
#define FEATURE_CUSTOM_COMMANDS 0
#endif

#include "constants.hpp"
#include "OwnRibbonTab.h"
#include "Resource.h"
#include "RibbonUI.h"
#include "core/settings_runtime.hpp"
#include "helpers/text_conv.hpp"
#include "win/ribbon_commands.hpp"
#ifdef FEATURE_SVG_BUTTONS
#include "svg_paths.generated.h"
#endif

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

using own_ribbon::IconPresentation;
using own_ribbon::LayoutItem;
using json = nlohmann::json;

struct CustomRibbonState {
    std::deque<std::wstring> text;
    std::deque<std::vector<LayoutItem>> dropdowns;
    std::vector<own_ribbon::CustomRibbonCommand> commands;
    UINT nextCmdId = own_ribbon::kCustomRibbonCommandFirst;

    void clear()
    {
        text.clear();
        dropdowns.clear();
        commands.clear();
        nextCmdId = own_ribbon::kCustomRibbonCommandFirst;
    }

    const wchar_t* keep(std::wstring s)
    {
        text.push_back(std::move(s));
        return text.back().c_str();
    }

    UINT allocate()
    {
        if (nextCmdId > own_ribbon::kCustomRibbonCommandLast)
            return 0;
        return nextCmdId++;
    }
};

CustomRibbonState g_customRibbon;

LayoutItem Sep()
{
    LayoutItem s{};
    s.cmdId = 0;
    return s;
}

std::string trim_ascii(std::string s)
{
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

std::string strip_utf8_bom(std::string s)
{
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    return s;
}

std::string lower_compact(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return c == '_' || c == '-' || c == ' ';
    }), s.end());
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::wstring json_string_w(const json& o, const char* key, const wchar_t* fallback = L"")
{
    if (o.contains(key) && o[key].is_string())
        return pmui::utf8_to_wide(o[key].get<std::string>());
    return fallback ? std::wstring(fallback) : std::wstring();
}

std::string json_string(const json& o, const char* key)
{
    if (o.contains(key) && o[key].is_string())
        return o[key].get<std::string>();
    return {};
}

UINT ribbon_command_id_from_json(const json& o)
{
    const json* v = nullptr;
    if (o.contains("ribbonCommand"))
        v = &o["ribbonCommand"];
    else if (o.contains("commandId"))
        v = &o["commandId"];
    if (!v)
        return 0;
    if (v->is_number_unsigned())
        return v->get<UINT>();
    if (!v->is_string())
        return 0;
    if (const auto id = pm::cli::ribbon_command_id_from_name(v->get<std::string>()))
        return static_cast<UINT>(id);
    return 0;
}

#ifdef FEATURE_SVG_BUTTONS
struct CustomTablerFilledPaths {
    std::wstring chat, run, pause, resume, cancel, find, reset_layout, app_settings;

    CustomTablerFilledPaths()
    {
        namespace fs = std::filesystem;
        const fs::path b(PM_TABLER_FILLED_DIR_W);
        chat         = (b / "message-circle.svg").wstring();
        run          = (b / "player-play.svg").wstring();
        pause        = (b / "player-pause.svg").wstring();
        resume       = (b / "player-track-next.svg").wstring();
        cancel       = (b / "player-stop.svg").wstring();
        find         = (b / "search.svg").wstring();
        reset_layout = (b / "layout.svg").wstring();
        app_settings = (b / "settings.svg").wstring();
    }
};

static const CustomTablerFilledPaths g_customTabler;
#endif

void apply_default_custom_icon(LayoutItem& x, const std::string& iconName)
{
#ifdef FEATURE_SVG_BUTTONS
    const std::string icon = lower_compact(iconName);
    COLORREF tint = RGB(167, 139, 250);
    if (icon == "chat") {
        x.svgFilePathW = g_customTabler.chat.c_str();
        tint = RGB(6, 182, 212);
    } else if (icon == "run" || icon == "play") {
        x.svgFilePathW = g_customTabler.run.c_str();
        tint = RGB(34, 197, 94);
    } else if (icon == "pause") {
        x.svgFilePathW = g_customTabler.pause.c_str();
        tint = RGB(156, 163, 175);
    } else if (icon == "resume" || icon == "next") {
        x.svgFilePathW = g_customTabler.resume.c_str();
        tint = RGB(74, 222, 128);
    } else if (icon == "cancel" || icon == "stop") {
        x.svgFilePathW = g_customTabler.cancel.c_str();
        tint = RGB(248, 113, 113);
    } else if (icon == "find" || icon == "search") {
        x.svgFilePathW = g_customTabler.find.c_str();
        tint = RGB(20, 184, 166);
    } else if (icon == "settings") {
        x.svgFilePathW = g_customTabler.app_settings.c_str();
        tint = RGB(167, 139, 250);
    } else if (icon == "layout") {
        x.svgFilePathW = g_customTabler.reset_layout.c_str();
        tint = RGB(148, 163, 184);
    } else {
        x.svgFilePathW = g_customTabler.app_settings.c_str();
    }
    x.svgTint = tint;
#else
    (void)iconName;
    x.imageResId = IDC_CMD_APP_SETTINGS_LargeImages_RESID;
#endif
}

COLORREF parse_rgb_hex(const std::string& s, COLORREF fallback)
{
    if (s.size() != 7 || s[0] != '#')
        return fallback;
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    };
    int vals[6]{};
    for (size_t i = 1; i < s.size(); ++i) {
        vals[i - 1] = h(s[i]);
        if (vals[i - 1] < 0)
            return fallback;
    }
    return RGB(vals[0] * 16 + vals[1], vals[2] * 16 + vals[3], vals[4] * 16 + vals[5]);
}

std::wstring current_module_exe_path()
{
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    if (n == 0u)
        return {};
    return std::wstring(buf.data(), n);
}

std::wstring current_cli_exe_path()
{
    namespace fs = std::filesystem;
    const fs::path current = current_module_exe_path();
    if (current.empty())
        return {};
    const fs::path cli = current.parent_path() / L"pm-image-cli.exe";
    std::error_code ec;
    if (fs::is_regular_file(cli, ec) && !ec)
        return cli.wstring();
    return current.wstring();
}

void append_json_string_array_args(const json& o, const char* key, std::vector<std::wstring>& out)
{
    if (!o.contains(key) || !o[key].is_array())
        return;
    for (const auto& arg : o[key]) {
        if (arg.is_string())
            out.push_back(pmui::utf8_to_wide(arg.get<std::string>()));
    }
}

LayoutItem custom_item_from_json(const json& o, bool child)
{
    LayoutItem x{};
    x.cmdId = g_customRibbon.allocate();
    if (x.cmdId == 0)
        return x;
    x.icon = child ? IconPresentation::Small16 : IconPresentation::Large32;
    x.showLabel = !child;
    x.enabled = o.value("enabled", true);
    x.isToggle = o.value("toggle", false);
    const std::wstring label = json_string_w(o, "label", L"Custom");
    const std::wstring tooltip = json_string_w(o, "tooltip", label.c_str());
    x.label = g_customRibbon.keep(label);
    x.tooltip = g_customRibbon.keep(tooltip);
    apply_default_custom_icon(x, json_string(o, "icon"));
#ifdef FEATURE_SVG_BUTTONS
    if (o.contains("svg") && o["svg"].is_string())
        x.svgFilePathW = g_customRibbon.keep(pmui::utf8_to_wide(o["svg"].get<std::string>()));
    if (o.contains("tint") && o["tint"].is_string())
        x.svgTint = parse_rgb_hex(o["tint"].get<std::string>(), x.svgTint);
#endif

    own_ribbon::CustomRibbonCommand c{};
    c.cmdId = x.cmdId;
    c.enabled = x.enabled;
    c.id = json_string(o, "id");
    c.appCommand = json_string(o, "appCommand");
    if (c.appCommand.empty())
        c.appCommand = json_string(o, "command");
    c.cliCommand = json_string(o, "cliCommand");
    c.targetRibbonCommandId = ribbon_command_id_from_json(o);
    if (o.contains("runOptions") && o["runOptions"].is_object()) {
        c.runNewShellWindow = o["runOptions"].value("newShellWindow", false);
        c.closeOnExit = o["runOptions"].value("closeOnExit", false);
    }
    json userData = o.contains("userData") ? o["userData"] : json::object();
    if (!userData.is_object())
        userData = json{{"value", userData}};
    if (o.contains("source"))
        userData["source"] = o["source"];
    if (o.contains("output"))
        userData["output"] = o["output"];
    if (o.contains("externalCommand"))
        userData["externalCommand"] = o["externalCommand"];
    if (o.contains("cliCommand"))
        userData["cliCommand"] = o["cliCommand"];
    if (o.contains("args"))
        userData["args"] = o["args"];
    if (o.contains("cwd"))
        userData["cwd"] = o["cwd"];
    if (o.contains("logLevel"))
        userData["logLevel"] = o["logLevel"];
    if (o.contains("runOptions"))
        userData["runOptions"] = o["runOptions"];
    if (!userData.empty())
        c.userDataJson = userData.dump();
    if (o.contains("externalCommand") && o["externalCommand"].is_object()) {
        const json& ext = o["externalCommand"];
        c.externalShellMode = json_string(ext, "mode") == "shell";
        c.externalShellLine = json_string_w(ext, "shellLine", L"");
        c.externalCommand = json_string_w(ext, "command", L"");
        c.externalCwd = json_string_w(ext, "cwd", L"");
        if (ext.contains("args") && ext["args"].is_array()) {
            for (const auto& arg : ext["args"]) {
                if (arg.is_string())
                    c.externalArgs.push_back(pmui::utf8_to_wide(arg.get<std::string>()));
            }
        }
    }
    if (!c.cliCommand.empty() && c.externalCommand.empty()) {
        c.externalCommand = current_cli_exe_path();
        append_json_string_array_args(o, "globalArgs", c.externalArgs);
        const std::wstring cwd = json_string_w(o, "cwd", L"");
        if (!cwd.empty() && cwd != L".") {
            c.externalCwd = cwd;
            c.externalArgs.push_back(L"--cwd");
            c.externalArgs.push_back(cwd);
        }
        const std::wstring logLevel = json_string_w(o, "logLevel", L"");
        if (!logLevel.empty() && logLevel != L"info") {
            c.externalArgs.push_back(L"--log-level");
            c.externalArgs.push_back(logLevel);
        }
        c.externalArgs.push_back(pmui::utf8_to_wide(c.cliCommand));
        append_json_string_array_args(o, "args", c.externalArgs);
    }
    c.openUrl = json_string_w(o, "url", L"");
    c.openPath = json_string_w(o, "path", L"");
    g_customRibbon.commands.push_back(std::move(c));
    return x;
}

void append_custom_items_from_json(const json& items, std::vector<LayoutItem>& out, bool child)
{
    if (!items.is_array())
        return;
    for (const auto& item : items) {
        if (!item.is_object())
            continue;
        if (!item.value("visible", true))
            continue;
        const std::string type = lower_compact(item.value("type", "button"));
        if (type == "separator" || type == "sep") {
            out.push_back(Sep());
            continue;
        }
        LayoutItem x = custom_item_from_json(item, child);
        if (x.cmdId == 0)
            continue;
        if ((type == "dropdown" || item.contains("items")) && item.contains("items") && item["items"].is_array()) {
            g_customRibbon.dropdowns.emplace_back();
            append_custom_items_from_json(item["items"], g_customRibbon.dropdowns.back(), true);
            x.isDropdown = true;
            x.dropdownItems = &g_customRibbon.dropdowns.back();
        }
        out.push_back(x);
    }
}

} // namespace

namespace own_ribbon {

void AppendCustomRibbonLayout(std::vector<LayoutItem>& out)
{
#if FEATURE_CUSTOM_COMMANDS
    g_customRibbon.clear();
    std::string raw;
    std::string err;
    if (!media::runtime_settings::load_command_json_utf8(raw, err) || raw.empty())
        return;
    try {
        const json root = json::parse(trim_ascii(strip_utf8_bom(std::move(raw))));
        const json* groups = nullptr;
        if (root.contains("ribbon") && root["ribbon"].is_object() && root["ribbon"].contains("groups"))
            groups = &root["ribbon"]["groups"];
        else if (root.contains("groups"))
            groups = &root["groups"];
        else if (root.is_array())
            groups = &root;
        if (!groups || !groups->is_array())
            return;
        bool appendedAny = false;
        for (const auto& group : *groups) {
            const json* items = nullptr;
            if (group.is_object() && group.contains("items"))
                items = &group["items"];
            else if (group.is_array())
                items = &group;
            if (!items || !items->is_array() || items->empty())
                continue;
            if (appendedAny || !out.empty())
                out.push_back(Sep());
            const size_t before = out.size();
            append_custom_items_from_json(*items, out, false);
            appendedAny = appendedAny || out.size() > before;
        }
    } catch (const std::exception& e) {
        (void)::OutputDebugStringW((L"[pm-image] commands.json ignored: " + pmui::utf8_to_wide(e.what()) + L"\n").c_str());
        g_customRibbon.clear();
    }
#else
    (void)out;
    g_customRibbon.clear();
#endif
}

const CustomRibbonCommand* FindCustomRibbonCommand(UINT cmdId)
{
    for (const auto& cmd : g_customRibbon.commands) {
        if (cmd.cmdId == cmdId)
            return &cmd;
    }
    return nullptr;
}

bool IsCustomRibbonCommandId(UINT cmdId)
{
    return cmdId >= kCustomRibbonCommandFirst && cmdId <= kCustomRibbonCommandLast && FindCustomRibbonCommand(cmdId) != nullptr;
}

} // namespace own_ribbon

#endif // FEATURE_USE_OWN_RIBBON
