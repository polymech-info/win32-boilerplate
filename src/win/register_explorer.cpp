#include "register_explorer.hpp"
#include "constants.hpp"
#include "settings_store.hpp"
#include "logger/logger.h"
#include "shell/pm_iexecute_reg.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "Advapi32.lib")

namespace media::win {
namespace {

namespace fs = std::filesystem;

// `L"SystemFileAssociations\\"` is 23 wchars, not 24. Using `compare(0, 24, ...)` made the
// 24th character compare against L'\0' vs the first char of the extension, so the match
// always failed: Presets/Chat never registered for image assocs, only for `Directory`.
static constexpr wchar_t k_sfa_prefix[]   = L"SystemFileAssociations\\";
static constexpr size_t  k_sfa_prefix_len = (sizeof(k_sfa_prefix) / sizeof(wchar_t)) - 1u;
static constexpr wchar_t k_explorer11_open_clsid_w[] = L"{A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D}";
static constexpr wchar_t k_explorer11_handler_name_w[] = L"Pixlwiz";
static constexpr wchar_t k_explorer11_sparse_name_w[] = L"PolyMech.PMImage.Explorer11";
static constexpr bool    WIN11_FORCE = true;

/** Logger + stderr + %APPDATA%\\<k_config_subpath>\\register-explorer.log (so in-app Update Explorer is traceable). */
void reg_explorer_log_line(const std::string& msg) {
    const std::string full = "[register-explorer] " + msg;
    logger::info(full);
    std::cerr << full << "\n";
    try {
        std::error_code ec;
        const fs::path dir  = media::settings::get_config_dir();
        fs::create_directories(dir, ec);
        const fs::path logf = dir / "register-explorer.log";
        std::ofstream     out(logf, std::ios::app | std::ios::binary);
        if (!out)
            return;
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        char ts[48]{};
        sprintf_s(ts, "%04u-%02u-%02u %02u:%02u:%02u  ", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                  (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        out << ts << msg << "\n";
    } catch (...) {
    }
}

void reg_explorer_log_top_level_json_keys() {
    const media::settings::SettingsLoadLabelScope _ls("register_explorer_key_dump");
    std::string         err, raw;
    if (!media::settings::load_settings_utf8(raw, err)) {
        reg_explorer_log_line("load_settings_utf8 (for key dump) failed: " + err);
        return;
    }
    if (raw.empty()) {
        reg_explorer_log_line("settings file empty or not read");
        return;
    }
    try {
        const auto j = nlohmann::json::parse(raw, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            reg_explorer_log_line("settings root is not a JSON object");
            return;
        }
        std::ostringstream oss;
        oss << "settings.json top-level keys: ";
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) oss << ", ";
            first = false;
            oss << it.key();
        }
        reg_explorer_log_line(oss.str());
    } catch (const std::exception& e) {
        reg_explorer_log_line(std::string("parse settings for key dump: ") + e.what());
    }
}

std::wstring utf8_to_wide(const std::string &s) {
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wide_to_utf8(const std::wstring &w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string reg_status_u8(LSTATUS status) {
    return std::to_string(static_cast<long>(status));
}

bool is_process_elevated() {
    BOOL is_admin = FALSE;
    PSID admin_group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    if (::AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        ::CheckTokenMembership(nullptr, admin_group, &is_admin);
        ::FreeSid(admin_group);
    }
    return is_admin == TRUE;
}

std::wstring exe_directory() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return L".";
    std::wstring p(buf, n);
    const size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return L".";
    return p.substr(0, pos);
}

std::wstring default_media_bin() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::wstring(pm::brand::k_exe_basename_w);
    return std::wstring(buf, n);
}

bool file_exists_w(const std::wstring &path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring normalize_path(const std::wstring &p) {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH, buf, nullptr);
    if (n == 0 || n >= MAX_PATH)
        return p;
    return std::wstring(buf, n);
}

struct TransformShellEntry {
    std::wstring registry_sub_id;
    std::wstring mui_label;
    std::wstring preset_id;
};

struct CustomShellItem {
    std::wstring registry_sub_id;
    std::wstring mui_label;
    std::wstring command_id;
};

struct CustomShellMenu {
    std::wstring registry_sub_id;
    std::wstring mui_label;
    std::vector<CustomShellItem> items;
    std::vector<CustomShellMenu> submenus;
};

std::wstring sanitize_tf_reg_id(const std::string &utf8_id) {
    const std::wstring    w  = utf8_to_wide(utf8_id);
    std::wstring          o  = L"tf_";
    for (const wchar_t c : w) {
        if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'-' || c == L'_')
            o += c;
        else
            o += L'_';
    }
    if (o.size() > 90) o.resize(90);
    return o;
}

std::wstring sanitize_custom_reg_id(const std::string& prefix, const std::string& utf8_id) {
    std::wstring o = utf8_to_wide(prefix);
    const std::wstring w = utf8_to_wide(utf8_id);
    for (const wchar_t c : w) {
        if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'-' || c == L'_')
            o += c;
        else
            o += L'_';
    }
    if (o.size() > 90) o.resize(90);
    return o.empty() ? L"custom_command" : o;
}

std::string trim_command_json(std::string json_text) {
    if (json_text.size() >= 3 && static_cast<unsigned char>(json_text[0]) == 0xEF &&
        static_cast<unsigned char>(json_text[1]) == 0xBB && static_cast<unsigned char>(json_text[2]) == 0xBF) {
        json_text.erase(0, 3);
    }
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!json_text.empty() && is_ws(static_cast<unsigned char>(json_text.front())))
        json_text.erase(json_text.begin());
    while (!json_text.empty() && is_ws(static_cast<unsigned char>(json_text.back())))
        json_text.pop_back();
    return json_text;
}

bool custom_command_executable_from_shell(const nlohmann::json& item) {
    if (!item.is_object())
        return false;
    if (item.contains("cliCommand") && item["cliCommand"].is_string() && !item["cliCommand"].get<std::string>().empty())
        return true;
    if (item.contains("externalCommand") && item["externalCommand"].is_object())
        return true;
    return false;
}

std::string custom_action_name(const nlohmann::json& item) {
    if (item.contains("cliCommand") && item["cliCommand"].is_string())
        return "cliCommand:" + item["cliCommand"].get<std::string>();
    if (item.contains("externalCommand") && item["externalCommand"].is_object())
        return "externalCommand";
    if (item.contains("appCommand")) return "appCommand";
    if (item.contains("ribbonCommand")) return "ribbonCommand";
    if (item.contains("url")) return "url";
    if (item.contains("path")) return "path";
    return "metadata";
}

CustomShellMenu collect_custom_shell_menu(const nlohmann::json& owner,
                                          const std::string& fallback_id,
                                          const std::string& fallback_label,
                                          std::set<std::wstring>& seen_reg_ids,
                                          std::set<std::wstring>& seen_command_ids,
                                          int depth = 0) {
    CustomShellMenu menu;
    const std::string id = owner.value("id", fallback_id);
    const std::string label = owner.value("label", fallback_label.empty() ? id : fallback_label);
    menu.registry_sub_id = sanitize_custom_reg_id(depth == 0 ? "custom_group_" : "custom_menu_", id.empty() ? label : id);
    while (seen_reg_ids.count(menu.registry_sub_id))
        menu.registry_sub_id += L"_";
    seen_reg_ids.insert(menu.registry_sub_id);
    menu.mui_label = utf8_to_wide(label.empty() ? id : label);

    const nlohmann::json items = owner.contains("items") ? owner["items"] : nlohmann::json::array();
    if (!items.is_array()) {
        reg_explorer_log_line("custom commands: skip submenu id=" + id + " because items is not an array");
        return menu;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (!item.is_object()) {
            reg_explorer_log_line("custom commands: skip non-object item in " + id + " index=" + std::to_string(i));
            continue;
        }
        const std::string type = item.value("type", std::string{"button"});
        const std::string item_id = item.value("id", std::string{});
        const std::string item_label = item.value("label", item_id.empty() ? ("Command " + std::to_string(i + 1)) : item_id);
        if (type == "separator") {
            reg_explorer_log_line("custom commands: skip separator id=" + item_id);
            continue;
        }
        if (item.contains("items") && item["items"].is_array()) {
            CustomShellMenu sub = collect_custom_shell_menu(item, item_id.empty() ? ("dropdown-" + std::to_string(i)) : item_id,
                                                           item_label, seen_reg_ids, seen_command_ids, depth + 1);
            if (!sub.items.empty() || !sub.submenus.empty()) {
                reg_explorer_log_line("custom commands: + submenu label=" + wide_to_utf8(sub.mui_label)
                                      + " entries=" + std::to_string(sub.items.size())
                                      + " submenus=" + std::to_string(sub.submenus.size()));
                menu.submenus.push_back(std::move(sub));
            }
        }
        if (!item.value("registerInExplorer", false)) {
            reg_explorer_log_line("custom commands: skip id=" + item_id + " label=" + item_label + " (registerInExplorer=false)");
            continue;
        }
        if (item.value("enabled", true) == false || item.value("visible", true) == false) {
            reg_explorer_log_line("custom commands: skip id=" + item_id + " label=" + item_label + " (disabled or invisible)");
            continue;
        }
        if (item_id.empty()) {
            reg_explorer_log_line("custom commands: skip label=" + item_label + " (missing id)");
            continue;
        }
        if (!custom_command_executable_from_shell(item)) {
            reg_explorer_log_line("custom commands: skip id=" + item_id + " label=" + item_label
                                  + " unsupported action=" + custom_action_name(item)
                                  + " (Explorer custom commands currently use CLI wrapper)");
            continue;
        }
        const std::wstring wid = utf8_to_wide(item_id);
        if (seen_command_ids.count(wid)) {
            reg_explorer_log_line("custom commands: skip duplicate command id=" + item_id);
            continue;
        }
        seen_command_ids.insert(wid);
        CustomShellItem entry;
        entry.command_id = wid;
        entry.mui_label = utf8_to_wide(item_label);
        entry.registry_sub_id = sanitize_custom_reg_id("custom_", item_id);
        while (seen_reg_ids.count(entry.registry_sub_id))
            entry.registry_sub_id += L"_";
        seen_reg_ids.insert(entry.registry_sub_id);
        reg_explorer_log_line("custom commands: + id=" + item_id + " label=" + item_label
                              + " action=" + custom_action_name(item));
        menu.items.push_back(std::move(entry));
    }
    return menu;
}

std::vector<CustomShellMenu> load_custom_shell_menus() {
    std::vector<CustomShellMenu> out;
    std::string raw, err;
    if (!media::settings::load_command_json_utf8(raw, err)) {
        reg_explorer_log_line("custom commands: load_command_json_utf8 failed: " + err);
        return out;
    }
    if (raw.empty()) {
        reg_explorer_log_line("custom commands: commands.json missing or empty");
        return out;
    }
    try {
        const auto doc = nlohmann::json::parse(trim_command_json(std::move(raw)));
        const auto groups = doc.value("ribbon", nlohmann::json::object()).value("groups", nlohmann::json::array());
        if (!groups.is_array()) {
            reg_explorer_log_line("custom commands: ribbon.groups missing or not an array");
            return out;
        }
        reg_explorer_log_line("custom commands: scanning groups=" + std::to_string(groups.size())
                              + " path=" + media::settings::get_command_json_path().string());
        std::set<std::wstring> seen_reg_ids;
        std::set<std::wstring> seen_command_ids;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const auto& group = groups[i];
            if (!group.is_object()) {
                reg_explorer_log_line("custom commands: skip non-object group index=" + std::to_string(i));
                continue;
            }
            CustomShellMenu menu = collect_custom_shell_menu(group, "group-" + std::to_string(i),
                                                            group.value("label", std::string{}),
                                                            seen_reg_ids, seen_command_ids);
            if (!menu.items.empty() || !menu.submenus.empty()) {
                reg_explorer_log_line("custom commands: + group submenu label=" + wide_to_utf8(menu.mui_label)
                                      + " entries=" + std::to_string(menu.items.size())
                                      + " submenus=" + std::to_string(menu.submenus.size()));
                out.push_back(std::move(menu));
            }
        }
        reg_explorer_log_line("custom commands: registered menu groups=" + std::to_string(out.size())
                              + " command delegates=" + std::to_string(seen_command_ids.size()));
    } catch (const std::exception& e) {
        reg_explorer_log_line(std::string("custom commands: parse failed: ") + e.what());
    }
    return out;
}

void collect_custom_command_ids(const std::vector<CustomShellMenu>& menus, std::vector<std::wstring>& out) {
    for (const auto& menu : menus) {
        for (const auto& item : menu.items)
            out.push_back(item.command_id);
        collect_custom_command_ids(menu.submenus, out);
    }
}

/** Visible Explorer label: `Transform: <name>` — distinct from Resize / Chat in the same cascade. */
std::wstring transform_mui_verb(const std::string &name_utf8) {
    return L"Transform: " + utf8_to_wide(name_utf8);
}

std::vector<TransformShellEntry> load_transform_shell_entries() {
    std::vector<TransformShellEntry>            v;
    std::set<std::wstring>                      seen;
    std::string                                 err;
    std::vector<media::settings::ExplorerPreset> presets;
    if (!media::settings::load_explorer_presets(presets, err)) {
        reg_explorer_log_line("load_explorer_presets failed: " + err);
    } else {
        reg_explorer_log_line("explorer_presets: " + std::to_string(presets.size()) + " item(s) in store");
        for (const auto &p : presets) {
            if (p.op != "transform" || p.id.empty()) {
                reg_explorer_log_line("  skip explorer_preset id=" + p.id + " op=" + p.op + " (need op=transform, id)");
                continue;
            }
            TransformShellEntry e;
            e.preset_id         = utf8_to_wide(p.id);
            e.mui_label
                = transform_mui_verb(p.label.empty() ? p.id : p.label);
            e.registry_sub_id   = sanitize_tf_reg_id(p.id);
            if (seen.count(e.registry_sub_id)) {
                reg_explorer_log_line("  skip duplicate registry id: " + wide_to_utf8(e.registry_sub_id));
                continue;
            }
            seen.insert(e.registry_sub_id);
            v.push_back(std::move(e));
            reg_explorer_log_line("  + transform preset_id=" + p.id + " label=" + (p.label.empty() ? p.id : p.label));
        }
    }
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err)) {
        reg_explorer_log_line("load_chat_web failed: " + err);
    } else if (!cw.is_object() || !cw.contains("quick_actions") || !cw["quick_actions"].is_array()) {
        std::ostringstream w;
        w << "chat_web: no quick_actions[] (is_object=" << (cw.is_object() ? 1 : 0);
        if (cw.is_object()) w << " keys=" << cw.size();
        w << ")";
        reg_explorer_log_line(w.str());
    } else {
        const auto& qa = cw["quick_actions"];
        reg_explorer_log_line("chat_web.quick_actions: " + std::to_string(qa.size()) + " element(s)");
        for (const auto &el : qa) {
            if (!el.is_object() || !el.contains("id")) {
                reg_explorer_log_line("  skip quick_action: not an object with id");
                continue;
            }
            std::string   id   = el.value("id", "");
            std::string   name = el.value("name", "");
            const std::string prompt = el.value("prompt", "");
            if (id.empty()) {
                reg_explorer_log_line("  skip quick_action: empty id name=" + name);
                continue;
            }
            if (prompt.empty()) {
                reg_explorer_log_line("  skip quick_action: empty prompt id=" + id);
                continue;
            }
            // Web chat may store id as `qa-...` (we prefix) or full `chat-qa-...` (do not double-prefix)
            const std::string full   = (id.size() > 5 && id.compare(0, 5, "chat-") == 0) ? id : ("chat-" + id);
            TransformShellEntry e;
            e.preset_id = utf8_to_wide(full);
            e.mui_label
                = transform_mui_verb(name.empty() ? id : name);
            e.registry_sub_id = sanitize_tf_reg_id(full);
            if (seen.count(e.registry_sub_id)) {
                reg_explorer_log_line("  skip duplicate chat id: " + full);
                continue;
            }
            seen.insert(e.registry_sub_id);
            v.push_back(std::move(e));
            reg_explorer_log_line("  + chat --preset-id " + full + " name=" + (name.empty() ? id : name));
        }
    }
    nlohmann::json tf;
    if (!media::settings::load_subtree("transform", tf, err)) {
        reg_explorer_log_line("load_subtree(\"transform\") failed: " + err);
    } else if (!tf.is_object() || !tf.contains("prompt_presets") || !tf["prompt_presets"].is_array()) {
        reg_explorer_log_line("transform: no prompt_presets[] in settings");
    } else {
        const auto&  psrc = tf["prompt_presets"];
        const std::size_t n = psrc.size();
        reg_explorer_log_line("transform.prompt_presets: " + std::to_string(n) + " item(s)");
        for (std::size_t i = 0; i < n; ++i) {
            const auto& el = psrc[i];
            if (!el.is_object()) {
                reg_explorer_log_line("  skip style index " + std::to_string(i) + " not an object");
                continue;
            }
            const std::string prompt = el.value("prompt", "");
            if (prompt.empty()) {
                reg_explorer_log_line("  skip style index " + std::to_string(i) + " empty prompt");
                continue;
            }
            const std::string name = el.value("name", "");
            const std::string pid  = "style-" + std::to_string(i);
            TransformShellEntry e;
            e.preset_id = utf8_to_wide(pid);
            e.mui_label
                = transform_mui_verb(name.empty() ? pid : name);
            e.registry_sub_id = sanitize_tf_reg_id(pid);
            if (seen.count(e.registry_sub_id)) {
                reg_explorer_log_line("  skip duplicate: " + pid);
                continue;
            }
            seen.insert(e.registry_sub_id);
            v.push_back(std::move(e));
            reg_explorer_log_line("  + style --preset-id " + pid + " name=" + (name.empty() ? pid : name));
        }
    }
    reg_explorer_log_line("transform shell entries total: " + std::to_string(v.size())
                          + " (in \"Presets\" submenu when >0; main \"Chat…\" is separate, on PM Media root)");
    return v;
}

bool parse_widths(const std::string &s, std::vector<int> &out) {
    out.clear();
    std::string cur;
    auto flush = [&]() {
        if (cur.empty())
            return;
        try {
            int v = std::stoi(cur);
            if (v > 0)
                out.push_back(v);
        } catch (...) {
        }
        cur.clear();
    };
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '\t') {
            flush();
        } else {
            cur.push_back(c);
        }
    }
    flush();
    return !out.empty();
}

/** Lowercase ASCII extension without dot, e.g. "jpg", "tiff", "arw". */
static const char *k_canonical_ext[] = {"jpg",  "jpeg", "png",  "gif", "bmp", "webp", "tiff", "tif",
                                        "jpe",  "jfif", "avif", "arw"};

/** Non-photo preview types: PM Media registers Chat/Workbench/Viewer (no image ops). */
static const char *k_viewer_openwith_extra_ascii[] = {
    "pdf", "md", "markdown", "txt", "log", "csv", "tsv", "xls", "xlsx",
    "json", "jsonl", "ndjson", "xblox", "xml", "html", "htm", "css", "scss", "less",
    "js", "jsx", "mjs", "cjs", "ts", "tsx", "cpp", "cxx", "cc", "c", "h", "hpp", "hxx", "inl",
    "py", "rb", "rs", "go", "java", "kt", "swift", "cs", "sh", "bash", "zsh", "ps1", "cmd", "bat",
    "yaml", "yml", "toml", "ini", "conf", "cfg", "env", "sql", "dot", "gv", "svg", "diff", "patch",
    "gitignore", "gitattributes", "editorconfig", "cmake", "dockerfile",
    "stl", "obj", "gltf", "glb", "ply", "step", "stp", "dxf", "scad",
    "mp4", "m4v", "webm", "mov", "mkv", "avi", "wmv", "ogv", "ogg", "mpg", "mpeg", "ts", "m2ts", "mts", "3gp", "3g2"};

/** Register .ext, .EXT, and .Ext so Explorer matches case-insensitively per user preference. */
std::vector<std::wstring> ext_dot_variants(const std::string &canon_lower_ascii) {
    std::wstring lower = L".";
    std::wstring upper = L".";
    std::wstring title = L".";
    for (unsigned char ch : canon_lower_ascii) {
        lower += static_cast<wchar_t>(ch);
        upper += static_cast<wchar_t>(std::toupper(ch));
    }
    if (!canon_lower_ascii.empty()) {
        title += static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(canon_lower_ascii[0])));
        for (size_t i = 1; i < canon_lower_ascii.size(); ++i)
            title += static_cast<wchar_t>(canon_lower_ascii[i]);
    }
    std::vector<std::wstring> v;
    v.push_back(lower);
    v.push_back(upper);
    v.push_back(title);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

struct AssocTarget {
    std::wstring classes_suffix;
    /** `Directory\\Background` uses `%V`; file/folder targets use `%1`. */
    bool is_background = false;
    /** Non-image previewable file types get Chat/Workbench/Viewer only, not resize/convert/image ops. */
    bool preview_only = false;
};

LONG create_key(HKEY root, const std::wstring &rel, HKEY *out) {
    return RegCreateKeyExW(root, rel.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, nullptr, out, nullptr);
}

bool set_sz(HKEY key, const wchar_t *name, const std::wstring &value) {
    const BYTE *data = reinterpret_cast<const BYTE *>(value.c_str());
    const DWORD cb = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_SZ, data, cb) == ERROR_SUCCESS;
}

bool set_default_sz(HKEY key, const std::wstring &value) {
    const BYTE *data = reinterpret_cast<const BYTE *>(value.c_str());
    const DWORD cb = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, nullptr, 0, REG_SZ, data, cb) == ERROR_SUCCESS;
}

/** ProgId for “Open with” and `OpenWithProgids` (per-user, HKCU\Software\Classes). */
static std::wstring applications_exe_name(const std::wstring& media_exe) {
    const size_t p = media_exe.find_last_of(L"\\/");
    if (p == std::wstring::npos) return media_exe;
    return media_exe.substr(p + 1);
}

/** Legacy viewer duplicate path, used only to remove old Open-with/App Paths registration. */
static std::wstring sibling_viewer_exe_path(const std::wstring& media_exe) {
    const size_t p = media_exe.find_last_of(L"\\/");
    if (p == std::wstring::npos) return std::wstring(pm::brand::k_viewer_exe_basename_w);
    return media_exe.substr(0, p + 1) + std::wstring(pm::brand::k_viewer_exe_basename_w);
}

/** Shell `Open with` / ProgId `open` verb: main workbench seeded by top-level --src. */
static std::wstring build_open_with_command(const std::wstring& media_exe) {
    return L"\"" + media_exe + L"\" --ui-preset=main --src \"%1\"";
}

bool write_verbed_command_delegate(HKEY root, const std::wstring &shell_group, const std::wstring &id,
                                   const std::wstring &mui_name, const std::wstring &delegate_clsid_w,
                                   const std::wstring &icon_exe);

static void set_shell_verb_icon(HKEY h, const std::wstring &icon_exe);

/** `Applications\app.exe\SupportedTypes` is a *subkey*; each supported ext is a value name (e.g. ".jpg") with empty data — not one REG_SZ with semicolons. */
static bool add_supported_type_values(HKEY h_supported_types) {
    for (const char* c : k_canonical_ext) {
        std::wstring ext = L".";
        for (unsigned char ch : std::string(c)) ext += static_cast<wchar_t>(ch);
        if (!set_sz(h_supported_types, ext.c_str(), L"")) return false;
    }
    return true;
}

static bool register_app_paths_user_for_file(const std::wstring& full_exe_path) {
    const std::wstring exe_fn = applications_exe_name(full_exe_path);
    const std::wstring rel    = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + exe_fn;
    HKEY               h      = nullptr;
    if (create_key(HKEY_CURRENT_USER, rel, &h) != ERROR_SUCCESS) return false;
    if (!set_default_sz(h, full_exe_path)) {
        RegCloseKey(h);
        return false;
    }
    RegCloseKey(h);
    return true;
}

static void unregister_app_paths_user_for_file(const std::wstring& full_exe_path) {
    const std::wstring rel = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + applications_exe_name(full_exe_path);
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, rel.c_str());
}

static bool register_app_paths_user(const std::wstring& media_exe) { return register_app_paths_user_for_file(media_exe); }

static void unregister_app_paths_user(const std::wstring& media_exe) {
    const std::wstring rel = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + applications_exe_name(media_exe);
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, rel.c_str());
}

static std::wstring make_fileexts_ext_dot(const char* canon) {
    std::wstring s = L".";
    for (; *canon; ++canon) s += static_cast<wchar_t>(static_cast<unsigned char>(*canon));
    return s;
}

static void unregister_viewer_openwith_extra_progids() {
    for (const char* c : k_viewer_openwith_extra_ascii) {
        for (const std::wstring& dot : ext_dot_variants(std::string(c))) {
            const std::wstring owp = L"Software\\Classes\\" + dot + L"\\OpenWithProgids";
            HKEY               h    = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, owp.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
                (void)RegDeleteValueW(h, pm::brand::k_open_with_viewer_progid_w);
                RegCloseKey(h);
            }
        }
    }
    const std::wstring dir_owp = L"Software\\Classes\\Directory\\OpenWithProgids";
    HKEY               h       = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, dir_owp.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
        (void)RegDeleteValueW(h, pm::brand::k_open_with_viewer_progid_w);
        RegCloseKey(h);
    }
}

bool register_open_with_for_user(const std::wstring& media_exe) {
    const std::wstring    open_cmd  = build_open_with_command(media_exe);
    const std::wstring    exe_fn    = applications_exe_name(media_exe);
    const std::wstring    pro_base  = L"Software\\Classes\\" + std::wstring(pm::brand::k_open_with_progid_w);

    HKEY h = nullptr;
    if (create_key(HKEY_CURRENT_USER, pro_base, &h) != ERROR_SUCCESS) return false;
    if (!set_default_sz(h, pm::brand::k_app_display_w)) {
        RegCloseKey(h);
        return false;
    }
    RegCloseKey(h);
    {
        const std::wstring dicon = pro_base + L"\\DefaultIcon";
        if (create_key(HKEY_CURRENT_USER, dicon, &h) == ERROR_SUCCESS) {
            (void)set_default_sz(h, L"\"" + media_exe + L"\",0");
            RegCloseKey(h);
        }
    }
    {
        const std::wstring sopen = pro_base + L"\\shell\\open\\command";
        if (create_key(HKEY_CURRENT_USER, sopen, &h) != ERROR_SUCCESS) return false;
        if (!set_default_sz(h, open_cmd)) {
            RegCloseKey(h);
            return false;
        }
        RegCloseKey(h);
    }

    const std::wstring app_path = L"Software\\Classes\\Applications\\" + exe_fn;
    if (create_key(HKEY_CURRENT_USER, app_path, &h) != ERROR_SUCCESS) return false;
    if (!set_default_sz(h, pm::brand::k_app_display_w)) {
        RegCloseKey(h);
        return false;
    }
    (void)set_sz(h, L"FriendlyAppName", pm::brand::k_app_display_w);
    RegCloseKey(h);
    {
        const std::wstring app_icon = app_path + L"\\DefaultIcon";
        if (create_key(HKEY_CURRENT_USER, app_icon, &h) == ERROR_SUCCESS) {
            (void)set_default_sz(h, L"\"" + media_exe + L"\",0");
            RegCloseKey(h);
        }
    }
    {
        const std::wstring st_sub = app_path + L"\\SupportedTypes";
        if (create_key(HKEY_CURRENT_USER, st_sub, &h) != ERROR_SUCCESS) return false;
        if (!add_supported_type_values(h)) {
            RegCloseKey(h);
            return false;
        }
        RegCloseKey(h);
    }
    {
        const std::wstring aopen = app_path + L"\\shell\\open\\command";
        if (create_key(HKEY_CURRENT_USER, aopen, &h) != ERROR_SUCCESS) return false;
        if (!set_default_sz(h, open_cmd)) {
            RegCloseKey(h);
            return false;
        }
        RegCloseKey(h);
    }
    if (!register_app_paths_user(media_exe)) return false;

    for (const char* c : k_canonical_ext) {
        const std::wstring dot = make_fileexts_ext_dot(c);
        const std::wstring owp = L"Software\\Classes\\" + dot + L"\\OpenWithProgids";
        if (create_key(HKEY_CURRENT_USER, owp, &h) != ERROR_SUCCESS) return false;
        if (!set_sz(h, pm::brand::k_open_with_progid_w, L"")) {
            RegCloseKey(h);
            return false;
        }
        RegCloseKey(h);
    }
    unregister_viewer_openwith_extra_progids();
    reg_explorer_log_line("Open with: main ProgId only; stale PM Viewer Open-with entries cleaned");
    return true;
}

void unregister_viewer_open_with_for_user(const std::wstring& media_exe) {
    const std::wstring media_n = normalize_path(media_exe);
    const std::wstring viewer_n = normalize_path(sibling_viewer_exe_path(media_n));
    const std::wstring pro_viewer = L"Software\\Classes\\" + std::wstring(pm::brand::k_open_with_viewer_progid_w);
    {
        HKEY h_ra = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, KEY_SET_VALUE, &h_ra)
            == ERROR_SUCCESS) {
            (void)RegDeleteValueW(h_ra, pm::brand::k_open_with_viewer_progid_w);
            RegCloseKey(h_ra);
        }
    }
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, pro_viewer.c_str());
    {
        const std::wstring viewer_appk = L"Software\\Classes\\Applications\\" + std::wstring(pm::brand::k_viewer_exe_basename_w);
        (void)RegDeleteTreeW(HKEY_CURRENT_USER, viewer_appk.c_str());
    }
    unregister_app_paths_user_for_file(viewer_n);
    for (const char* c : k_canonical_ext) {
        const std::wstring dot = make_fileexts_ext_dot(c);
        const std::wstring owp = L"Software\\Classes\\" + dot + L"\\OpenWithProgids";
        HKEY h = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, owp.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            (void)RegDeleteValueW(h, pm::brand::k_open_with_viewer_progid_w);
            RegCloseKey(h);
        }
    }
    unregister_viewer_openwith_extra_progids();
}

void unregister_open_with_for_user(const std::wstring& media_exe) {
    const std::wstring   media_n  = normalize_path(media_exe);
    const std::wstring   viewer_n = normalize_path(sibling_viewer_exe_path(media_n));
    const std::wstring   exe_fn = applications_exe_name(media_exe);
    const std::wstring   pro    = L"Software\\Classes\\" + std::wstring(pm::brand::k_open_with_progid_w);
    const std::wstring   pro_viewer = L"Software\\Classes\\" + std::wstring(pm::brand::k_open_with_viewer_progid_w);
    {
        HKEY h_ra = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, KEY_SET_VALUE, &h_ra)
            == ERROR_SUCCESS) {
            (void)RegDeleteValueW(h_ra, pm::brand::k_open_with_viewer_progid_w);
            RegCloseKey(h_ra);
        }
    }
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, pro_viewer.c_str());
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, pro.c_str());
    {
        const std::wstring viewer_appk = L"Software\\Classes\\Applications\\" + std::wstring(pm::brand::k_viewer_exe_basename_w);
        (void)RegDeleteTreeW(HKEY_CURRENT_USER, viewer_appk.c_str());
    }
    unregister_app_paths_user_for_file(viewer_n);
    const std::wstring   appk = L"Software\\Classes\\Applications\\" + exe_fn;
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, appk.c_str());
    unregister_app_paths_user(media_exe);
    for (const char* c : k_canonical_ext) {
        const std::wstring  dot = make_fileexts_ext_dot(c);
        const std::wstring  owp = L"Software\\Classes\\" + dot + L"\\OpenWithProgids";
        HKEY                  h   = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, owp.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            (void)RegDeleteValueW(h, pm::brand::k_open_with_viewer_progid_w);
            (void)RegDeleteValueW(h, pm::brand::k_open_with_progid_w);
            RegCloseKey(h);
        }
    }
    unregister_viewer_openwith_extra_progids();
}

static void set_shell_verb_icon(HKEY h, const std::wstring &icon_exe) {
    if (icon_exe.empty()) return;
    (void)set_sz(h, L"Icon", L"\"" + icon_exe + L"\",0");
}

static std::wstring explorer11_dll_from_exe_dir(const std::wstring& exe_dir) {
    return normalize_path(exe_dir + L"\\" + std::wstring(pm::brand::k_explorer11_dll_basename_w));
}

static std::wstring explorer11_package_root_from_exe_dir(const std::wstring& exe_dir) {
    return normalize_path(exe_dir);
}

static std::wstring explorer11_sparse_root_from_exe_dir(const std::wstring& exe_dir) {
    return normalize_path(exe_dir + L"\\sparse");
}

static std::wstring explorer11_sparse_manifest_from_exe_dir(const std::wstring& exe_dir) {
    return normalize_path(explorer11_sparse_root_from_exe_dir(exe_dir) + L"\\AppxManifest.xml");
}

static std::wstring ps_single_quote(const std::wstring& value) {
    std::wstring quoted = L"'";
    for (wchar_t ch : value) {
        if (ch == L'\'')
            quoted += L"''";
        else
            quoted += ch;
    }
    quoted += L"'";
    return quoted;
}

static std::wstring win32_command_arg_quote(const std::wstring& value) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += ch;
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += ch;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L"\"";
    return quoted;
}

static std::wstring current_exe_path_w() {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size())
        return {};
    buf.resize(n);
    return buf;
}

static std::wstring register_explorer_elevated_params(const RegisterExplorerOptions& opt) {
    std::wstring params = L"register-explorer";
    params += L" --elevated-write-only";
    params += L" --group ";
    params += win32_command_arg_quote(utf8_to_wide(opt.group));
    params += L" --widths ";
    params += win32_command_arg_quote(utf8_to_wide(opt.widths));
    if (opt.unregister)
        params += L" --unregister";
    if (!opt.refresh_shell)
        params += L" --no-refresh-shell";
    if (!opt.media_bin.empty()) {
        params += L" --media-bin ";
        params += win32_command_arg_quote(utf8_to_wide(opt.media_bin));
    }
    return params;
}

static int relaunch_register_explorer_elevated(const RegisterExplorerOptions& opt) {
    const std::wstring exe = current_exe_path_w();
    const std::wstring params = register_explorer_elevated_params(opt);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    reg_explorer_log_line("register-explorer: requesting elevation via ShellExecuteExW(runas)");
    if (!::ShellExecuteExW(&sei)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_CANCELLED)
            std::cerr << "register-explorer: elevation cancelled by user\n";
        else
            std::cerr << "register-explorer: ShellExecuteExW(runas) failed: " << static_cast<unsigned long>(err) << "\n";
        reg_explorer_log_line("register-explorer: elevation failed/cancelled gle=" + std::to_string(static_cast<unsigned long>(err)));
        return 1;
    }

    DWORD exit_code = 1;
    if (sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, INFINITE);
        (void)::GetExitCodeProcess(sei.hProcess, &exit_code);
        ::CloseHandle(sei.hProcess);
    }
    reg_explorer_log_line("register-explorer: elevated child exit=" + std::to_string(static_cast<unsigned long>(exit_code)));
    return static_cast<int>(exit_code);
}

static bool run_explorer11_sparse_powershell(const std::wstring& script, const char* label) {
    fs::path output_path;
    try {
        output_path = media::settings::get_config_dir() / (std::string("register-explorer-powershell-") + label + ".log");
    } catch (...) {
        output_path = fs::temp_directory_path() / (std::string("register-explorer-powershell-") + label + ".log");
    }
    std::error_code remove_ec;
    fs::remove(output_path, remove_ec);

    const std::wstring wrapped =
        L"& { try { " + script
        + L"; exit 0 } catch { Write-Error ($_ | Out-String); if ($_.Exception) { Write-Error ($_.Exception.ToString()) }; exit 1 } } *> "
        + ps_single_quote(output_path.wstring());
    std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command " + win32_command_arg_quote(wrapped);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    bool saw_deployment_error = false;
    reg_explorer_log_line(std::string("Win11 sparse package: powershell begin ") + label
                          + " output=" + output_path.string());
    if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        reg_explorer_log_line(std::string("Win11 sparse package: CreateProcess failed ") + label
                              + " gle=" + std::to_string(static_cast<unsigned long>(GetLastError())));
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    reg_explorer_log_line(std::string("Win11 sparse package: powershell end ") + label
                          + " exit=" + std::to_string(static_cast<unsigned long>(exit_code)));
    try {
        std::ifstream out(output_path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
        if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xFF && static_cast<unsigned char>(text[1]) == 0xFE) {
            std::wstring wide;
            wide.reserve((text.size() - 2) / 2);
            for (size_t i = 2; i + 1 < text.size(); i += 2) {
                const auto lo = static_cast<unsigned char>(text[i]);
                const auto hi = static_cast<unsigned char>(text[i + 1]);
                wide.push_back(static_cast<wchar_t>(lo | (hi << 8)));
            }
            text = wide_to_utf8(wide);
        }
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '\0'))
            text.pop_back();
        if (!text.empty()) {
            if (text.find("Deployment failed") != std::string::npos || text.find("Add-AppxPackage :") != std::string::npos)
                saw_deployment_error = true;
            reg_explorer_log_line(std::string("Win11 sparse package: powershell output ") + label + ": " + text);
        }
    } catch (...) {
    }
    return exit_code == 0 && !saw_deployment_error;
}

static bool unregister_explorer11_sparse_package() {
    const std::wstring script =
        L"$ErrorActionPreference='Stop';"
        L"$pkg=Get-AppxPackage -Name " + ps_single_quote(k_explorer11_sparse_name_w) + L" -ErrorAction SilentlyContinue;"
        L"if($pkg){$pkg|Remove-AppxPackage}";
    const bool ok = run_explorer11_sparse_powershell(script, "unregister");
    reg_explorer_log_line("Win11 sparse package: unregister "
                          + wide_to_utf8(k_explorer11_sparse_name_w)
                          + " ok=" + std::string(ok ? "yes" : "no"));
    return ok;
}

static bool register_explorer11_sparse_package(const std::wstring& exe_dir) {
    const std::wstring package_root = explorer11_package_root_from_exe_dir(exe_dir);
    const std::wstring sparse_root = explorer11_sparse_root_from_exe_dir(exe_dir);
    const std::wstring manifest = explorer11_sparse_manifest_from_exe_dir(exe_dir);
    const std::wstring package_dll = explorer11_dll_from_exe_dir(exe_dir);
    const std::wstring package_exe = normalize_path(package_root + L"\\" + std::wstring(pm::brand::k_exe_basename_w));
    reg_explorer_log_line("Win11 sparse package: register begin name=" + wide_to_utf8(k_explorer11_sparse_name_w)
                          + " root=" + wide_to_utf8(package_root)
                          + " sparseRoot=" + wide_to_utf8(sparse_root)
                          + " manifest=" + wide_to_utf8(manifest)
                          + " manifestExists=" + std::string(file_exists_w(manifest) ? "yes" : "no")
                          + " exe=" + wide_to_utf8(package_exe)
                          + " exeExists=" + std::string(file_exists_w(package_exe) ? "yes" : "no")
                          + " dll=" + wide_to_utf8(package_dll)
                          + " dllExists=" + std::string(file_exists_w(package_dll) ? "yes" : "no"));
    if (!file_exists_w(manifest) || !file_exists_w(package_exe) || !file_exists_w(package_dll)) {
        reg_explorer_log_line("Win11 sparse package: skipped; missing sparse layout built by npm run build:cpp");
        return false;
    }

    unregister_explorer11_sparse_package();
    const std::wstring script =
        L"$ErrorActionPreference='Continue';"
        L"$errs=@();"
        L"$out=Add-AppxPackage -Register -Path " + ps_single_quote(manifest)
        + L" -ExternalLocation " + ps_single_quote(package_root)
        + L" -Verbose -ErrorAction SilentlyContinue -ErrorVariable errs 4>&1 3>&1 2>&1 | Out-String;"
        L"if($out){Write-Output $out};"
        L"if($errs.Count -gt 0){"
        L"Write-Output 'APPX_ERROR_RECORDS_BEGIN';"
        L"$errs | Format-List * -Force | Out-String | Write-Output;"
        L"foreach($e in $errs){if($e.Exception){Write-Output $e.Exception.ToString()}};"
        L"Write-Output 'APPX_ERROR_RECORDS_END';"
        L"};"
        L"$events=Get-WinEvent -LogName 'Microsoft-Windows-AppXDeploymentServer/Operational' -MaxEvents 8 -ErrorAction SilentlyContinue | "
        L"Select-Object TimeCreated,Id,LevelDisplayName,Message | Format-List | Out-String;"
        L"if($events){Write-Output 'APPX_RECENT_EVENTS_BEGIN';Write-Output $events;Write-Output 'APPX_RECENT_EVENTS_END'};"
        L"if($errs.Count -gt 0){exit 1};"
        L"exit 0";
    const bool ok = run_explorer11_sparse_powershell(script, "register");
    reg_explorer_log_line("Win11 sparse package: register end ok=" + std::string(ok ? "yes" : "no"));
    return ok;
}

static bool write_explorer11_context_handler_at(HKEY root, const char* root_label, const std::wstring& classes_suffix) {
    const std::wstring rel = L"Software\\Classes\\" + classes_suffix + L"\\shellex\\ContextMenuHandlers\\"
                           + std::wstring(k_explorer11_handler_name_w);
    HKEY h = nullptr;
    const LSTATUS create_status = create_key(root, rel, &h);
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: create handler key ") + root_label + "\\"
                          + wide_to_utf8(rel)
                          + " status=" + reg_status_u8(create_status));
    if (create_status != ERROR_SUCCESS) return false;
    const bool ok = set_default_sz(h, k_explorer11_open_clsid_w);
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: set handler default root=") + root_label
                          + " target=" + wide_to_utf8(classes_suffix)
                          + " clsid=" + wide_to_utf8(k_explorer11_open_clsid_w)
                          + " ok=" + std::string(ok ? "yes" : "no"));
    RegCloseKey(h);
    return ok;
}

static void unregister_explorer11_context_handler_for_root(HKEY root, const char* root_label) {
    const std::wstring clsid_rel = L"Software\\Classes\\CLSID\\" + std::wstring(k_explorer11_open_clsid_w);
    LSTATUS del_status = RegDeleteTreeW(root, clsid_rel.c_str());
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: delete ") + root_label + "\\" + wide_to_utf8(clsid_rel)
                          + " status=" + reg_status_u8(del_status));

    static constexpr const wchar_t* k_targets[] = {
        L"*",
        L"Directory",
        L"Directory\\Background",
        L"Folder",
        L"Drive",
    };
    for (const wchar_t* target : k_targets) {
        const std::wstring rel = L"Software\\Classes\\" + std::wstring(target)
                               + L"\\shellex\\ContextMenuHandlers\\" + std::wstring(k_explorer11_handler_name_w);
        del_status = RegDeleteTreeW(root, rel.c_str());
        reg_explorer_log_line(std::string("Win11 IExplorerCommand: delete ") + root_label + "\\" + wide_to_utf8(rel)
                              + " status=" + reg_status_u8(del_status));
    }
}

static void unregister_explorer11_context_handler_for_user() {
    unregister_explorer11_context_handler_for_root(HKEY_CURRENT_USER, "HKCU");
    if constexpr (WIN11_FORCE) {
        if (!is_process_elevated()) {
            reg_explorer_log_line("Win11 IExplorerCommand: skip HKLM unregister; process is not elevated");
            return;
        }
        unregister_explorer11_context_handler_for_root(HKEY_LOCAL_MACHINE, "HKLM");
        static constexpr const wchar_t* k_approved_paths[] = {
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
            L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        };
        for (const wchar_t* approved_path : k_approved_paths) {
            HKEY h = nullptr;
            const LSTATUS open_status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, approved_path, 0, KEY_SET_VALUE, &h);
            reg_explorer_log_line("Win11 IExplorerCommand: open HKLM\\" + wide_to_utf8(approved_path)
                                  + " for approved-delete status=" + reg_status_u8(open_status));
            if (open_status == ERROR_SUCCESS) {
                const LSTATUS del_status = RegDeleteValueW(h, k_explorer11_open_clsid_w);
                reg_explorer_log_line("Win11 IExplorerCommand: delete approved HKLM\\" + wide_to_utf8(approved_path)
                                      + " value=" + wide_to_utf8(k_explorer11_open_clsid_w)
                                      + " status=" + reg_status_u8(del_status));
                RegCloseKey(h);
            }
        }
    }
}

static bool write_explorer11_clsid_at(HKEY root, const char* root_label, const std::wstring& explorer11_dll) {
    const std::wstring clsid_rel = L"Software\\Classes\\CLSID\\" + std::wstring(k_explorer11_open_clsid_w);
    HKEY h = nullptr;
    LSTATUS create_status = create_key(root, clsid_rel, &h);
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: create CLSID key ") + root_label + "\\"
                          + wide_to_utf8(clsid_rel)
                          + " status=" + reg_status_u8(create_status));
    if (create_status != ERROR_SUCCESS) return false;
    const bool clsid_default_ok = set_default_sz(h, L"Open in Pixlwiz");
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: set CLSID default title root=") + root_label
                          + " ok="
                          + std::string(clsid_default_ok ? "yes" : "no"));
    RegCloseKey(h);
    if (!clsid_default_ok) return false;

    const std::wstring inproc_rel = clsid_rel + L"\\InprocServer32";
    create_status = create_key(root, inproc_rel, &h);
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: create InprocServer32 key ") + root_label + "\\"
                          + wide_to_utf8(inproc_rel)
                          + " status=" + reg_status_u8(create_status));
    if (create_status != ERROR_SUCCESS) return false;
    const bool server_ok = set_default_sz(h, explorer11_dll);
    const bool threading_ok = set_sz(h, L"ThreadingModel", L"Apartment");
    const bool inproc_ok = server_ok && threading_ok;
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: set InprocServer32 root=") + root_label
                          + " default ok="
                          + std::string(server_ok ? "yes" : "no")
                          + " threadingModel=Apartment ok=" + std::string(threading_ok ? "yes" : "no"));
    RegCloseKey(h);
    return inproc_ok;
}

static bool write_explorer11_approved_at(HKEY root, const char* root_label) {
    const std::wstring approved = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
    HKEY h = nullptr;
    const LSTATUS create_status = create_key(root, approved, &h);
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: create Approved key ") + root_label + "\\"
                          + wide_to_utf8(approved) + " status=" + reg_status_u8(create_status));
    if (create_status != ERROR_SUCCESS) return false;
    const bool ok = set_sz(h, k_explorer11_open_clsid_w, L"Pixlwiz");
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: set Approved root=") + root_label
                          + " value=" + wide_to_utf8(k_explorer11_open_clsid_w)
                          + " ok=" + std::string(ok ? "yes" : "no"));
    RegCloseKey(h);
    return ok;
}

static bool register_explorer11_context_handler_for_root(HKEY root, const char* root_label, const std::wstring& explorer11_dll) {
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: register root=") + root_label
                          + " clsid=" + wide_to_utf8(k_explorer11_open_clsid_w)
                          + " handlerName=" + wide_to_utf8(k_explorer11_handler_name_w)
                          + " dll=" + wide_to_utf8(explorer11_dll));

    if (!write_explorer11_clsid_at(root, root_label, explorer11_dll)) return false;

    static constexpr const wchar_t* k_targets[] = {
        L"*",
        L"Directory",
        L"Directory\\Background",
        L"Folder",
        L"Drive",
    };
    for (const wchar_t* target : k_targets) {
        if (!write_explorer11_context_handler_at(root, root_label, target)) return false;
    }

    HKEY verify = nullptr;
    const std::wstring verify_rel = L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\"
                                  + std::wstring(k_explorer11_handler_name_w);
    const LSTATUS verify_status = RegOpenKeyExW(root, verify_rel.c_str(), 0, KEY_READ, &verify);
    reg_explorer_log_line(std::string("Win11 IExplorerCommand: verify open ") + root_label + "\\"
                          + wide_to_utf8(verify_rel)
                          + " status=" + reg_status_u8(verify_status));
    if (verify) RegCloseKey(verify);

    return true;
}

static bool register_explorer11_context_handler_for_user(const std::wstring& explorer11_dll) {
    reg_explorer_log_line("Win11 IExplorerCommand: registration begin WIN11_FORCE="
                          + std::string(WIN11_FORCE ? "true" : "false")
                          + " clsid=" + wide_to_utf8(k_explorer11_open_clsid_w)
                          + " handlerName=" + wide_to_utf8(k_explorer11_handler_name_w)
                          + " dll=" + wide_to_utf8(explorer11_dll)
                          + " exists=" + std::string(file_exists_w(explorer11_dll) ? "yes" : "no"));
    unregister_explorer11_context_handler_for_user();
    if (!file_exists_w(explorer11_dll)) {
        reg_explorer_log_line("Win11 IExplorerCommand: skipped sparse COM prep; missing "
                              + wide_to_utf8(explorer11_dll));
        return true;
    }

    reg_explorer_log_line("Win11 IExplorerCommand: sparse package owns ExplorerCommand; stale classic shellex handlers removed; dll="
                          + wide_to_utf8(explorer11_dll)
                          + " overall=ok");
    return true;
}

/** IExecuteCommand: `…\command` default is empty, `DelegateExecute` = class id (braced string). */
bool write_command_delegate_at(HKEY root, const std::wstring& verb_key, const std::wstring& mui_name,
                               const std::wstring& delegate_clsid_w, const std::wstring& icon_exe) {
    HKEY h_cmd = nullptr;
    if (create_key(root, verb_key, &h_cmd) != ERROR_SUCCESS)
        return false;
    if (!set_sz(h_cmd, L"MUIVerb", mui_name)) {
        RegCloseKey(h_cmd);
        return false;
    }
    set_shell_verb_icon(h_cmd, icon_exe);
    RegCloseKey(h_cmd);

    const std::wstring sub_cmd = verb_key + L"\\command";
    HKEY h_run = nullptr;
    if (create_key(root, sub_cmd, &h_run) != ERROR_SUCCESS)
        return false;
    if (!set_default_sz(h_run, L"")) {
        RegCloseKey(h_run);
        return false;
    }
    if (!set_sz(h_run, L"DelegateExecute", delegate_clsid_w)) {
        RegCloseKey(h_run);
        return false;
    }
    RegCloseKey(h_run);
    return true;
}

bool write_verbed_command_delegate(HKEY root, const std::wstring &shell_group, const std::wstring &id,
                                   const std::wstring &mui_name, const std::wstring &delegate_clsid_w,
                                   const std::wstring &icon_exe) {
    return write_command_delegate_at(root, shell_group + L"\\shell\\" + id, mui_name, delegate_clsid_w, icon_exe);
}

/** Nested cascade `…\shell\pm_aichat` ("Presets": Chat + transform presets; MUI + empty subCommands).
 *  Registered **first** under the PM Media group: Explorer truncates flat static verbs (~16), so when this
 *  ran after resize/convert/meta it disappeared on image files; folders often had fewer competing verbs. */
bool write_nested_cascade_header(HKEY root, const std::wstring &pm_media_shell, const std::wstring &submenu_id,
                                 const std::wstring &mui_verb, const std::wstring &icon_exe) {
    const std::wstring path = pm_media_shell + L"\\shell\\" + submenu_id;
    HKEY h = nullptr;
    if (create_key(root, path, &h) != ERROR_SUCCESS)
        return false;
    if (!set_sz(h, L"MUIVerb", mui_verb)) {
        RegCloseKey(h);
        return false;
    }
    if (!set_sz(h, L"subCommands", L"")) {
        RegCloseKey(h);
        return false;
    }
    set_shell_verb_icon(h, icon_exe);
    RegCloseKey(h);
    return true;
}

bool write_custom_shell_menu(HKEY root, const std::wstring& parent_shell, const CustomShellMenu& menu,
                             const pm::iexecute::DelegateTable& delegates, const std::wstring& icon_exe) {
    if (!write_nested_cascade_header(root, parent_shell, menu.registry_sub_id, menu.mui_label, icon_exe))
        return false;
    const std::wstring menu_root = parent_shell + L"\\shell\\" + menu.registry_sub_id;
    for (const auto& item : menu.items) {
        const std::wstring* clsid = delegates.custom_clsid(item.command_id);
        if (!clsid) {
            reg_explorer_log_line("custom commands: missing DelegateExecute CLSID for id=" + wide_to_utf8(item.command_id));
            return false;
        }
        reg_explorer_log_line("custom commands: write verb label=" + wide_to_utf8(item.mui_label)
                              + " id=" + wide_to_utf8(item.command_id)
                              + " clsid=" + wide_to_utf8(*clsid));
        if (!write_verbed_command_delegate(root, menu_root, item.registry_sub_id, item.mui_label, *clsid, icon_exe))
            return false;
    }
    for (const auto& submenu : menu.submenus) {
        if (!write_custom_shell_menu(root, menu_root, submenu, delegates, icon_exe))
            return false;
    }
    return true;
}

bool register_one_target(HKEY root, const std::wstring &classes_suffix, const std::wstring &group,
                         const std::vector<TransformShellEntry> &transform_entries, bool register_chat_verb,
                         const std::vector<CustomShellMenu>& custom_menus,
                         const std::wstring &media, const std::vector<int> &widths,
                         const pm::iexecute::DelegateTable &delegates, bool preview_only) {
    const std::wstring base = std::wstring(L"Software\\Classes\\") + classes_suffix;

    // Win11 owns the top-level PixlWiz surface through the sparse IExplorerCommand package.
    // Remove stale classic top-level verbs so Show More Options cannot masquerade as the Win11 entry.
    const std::wstring stale_win11_probe = base + L"\\shell\\openpixlwiz";
    const LONG stale_r = RegDeleteTreeW(root, stale_win11_probe.c_str());
    if (stale_r != ERROR_SUCCESS && stale_r != ERROR_FILE_NOT_FOUND) {
        reg_explorer_log_line("classic top-level cleanup: delete " + wide_to_utf8(stale_win11_probe)
                              + " status=" + reg_status_u8(stale_r));
    }

    const std::wstring shell_group = base + L"\\shell\\" + group;

    HKEY h_shell = nullptr;
    if (create_key(root, shell_group, &h_shell) != ERROR_SUCCESS)
        return false;
    const bool head_ok = set_sz(h_shell, L"MUIVerb", group) && set_sz(h_shell, L"subCommands", L"");
    if (head_ok) set_shell_verb_icon(h_shell, media);
    RegCloseKey(h_shell);
    if (!head_ok)
        return false;

    // Write verbs in display order. Explorer preserves static subkey insertion order well enough for
    // this classic cascade, so keep this sequence intentional.
    if (register_chat_verb) {
#if FEATURE_COMMAND_LLM
        if (!write_verbed_command_delegate(root, shell_group, L"chatui", L"Open in Chat\u2026", delegates.chat, media))
            return false;
#endif
    }

    if (!write_verbed_command_delegate(root, shell_group, L"openui", pm::brand::k_shell_verb_open_in_w, delegates.openui,
                                       media))
        return false;

    // Same IExecute Kind::Viewer as ProgId `PolyMech.pm-image.viewer` / “Open with” — explicit Win10 cascade entry.
    if (!write_verbed_command_delegate(root, shell_group, L"pmview", L"Open in Viewer\u2026", delegates.viewer, media))
        return false;

    for (const auto& menu : custom_menus) {
        if (!write_custom_shell_menu(root, shell_group, menu, delegates, media))
            return false;
    }

#if FEATURE_COMMAND_TRANSFORM
    if (!preview_only && register_chat_verb && !transform_entries.empty()) {
        const std::wstring nest_id   = L"pm_aichat";
        const std::wstring nest_root = shell_group + L"\\shell\\" + nest_id;
        if (!write_nested_cascade_header(root, shell_group, nest_id, L"Presets", media))
            return false;
        for (const auto &te : transform_entries) {
            const std::wstring *tc = delegates.transform_clsid(te.preset_id);
            if (!tc) return false;
            if (!write_verbed_command_delegate(root, nest_root, te.registry_sub_id, te.mui_label, *tc, media))
                return false;
        }
    }
#endif

#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    if (!preview_only && !write_verbed_command_delegate(root, shell_group, L"pixlwizshare", L"Share to Pixlwiz\u2026", delegates.share, media))
        return false;
#endif

#if FEATURE_COMMAND_RESIZE
    if (!preview_only) {
        for (int w : widths) {
            const std::wstring id_in = L"r" + std::to_wstring(w) + L"in";
            const std::wstring id_cp = L"r" + std::to_wstring(w) + L"cp";
            const std::wstring name_in = L"Resize max " + std::to_wstring(w) + L" (in place)";
            const std::wstring name_cp = L"Resize max " + std::to_wstring(w) + L" (copy _" + std::to_wstring(w) + L")";

            auto write_resize = [&](const std::wstring &id, const std::wstring &mui_name, bool inplace) -> bool {
                if (const std::wstring *d = delegates.resize_clsid(w, inplace))
                    return write_verbed_command_delegate(root, shell_group, id, mui_name, *d, media);
                return false;
            };

            if (!write_resize(id_in, name_in, true))
                return false;
            if (!write_resize(id_cp, name_cp, false))
                return false;
        }
    }
#endif

#if FEATURE_COMMAND_COMPRESS
    if (!preview_only && !write_verbed_command_delegate(root, shell_group, L"cvtjpg", L"Convert to JPG", delegates.convert, media))
        return false;
#endif

#if FEATURE_COMMAND_META
    if (!preview_only && !write_verbed_command_delegate(root, shell_group, L"metagen", L"Generate Meta (.md + .json)", delegates.meta,
                                       media))
        return false;
#endif

    return true;
}

void refresh_shell_notify() {
    using SHChangeNotify_fn = void(WINAPI *)(LONG, UINT, const void *, const void *);
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    if (!sh)
        return;
    auto fn = reinterpret_cast<SHChangeNotify_fn>(GetProcAddress(sh, "SHChangeNotify"));
    if (fn)
        fn(0x08000000, 0, nullptr, nullptr);
    FreeLibrary(sh);
}

std::vector<AssocTarget> build_file_association_targets() {
    std::vector<AssocTarget> out;
    std::vector<std::wstring> seen_suffixes;
    auto add_unique = [&](AssocTarget t) {
        if (std::find(seen_suffixes.begin(), seen_suffixes.end(), t.classes_suffix) != seen_suffixes.end())
            return;
        seen_suffixes.push_back(t.classes_suffix);
        out.push_back(std::move(t));
    };
    for (const char *c : k_canonical_ext) {
        const std::string canon(c);
        for (const std::wstring &dot : ext_dot_variants(canon)) {
            AssocTarget t;
            t.classes_suffix = L"SystemFileAssociations\\" + dot;
            add_unique(std::move(t));
        }
    }
    for (const char *c : k_viewer_openwith_extra_ascii) {
        const std::string canon(c);
        for (const std::wstring &dot : ext_dot_variants(canon)) {
            AssocTarget t;
            t.classes_suffix = L"SystemFileAssociations\\" + dot;
            t.preview_only = true;
            add_unique(std::move(t));
        }
    }
    add_unique(AssocTarget{L"Directory", false, false});
    add_unique(AssocTarget{L"Directory\\Background", true, false});
    return out;
}

std::wstring shell_group_rel(const std::wstring &classes_suffix, const std::wstring &group) {
    return L"Software\\Classes\\" + classes_suffix + L"\\shell\\" + group;
}

} // namespace

int register_explorer_run(const RegisterExplorerOptions &opt) {
    if (opt.dry) {
        reg_explorer_log_line("dry run (no registry) group=" + opt.group);
    } else {
        reg_explorer_log_line("run begin group=" + opt.group
                              + (opt.unregister ? " (unregister)" : " (register)")
                              + " elevated=" + std::string(is_process_elevated() ? "yes" : "no")
                              + " elevatedWriteOnly=" + std::string(opt.elevated_write_only ? "yes" : "no"));
        if (!is_process_elevated())
            reg_explorer_log_line("register-explorer: running unelevated; mapped drives remain visible, HKLM fallback will be skipped");
    }
    try {
        reg_explorer_log_line("settings path: " + media::settings::get_settings_json_path().string());
    } catch (...) {
        reg_explorer_log_line("settings path: (unavailable)");
    }
    if (!opt.unregister) {
        reg_explorer_log_top_level_json_keys();
    }
    const std::wstring exe_dir = exe_directory();
    std::wstring       media_w = opt.media_bin.empty() ? default_media_bin() : utf8_to_wide(opt.media_bin);
    const std::vector<TransformShellEntry> transform_entries = load_transform_shell_entries();
    const std::vector<CustomShellMenu> custom_menus = load_custom_shell_menus();
    std::vector<std::wstring> custom_command_ids;
    collect_custom_command_ids(custom_menus, custom_command_ids);
    const std::wstring execute_dll_w =
        normalize_path(exe_dir + L"\\" + std::wstring(pm::brand::k_iexecute_dll_basename_w));
    const std::wstring explorer11_dll_w = explorer11_dll_from_exe_dir(exe_dir);
    if (!opt.dry) {
        reg_explorer_log_line(std::string("mode: IExecuteCommand (COM) via ") + pm::brand::k_iexecute_dll_basename_u8
                              + ": " + (file_exists_w(execute_dll_w) ? "dll present" : "dll missing"));
        reg_explorer_log_line(std::string("mode: Win11 IExplorerCommand root via ") + pm::brand::k_explorer11_dll_basename_u8
                              + ": " + (file_exists_w(explorer11_dll_w) ? "dll present" : "dll missing"));
        reg_explorer_log_line("media binary: " + wide_to_utf8(media_w));
        reg_explorer_log_line(std::string("Verbs: DelegateExecute → ") + pm::brand::k_iexecute_dll_basename_u8 + ".");
        if (transform_entries.empty()) {
            reg_explorer_log_line("no transform menu entries - check settings (explorer_presets op=transform, "
                                  "chat_web.quick_actions, transform.prompt_presets) and that settings load works");
        } else {
            reg_explorer_log_line("Chat… on PM Media root; \"Presets\" nested menu for transform + quick-action — "
                                  "registered first so Explorer static-verb limit does not drop the cascade on image types");
        }
        reg_explorer_log_line("custom command Explorer menus: groups=" + std::to_string(custom_menus.size())
                              + " delegates=" + std::to_string(custom_command_ids.size())
                              + " (all file types + Directory/Background; registerInExplorer=true cli/external only)");
    }

    std::vector<int> widths;
    if (!parse_widths(opt.widths, widths)) {
        std::cerr << "register-explorer: invalid or empty --widths\n";
        return 1;
    }

    if (!opt.unregister) {
        if (!file_exists_w(media_w)) {
            std::cerr << "register-explorer: media-img not found: " << wide_to_utf8(media_w) << "\n";
            return 1;
        }
        if (!file_exists_w(execute_dll_w)) {
            std::cerr << "register-explorer: requires " << wide_to_utf8(execute_dll_w) << " next to " << pm::brand::k_app_id_u8
                      << ".\n";
            return 1;
        }
    }

    const std::wstring group_w = utf8_to_wide(opt.group);

    if (opt.unregister) {
        if (opt.dry) {
            std::cout << "Dry run: would remove shell keys for group " << opt.group << "\n";
            return 0;
        }
        bool ok = true;
        for (const auto& t : build_file_association_targets()) {
            if (t.classes_suffix == L"Directory" || t.classes_suffix == L"Directory\\Background")
                continue;
            const std::wstring root_rel = L"Software\\Classes\\" + t.classes_suffix + L"\\shell\\openpixlwiz";
            const LONG root_r = RegDeleteTreeW(HKEY_CURRENT_USER, root_rel.c_str());
            if (root_r != ERROR_SUCCESS && root_r != ERROR_FILE_NOT_FOUND)
                ok = false;
            std::wstring rel = shell_group_rel(t.classes_suffix, group_w);
            const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, rel.c_str());
            if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND)
                ok = false;
        }
        {
            const std::wstring root_rel = L"Software\\Classes\\Directory\\shell\\openpixlwiz";
            const LONG root_r = RegDeleteTreeW(HKEY_CURRENT_USER, root_rel.c_str());
            if (root_r != ERROR_SUCCESS && root_r != ERROR_FILE_NOT_FOUND)
                ok = false;
            std::wstring rel = shell_group_rel(L"Directory", group_w);
            const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, rel.c_str());
            if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND)
                ok = false;
        }
        {
            const std::wstring root_rel = L"Software\\Classes\\Directory\\Background\\shell\\openpixlwiz";
            const LONG root_r = RegDeleteTreeW(HKEY_CURRENT_USER, root_rel.c_str());
            if (root_r != ERROR_SUCCESS && root_r != ERROR_FILE_NOT_FOUND)
                ok = false;
            std::wstring rel = shell_group_rel(L"Directory\\Background", group_w);
            const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, rel.c_str());
            if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND)
                ok = false;
        }
        unregister_open_with_for_user(normalize_path(media_w));
        if (!unregister_explorer11_sparse_package())
            ok = false;
        unregister_explorer11_context_handler_for_user();
        reg_explorer_log_line("Win11 IExplorerCommand: cleared root context handler registration and sparse package");
        if (!ok) {
            std::cerr << "register-explorer: failed to remove some registry keys\n";
            return 1;
        }
        std::cout << "Removed Explorer menus for group " << opt.group << " (and Open with / ProgIds "
                  << wide_to_utf8(std::wstring(pm::brand::k_open_with_progid_w)) << ", "
                  << wide_to_utf8(std::wstring(pm::brand::k_open_with_viewer_progid_w)) << ")\n";
        pm::iexecute::reg_cleanup_user_entries();
        reg_explorer_log_line(std::string("IExecute: cleared HKCU DelegateExecute / Inproc map for ") + pm::brand::k_iexecute_dll_basename_u8);
        if (opt.refresh_shell)
            refresh_shell_notify();
        return 0;
    }

    if (opt.dry) {
        std::cout << "Dry run (no registry): would register Explorer menus via IExecuteCommand (DelegateExecute → "
                  << pm::brand::k_iexecute_dll_basename_u8 << ") for group " << opt.group << ".\n";
        std::cout << "  Width presets: ";
        for (size_t i = 0; i < widths.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << widths[i];
        }
        std::cout << "\n  Presets / quick-action submenu entries: " << transform_entries.size() << "\n";
        std::cout << "  Custom command submenu groups: " << custom_menus.size()
                  << " (" << custom_command_ids.size() << " command delegate(s))\n";
        std::cout << "File types: " << (sizeof(k_canonical_ext) / sizeof(k_canonical_ext[0]))
                  << " image extensions with full PM Media verbs, plus "
                  << (sizeof(k_viewer_openwith_extra_ascii) / sizeof(k_viewer_openwith_extra_ascii[0]))
                  << " preview extensions with Chat/Workbench/Viewer verbs; plus Directory and Directory\\Background.\n";
        std::cout << "  Win11 root menu: " << wide_to_utf8(explorer11_sparse_manifest_from_exe_dir(exe_dir))
                  << " → Open in Pixlwiz (sparse package IExplorerCommand; classic shellex fallback: "
                  << wide_to_utf8(explorer11_dll_w) << ")\n";
        std::cout << "  Open with: ProgId " << wide_to_utf8(std::wstring(pm::brand::k_open_with_progid_w)) << " → "
                  << wide_to_utf8(build_open_with_command(normalize_path(media_w))) << "\n";
        std::cout << "  Open with: stale viewer ProgId "
                  << wide_to_utf8(std::wstring(pm::brand::k_open_with_viewer_progid_w))
                  << " / Applications\\" << pm::brand::k_viewer_exe_basename_u8 << " would be removed.\n";
        return 0;
    }

    pm::iexecute::DelegateTable delegates{};
    {
        const std::wstring edll = normalize_path(exe_dir + L"\\" + std::wstring(pm::brand::k_iexecute_dll_basename_w));
        pm::iexecute::reg_cleanup_user_entries();
        reg_explorer_log_line("IExecute: cleared old map/Inproc, registering fresh DelegateExecute for all verbs");
        std::vector<std::wstring> preset_ids;
        preset_ids.reserve(transform_entries.size());
        for (const auto &te : transform_entries) preset_ids.push_back(te.preset_id);
        if (!pm::iexecute::build_delegate_table(widths, preset_ids, custom_command_ids, edll, delegates)) {
            reg_explorer_log_line("IExecute: build_delegate_table failed (IExecuteMap, InprocServer32, or manifest)");
            std::cerr
                << "register-explorer: failed to write IExecute COM keys for " << pm::brand::k_iexecute_dll_basename_u8
                << " (is the DLL next to the exe, path readable, and HKCU writable?). "
                << "If context menus are broken, run: " << pm::brand::k_app_id_u8
                << " register-explorer --unregister\n  then: " << pm::brand::k_app_id_u8
                << " register-explorer\n";
            return 1;
        }
    }

    unregister_viewer_open_with_for_user(normalize_path(media_w));
    if (!register_open_with_for_user(normalize_path(media_w))) {
        reg_explorer_log_line("Open with registration failed (ProgId " + wide_to_utf8(std::wstring(pm::brand::k_open_with_progid_w)) + ")");
        std::cerr << "register-explorer: failed to register Open with / ProgId for " << pm::brand::k_app_id_u8 << " (see HKCU...\\"
                  << wide_to_utf8(std::wstring(pm::brand::k_open_with_progid_w)) << ")\n";
        return 1;
    }
    reg_explorer_log_line(std::string("Open with: ProgId ") + wide_to_utf8(std::wstring(pm::brand::k_open_with_progid_w))
                          + " + Applications\\" + wide_to_utf8(applications_exe_name(normalize_path(media_w)))
                          + " (stale PM Viewer ProgId/Application entries removed)");

    const std::vector<AssocTarget> targets = build_file_association_targets();
    {
        int n_img = 0, n_preview = 0, n_dir = 0, n_bkg = 0;
        for (const auto& t : targets) {
            if (t.is_background) ++n_bkg;
            else if (t.classes_suffix == L"Directory") ++n_dir;
            else if (t.preview_only) ++n_preview;
            else ++n_img;
        }
        reg_explorer_log_line("register targets: " + std::to_string(targets.size()) + " total (image assoc="
                              + std::to_string(n_img) + " preview-only assoc=" + std::to_string(n_preview)
                              + " Directory=" + std::to_string(n_dir) + " Background=" + std::to_string(n_bkg)
                              + ") - Chat + custom commands on all targets; Presets on image assocs and Directory only; not Background");
    }

    for (const auto &t : targets) {
        // "Chat…" on PM Media root for all file assocs + `Directory` (folder). Optional "Presets" (transform/QA)
        // only for image types and `Directory`; preview-only and `Directory\Background` omit Presets.
        const bool register_chat_verb = (t.classes_suffix.size() >= k_sfa_prefix_len
                                         && t.classes_suffix.compare(0, k_sfa_prefix_len, k_sfa_prefix) == 0)
                                       || (t.classes_suffix == L"Directory" && !t.is_background);
        if (!register_one_target(HKEY_CURRENT_USER, t.classes_suffix, group_w, transform_entries, register_chat_verb,
                                 custom_menus, media_w, widths, delegates, t.preview_only)) {
            reg_explorer_log_line("RegCreateKey / write failed for: " + wide_to_utf8(t.classes_suffix));
            std::cerr << "register-explorer: failed to register a context-menu target\n";
            return 1;
        }
    }

    if (!register_explorer11_context_handler_for_user(explorer11_dll_w)) {
        reg_explorer_log_line("Win11 IExplorerCommand: failed to write root handler keys for "
                              + wide_to_utf8(explorer11_dll_w));
        std::cerr << "register-explorer: failed to register Win11 root context-menu handler\n";
        return 1;
    }
    if (!register_explorer11_sparse_package(exe_dir)) {
        reg_explorer_log_line("Win11 sparse package: failed to register package for compact context menu");
        std::cerr << "register-explorer: failed to register Win11 sparse package for top-level context menu\n";
        return 1;
    }

    std::cout << "Registered Explorer menus for group " << opt.group << " and “Open with” (ProgId "
              << wide_to_utf8(std::wstring(pm::brand::k_open_with_progid_w)) << "; stale PM Viewer removed)\n";
    try {
        const fs::path iex_log = media::settings::get_config_dir() / "pm-image-iexecute.log";
        const std::string  p   = iex_log.string();
        reg_explorer_log_line("Context menu (IExecute) runtime log: " + p + " — lines appear when Explorer loads the shell DLL and runs a verb.");
        std::cout << "  IExecute debug log: " << p << "\n";
    } catch (...) {
    }
    reg_explorer_log_line("run end: success (refresh_shell=" + std::string(opt.refresh_shell ? "yes" : "no")
                          + "). Chat: all file assocs + Directory; custom commands: all targets; Presets (if any): image assocs and Directory; not Background.");
    if (opt.refresh_shell)
        refresh_shell_notify();
    return 0;
}

} // namespace media::win
