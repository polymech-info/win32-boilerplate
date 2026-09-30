#include "LayoutStore.hpp"
#include "settings_store.hpp"
#if !defined(PM_IMAGE_CLI_ONLY)
#include "ui_next/ui_log_file.hpp"
#endif

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace media::layout {
namespace {

// Last value from `LoadWindowLayout` (default true before first load).
std::atomic<bool> g_defer_dock_container_load{true};

// Layout tracing helpers - use pmui::ui_log_file_event with [layout] prefix for easy filtering
void layout_trace(const char* msg)
{
#if !defined(PM_IMAGE_CLI_ONLY)
    pmui::ui_log_file_event((std::string("[layout] ") + msg).c_str());
#else
    (void)msg;
#endif
}
void layout_tracef(const char* fmt, ...)
{
#if !defined(PM_IMAGE_CLI_ONLY)
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    pmui::ui_log_file_event((std::string("[layout] ") + buf).c_str());
#else
    (void)fmt;
#endif
}

const char* workbench_slot_or_main(const char* s)
{
    return (s && s[0]) ? s : "main";
}

const json* window_subtree_read(const json& j, const char* workbench_slot)
{
    const char* slot = workbench_slot_or_main(workbench_slot);
    if (j.contains("workbench") && j["workbench"].is_object() && j["workbench"].contains(slot)
        && j["workbench"][slot].is_object() && j["workbench"][slot].contains("window")
        && j["workbench"][slot]["window"].is_object())
        return &j["workbench"][slot]["window"];
    if (std::strcmp(slot, "main") == 0 && j.contains("window") && j["window"].is_object())
        return &j["window"];
    return nullptr;
}

/// Ensures @c j["workbench"][slot] is an object; returns a reference to it.
json& ensure_workbench_slot(json& j, const char* workbench_slot)
{
    const char* slot = workbench_slot_or_main(workbench_slot);
    if (!j.contains("workbench") || !j["workbench"].is_object())
        j["workbench"] = json::object();
    if (!j["workbench"].contains(slot) || !j["workbench"][slot].is_object())
        j["workbench"][slot] = json::object();
    return j["workbench"][slot];
}

/** Reject degenerate / hand-edited rects that break `SetWindowPlacement` or monitor math. */
bool normal_rect_is_usable_for_placement(const RECT& r)
{
    const LONG w = r.right - r.left;
    const LONG h = r.bottom - r.top;
    if (w < 64 || h < 64)
        return false;
    if (w > 20000 || h > 20000)
        return false;
    return true;
}

void clamp_show_cmd_for_window_placement(int& show_cmd)
{
    if (show_cmd == 0)
        show_cmd = SW_SHOWNORMAL;
    if (show_cmd < 0 || show_cmd > 11)
        show_cmd = SW_SHOWNORMAL;
}

bool json_bool_any(const json& j, std::initializer_list<const char*> keys, bool fallback)
{
    for (const char* key : keys) {
        if (j.contains(key) && j[key].is_boolean())
            return j[key].get<bool>();
    }
    return fallback;
}

void read_path_array(const json& arr, std::vector<std::string>& dest)
{
    if (!arr.is_array())
        return;
    for (const auto& el : arr) {
        if (!el.is_string())
            continue;
        std::string s = el.get<std::string>();
        if (s.empty() || dest.size() >= 15)
            continue;
        dest.push_back(std::move(s));
    }
}

void try_apply_workbench_window_defaults_to_layout(WindowLayout& out, const char* workbench_slot)
{
    if (out.has_placement)
        return;
    std::string err;
    WorkbenchWindowDefaults def;
    if (!LayoutStore::LoadWorkbenchWindowDefaults(def, err, workbench_slot) || !def.has_size()) {
        const char* slot = workbench_slot_or_main(workbench_slot);
        if (std::strcmp(slot, "chat") == 0 || std::strcmp(slot, "viewer") == 0) {
            const UINT sysDpi = ::GetDpiForSystem();
            if (std::strcmp(slot, "chat") == 0) {
                def.width  = ::MulDiv(640, (int)sysDpi, 96);
                def.height = ::MulDiv(720, (int)sysDpi, 96);
            } else {
                def.width  = ::MulDiv(960, (int)sysDpi, 96);
                def.height = ::MulDiv(720, (int)sysDpi, 96);
            }
        } else
            return;
    }

    HMONITOR hMon = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!hMon)
        return;
    MONITORINFO mi{sizeof(mi)};
    if (!::GetMonitorInfoW(hMon, &mi))
        return;
    const RECT& wa = mi.rcWork;
    const int   aw = (int)(wa.right - wa.left);
    const int   ah = (int)(wa.bottom - wa.top);
    if (aw < 32 || ah < 32)
        return;
    int w = std::min(def.width, aw);
    int h = std::min(def.height, ah);
    w     = std::max(w, 200);
    h     = std::max(h, 200);
    w     = std::min(w, aw);
    h     = std::min(h, ah);
    const int x       = wa.left + (aw - w) / 2;
    const int y       = wa.top + (ah - h) / 2;
    out.has_placement = true;
    out.show_cmd      = SW_SHOWNORMAL;
    out.normal_rect.left   = x;
    out.normal_rect.top    = y;
    out.normal_rect.right  = x + w;
    out.normal_rect.bottom = y + h;
    out.min_pos = {-1, -1};
    out.max_pos = {-1, -1};
}

void write_window_layout_fields(json& w, const WindowLayout& layout)
{
    if (!w.is_object())
        w = json::object();

    w["has_placement"] = layout.has_placement;
    w["show_cmd"]      = layout.show_cmd;
    w["min_pos"]       = json::array({layout.min_pos.x, layout.min_pos.y});
    w["max_pos"]       = json::array({layout.max_pos.x, layout.max_pos.y});
    w["normal_rect"]   = json::array({
        layout.normal_rect.left, layout.normal_rect.top,
        layout.normal_rect.right, layout.normal_rect.bottom});

    w["has_panel_visibility"] = layout.has_panel_visibility;
    if (layout.has_panel_visibility) {
        auto& p       = w["panels"];
        p["queue"]    = layout.pv_queue;
        p["log"]      = layout.pv_log;
        p["settings"] = layout.pv_settings;
        p["fileinfo"] = layout.pv_fileinfo;
        p["filetree"] = layout.pv_filetree;
        p["nodes"]    = layout.pv_nodes;
        p["findresults"] = layout.pv_findresults;
        p["dupresults"]  = layout.pv_dupresults;
        p["chat"]        = layout.pv_chat;
        p["console"]     = layout.pv_console;
    } else if (w.contains("panels")) {
        w.erase("panels");
    }

    if (!layout.filetree_folder.empty())
        w["filetree_folder"] = layout.filetree_folder;
    else if (w.contains("filetree_folder"))
        w.erase("filetree_folder");

    w["filetree_show_shell_frames"] = layout.filetree_show_shell_frames;
    w["filetree_filter_mask"]       = layout.filetree_filter_mask;
    w["center_view_tabbed"]         = layout.center_view_tabbed;

    w["filetree_view_mode"] = layout.filetree_view_mode;
    w["filetree_icon_size"] = layout.filetree_icon_size;

    if (!layout.filetree_columns.empty()) {
        json cols = json::array();
        for (const auto& c : layout.filetree_columns) {
            json col = json::object();
            col["fmtid"] = c.fmtid_str;
            col["pid"]   = c.pid;
            col["width"] = c.width;
            cols.push_back(std::move(col));
        }
        w["filetree_columns"] = std::move(cols);
    } else if (w.contains("filetree_columns")) {
        w.erase("filetree_columns");
    }

    w["defer_dock_containers"]      = layout.defer_dock_containers;

    if (!layout.recent_files.empty()) {
        json arr = json::array();
        for (const auto& s : layout.recent_files)
            arr.push_back(s);
        w["recent_files"] = std::move(arr);
    } else if (w.contains("recent_files")) {
        w.erase("recent_files");
    }

    if (!layout.recent_folders.empty()) {
        json arr = json::array();
        for (const auto& s : layout.recent_folders)
            arr.push_back(s);
        w["recent_folders"] = std::move(arr);
    } else if (w.contains("recent_folders")) {
        w.erase("recent_folders");
    }
}

} // namespace

bool LayoutStore::DeferDockContainerLoad()
{
    return g_defer_dock_container_load.load(std::memory_order_relaxed);
}

bool LayoutStore::ParseWindowLayout(const json& w, WindowLayout& out, std::string& err)
{
    layout_trace("ParseWindowLayout: enter");
    out = WindowLayout{};
    err.clear();
    if (!w.is_object()) {
        err = "window layout is not an object";
        layout_trace("ParseWindowLayout: FAILED - window layout is not an object");
        return false;
    }

    try {
        out.show_cmd = w.value("show_cmd", SW_SHOWNORMAL);
        clamp_show_cmd_for_window_placement(out.show_cmd);

        if (w.contains("min_pos") && w["min_pos"].is_array() && w["min_pos"].size() >= 2) {
            out.min_pos.x = (LONG)w["min_pos"][0].get<int>();
            out.min_pos.y = (LONG)w["min_pos"][1].get<int>();
        }
        if (w.contains("max_pos") && w["max_pos"].is_array() && w["max_pos"].size() >= 2) {
            out.max_pos.x = (LONG)w["max_pos"][0].get<int>();
            out.max_pos.y = (LONG)w["max_pos"][1].get<int>();
        }
        if (w.contains("normal_rect") && w["normal_rect"].is_array() && w["normal_rect"].size() >= 4) {
            out.normal_rect.left   = (LONG)w["normal_rect"][0].get<int>();
            out.normal_rect.top    = (LONG)w["normal_rect"][1].get<int>();
            out.normal_rect.right  = (LONG)w["normal_rect"][2].get<int>();
            out.normal_rect.bottom = (LONG)w["normal_rect"][3].get<int>();

            const bool wants_placement = !w.contains("has_placement")
                || !w["has_placement"].is_boolean() || w["has_placement"].get<bool>();
            out.has_placement = wants_placement && normal_rect_is_usable_for_placement(out.normal_rect);
        }

        if (w.contains("panels") && w["panels"].is_object()) {
            out.has_panel_visibility = true;
            const auto& p            = w["panels"];
            out.pv_queue             = json_bool_any(p, {"queue"}, true);
            out.pv_log               = json_bool_any(p, {"log"}, true);
            out.pv_settings          = json_bool_any(p, {"settings"}, true);
            out.pv_fileinfo          = json_bool_any(p, {"fileinfo"}, true);
            out.pv_filetree          = json_bool_any(p, {"filetree"}, true);
            out.pv_nodes             = json_bool_any(p, {"nodes"}, false);
            out.pv_findresults       = json_bool_any(p, {"findresults", "find_results"}, false);
            out.pv_dupresults        = json_bool_any(p, {"dupresults", "duplicate_results"}, false);
            out.pv_chat              = json_bool_any(p, {"chat"}, false);
            out.pv_console           = json_bool_any(p, {"console"}, false);
        } else {
            out.has_panel_visibility = json_bool_any(w, {"has_panel_visibility"}, false);
            out.pv_queue             = json_bool_any(w, {"pv_queue", "queue"}, true);
            out.pv_log               = json_bool_any(w, {"pv_log", "log"}, true);
            out.pv_settings          = json_bool_any(w, {"pv_settings", "settings"}, true);
            out.pv_fileinfo          = json_bool_any(w, {"pv_fileinfo", "fileinfo"}, true);
            out.pv_filetree          = json_bool_any(w, {"pv_filetree", "filetree"}, true);
            out.pv_nodes             = json_bool_any(w, {"pv_nodes", "nodes"}, false);
            out.pv_findresults       = json_bool_any(w, {"pv_findresults", "findresults", "find_results"}, false);
            out.pv_dupresults        = json_bool_any(w, {"pv_dupresults", "dupresults", "duplicate_results"}, false);
            out.pv_chat              = json_bool_any(w, {"pv_chat", "chat"}, false);
            out.pv_console           = json_bool_any(w, {"pv_console", "console"}, false);
        }

        if (w.contains("filetree_folder") && w["filetree_folder"].is_string()) {
            out.filetree_folder = w["filetree_folder"].get<std::string>();
            if (out.filetree_folder.size() > 32000u)
                out.filetree_folder.clear();
        }
        if (w.contains("filetree_show_shell_frames") && w["filetree_show_shell_frames"].is_boolean())
            out.filetree_show_shell_frames = w["filetree_show_shell_frames"].get<bool>();
        if (w.contains("center_view_tabbed") && w["center_view_tabbed"].is_boolean())
            out.center_view_tabbed = w["center_view_tabbed"].get<bool>();
        if (w.contains("filetree_filter_mask") && w["filetree_filter_mask"].is_string()) {
            out.filetree_filter_mask = w["filetree_filter_mask"].get<std::string>();
            if (out.filetree_filter_mask.size() > 4096u)
                out.filetree_filter_mask = "*.*";
        }

        if (w.contains("filetree_view_mode") && w["filetree_view_mode"].is_number_integer())
            out.filetree_view_mode = w["filetree_view_mode"].get<int>();
        if (w.contains("filetree_icon_size") && w["filetree_icon_size"].is_number_integer())
            out.filetree_icon_size = w["filetree_icon_size"].get<int>();

        if (w.contains("filetree_columns") && w["filetree_columns"].is_array()) {
            for (const auto& el : w["filetree_columns"]) {
                if (!el.is_object()) continue;
                FiletreeColumnSpec c;
                if (el.contains("fmtid") && el["fmtid"].is_string())
                    c.fmtid_str = el["fmtid"].get<std::string>();
                if (el.contains("pid") && el["pid"].is_number_unsigned())
                    c.pid = el["pid"].get<DWORD>();
                if (el.contains("width") && el["width"].is_number_unsigned())
                    c.width = el["width"].get<UINT>();
                if (!c.fmtid_str.empty() && out.filetree_columns.size() < 32u)
                    out.filetree_columns.push_back(std::move(c));
            }
        }

        out.defer_dock_containers = w.value("defer_dock_containers", true);

        if (w.contains("recent_files"))
            read_path_array(w["recent_files"], out.recent_files);
        if (w.contains("recent_folders"))
            read_path_array(w["recent_folders"], out.recent_folders);
    }
    catch (const std::exception& ex) {
        err = std::string("layout JSON parse: ") + ex.what();
        layout_tracef("ParseWindowLayout: FAILED - %s", err.c_str());
        return false;
    }
    layout_tracef("ParseWindowLayout: OK - placement=%d visibility=%d defer=%d panels[Q=%d,L=%d,S=%d,FT=%d,C=%d,FR=%d,DR=%d]",
        out.has_placement, out.has_panel_visibility, out.defer_dock_containers,
        out.pv_queue, out.pv_log, out.pv_settings, out.pv_filetree, out.pv_chat, out.pv_findresults, out.pv_dupresults);
    return true;
}

json LayoutStore::WindowLayoutToJson(const WindowLayout& layout)
{
    layout_tracef("WindowLayoutToJson: enter placement=%d visibility=%d defer=%d",
        layout.has_placement, layout.has_panel_visibility, layout.defer_dock_containers);
    json w = json::object();
    write_window_layout_fields(w, layout);
    layout_trace("WindowLayoutToJson: OK");
    return w;
}

json LayoutStore::WindowLayoutToExportJson(const WindowLayout& layout)
{
    json w = json::object();
    w["has_placement"] = layout.has_placement;
    w["show_cmd"]      = layout.show_cmd;
    w["normal_rect"]   = json::array({
        layout.normal_rect.left, layout.normal_rect.top,
        layout.normal_rect.right, layout.normal_rect.bottom});
    w["filetree_show_shell_frames"] = layout.filetree_show_shell_frames;
    w["center_view_tabbed"]         = layout.center_view_tabbed;

    if (layout.has_panel_visibility) {
        w["has_panel_visibility"] = true;
        auto& p                   = w["panels"];
        p["queue"]                = layout.pv_queue;
        p["log"]                  = layout.pv_log;
        p["settings"]             = layout.pv_settings;
        p["fileinfo"]             = layout.pv_fileinfo;
        p["filetree"]             = layout.pv_filetree;
        p["nodes"]                = layout.pv_nodes;
        p["findresults"]          = layout.pv_findresults;
        p["dupresults"]           = layout.pv_dupresults;
        p["chat"]                 = layout.pv_chat;
        p["console"]              = layout.pv_console;
    }
    return w;
}

bool LayoutStore::LoadWindowLayout(WindowLayout& out, std::string& err, const char* workbench_slot)
{
    layout_tracef("LoadWindowLayout: enter slot='%s'", workbench_slot ? workbench_slot : "main");
    media::settings::SettingsLoadLabelScope _ls("window_layout");
    out = WindowLayout{};
    err.clear();

    if (media::settings::ui_reset_session()) {
        layout_trace("LoadWindowLayout: ui_reset_session=true, using defaults");
        g_defer_dock_container_load.store(true, std::memory_order_relaxed);
        try_apply_workbench_window_defaults_to_layout(out, workbench_slot);
        return true;
    }

    std::string raw_json;
    if (!media::settings::load_settings_utf8(raw_json, err)) {
        layout_tracef("LoadWindowLayout: FAILED to load settings - %s", err.c_str());
        return false;
    }
    if (raw_json.empty()) {
        layout_trace("LoadWindowLayout: settings empty, using defaults");
        g_defer_dock_container_load.store(true, std::memory_order_relaxed);
        try_apply_workbench_window_defaults_to_layout(out, workbench_slot);
        return true;
    }

    try {
        auto        j  = json::parse(raw_json);
        const json* pW = window_subtree_read(j, workbench_slot);
        if (!pW) {
            layout_trace("LoadWindowLayout: no window subtree, using defaults");
            g_defer_dock_container_load.store(true, std::memory_order_relaxed);
            try_apply_workbench_window_defaults_to_layout(out, workbench_slot);
            return true;
        }
        if (!ParseWindowLayout(*pW, out, err))
            return false;
    }
    catch (const std::exception& ex) {
        err = std::string("layout JSON parse: ") + ex.what();
        layout_tracef("LoadWindowLayout: FAILED - %s", err.c_str());
        return false;
    }
    g_defer_dock_container_load.store(out.defer_dock_containers, std::memory_order_relaxed);
    layout_trace("LoadWindowLayout: OK");
    return true;
}

bool LayoutStore::SaveWindowLayout(const WindowLayout& layout, std::string& err, const char* workbench_slot)
{
    layout_tracef("SaveWindowLayout: enter slot='%s' placement=%d visibility=%d panels[Q=%d,L=%d,S=%d,FT=%d,C=%d]",
        workbench_slot ? workbench_slot : "main",
        layout.has_placement, layout.has_panel_visibility,
        layout.pv_queue, layout.pv_log, layout.pv_settings, layout.pv_filetree, layout.pv_chat);
    media::settings::SettingsLoadLabelScope _ls("save_window_layout");
    const char* slot = workbench_slot_or_main(workbench_slot);

    std::string raw_json;
    json        j = json::object();
    if (media::settings::load_settings_utf8(raw_json, err) && !raw_json.empty()) {
        try { j = json::parse(raw_json); } catch (...) { j = json::object(); }
    }

    // Remove legacy dock/panel keys if present from older builds.
    j.erase("layout");

    json& wM = ensure_workbench_slot(j, workbench_slot)["window"];
    write_window_layout_fields(wM, layout);

    if (std::strcmp(slot, "main") == 0 && j.contains("window"))
        j.erase("window");
    bool ok = media::settings::save_settings_utf8(j.dump(2), err);
    layout_tracef("SaveWindowLayout: %s", ok ? "OK" : "FAILED");
    return ok;
}

bool LayoutStore::SetFiletreeShowShellFrames(bool show, std::string& err, const char* workbench_slot)
{
    media::settings::SettingsLoadLabelScope _ls("set_filetree_show_shell_frames");
    const char* slot = workbench_slot_or_main(workbench_slot);

    std::string raw_json;
    json        j = json::object();
    if (media::settings::load_settings_utf8(raw_json, err) && !raw_json.empty()) {
        try {
            j = json::parse(raw_json);
        } catch (const std::exception&) {
            j = json::object();
        }
    }
    json& wM = ensure_workbench_slot(j, workbench_slot)["window"];
    if (!wM.is_object())
        wM = json::object();
    wM["filetree_show_shell_frames"] = show;
    if (std::strcmp(slot, "main") == 0 && j.contains("window"))
        j.erase("window");
    return media::settings::save_settings_utf8(j.dump(2), err);
}

bool LayoutStore::SetFiletreeFilterMask(const std::string& mask, std::string& err, const char* workbench_slot)
{
    media::settings::SettingsLoadLabelScope _ls("set_filetree_filter_mask");
    const char* slot = workbench_slot_or_main(workbench_slot);

    std::string raw_json;
    json        j = json::object();
    if (media::settings::load_settings_utf8(raw_json, err) && !raw_json.empty()) {
        try {
            j = json::parse(raw_json);
        } catch (const std::exception&) {
            j = json::object();
        }
    }
    json& wM = ensure_workbench_slot(j, workbench_slot)["window"];
    if (!wM.is_object())
        wM = json::object();
    wM["filetree_filter_mask"] = mask;
    if (std::strcmp(slot, "main") == 0 && j.contains("window"))
        j.erase("window");
    return media::settings::save_settings_utf8(j.dump(2), err);
}

bool LayoutStore::LoadWorkbenchWindowDefaults(WorkbenchWindowDefaults& out, std::string& err, const char* workbench_slot)
{
    out = WorkbenchWindowDefaults{};
    err.clear();
    const char* slot = workbench_slot_or_main(workbench_slot);
    std::string raw;
    if (!media::settings::load_settings_utf8(raw, err))
        return false;
    if (raw.empty())
        return true;
    try {
        const auto j = json::parse(raw);
        if (!j.contains("workbench") || !j["workbench"].is_object())
            return true;
        if (!j["workbench"].contains(slot) || !j["workbench"][slot].is_object())
            return true;
        const auto& wb = j["workbench"][slot];
        if (!wb.contains("window_defaults") || !wb["window_defaults"].is_object())
            return true;
        const auto& d = wb["window_defaults"];
        if (d.contains("width") && d["width"].is_number()) {
            const int wv
                = d["width"].is_number_integer() ? d["width"].get<int>() : (int)std::lround(d["width"].get<double>());
            out.width = wv > 0 ? wv : 0;
        }
        if (d.contains("height") && d["height"].is_number()) {
            const int hv
                = d["height"].is_number_integer() ? d["height"].get<int>() : (int)std::lround(d["height"].get<double>());
            out.height = hv > 0 ? hv : 0;
        }
    } catch (const std::exception& ex) {
        err = std::string("workbench window_defaults: ") + ex.what();
        return false;
    }
    return true;
}

} // namespace media::layout
