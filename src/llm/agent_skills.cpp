#include "llm/agent_skills.hpp"

#include "core/settings_store.hpp"
#include "core/settings_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace media::llm::skills {

namespace {

static std::string to_lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string trim(std::string s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

static void dedupe_lower(std::vector<std::string>& values) {
    std::unordered_map<std::string, bool> seen;
    std::vector<std::string> out;
    out.reserve(values.size());
    for (auto v : values) {
        v = to_lower(trim(std::move(v)));
        if (v.empty() || seen.count(v))
            continue;
        seen[v] = true;
        out.push_back(std::move(v));
    }
    values = std::move(out);
}

static std::string current_os_tag() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

struct ParsedMeta {
    std::string description;
    bool always = false;
    std::vector<std::string> os;
    std::vector<std::string> bins;
    std::vector<std::string> env;
};

static ParsedMeta parse_skill_metadata(const std::filesystem::path& skill_md) {
    ParsedMeta meta;
    std::ifstream in(skill_md, std::ios::binary);
    if (!in)
        return meta;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty())
        return meta;

    auto parse_csv_array = [](std::string raw, bool lower) -> std::vector<std::string> {
        std::vector<std::string> out;
        raw = trim(raw);
        if (raw.empty())
            return out;
        if (raw.front() == '[' && raw.back() == ']' && raw.size() >= 2)
            raw = raw.substr(1, raw.size() - 2);
        std::size_t pos = 0;
        while (pos < raw.size()) {
            const std::size_t comma = raw.find(',', pos);
            std::string one = (comma == std::string::npos)
                ? raw.substr(pos)
                : raw.substr(pos, comma - pos);
            one = trim(one);
            if (!one.empty() && one.front() == '"' && one.back() == '"' && one.size() >= 2)
                one = one.substr(1, one.size() - 2);
            if (!one.empty() && one.front() == '\'' && one.back() == '\'' && one.size() >= 2)
                one = one.substr(1, one.size() - 2);
            one = trim(one);
            if (!one.empty())
                out.push_back(lower ? to_lower(one) : one);
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
        return out;
    };

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#')
            continue;
        const std::size_t colon = t.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = to_lower(trim(t.substr(0, colon)));
        const std::string val = trim(t.substr(colon + 1));
        if (key == "description" && meta.description.empty()) {
            meta.description = val;
        } else if (key == "always") {
            meta.always = (to_lower(val) == "true");
        } else if (key == "os") {
            meta.os = parse_csv_array(val, true);
        } else if (key == "bins") {
            meta.bins = parse_csv_array(val, true);
        } else if (key == "env") {
            meta.env = parse_csv_array(val, false);
        }
    }

    dedupe_lower(meta.os);
    dedupe_lower(meta.bins);
    dedupe_lower(meta.env);
    return meta;
}

static bool path_has_skill_md(const std::filesystem::path& dir, std::filesystem::path& out_skill_md) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        return false;
    const auto f = dir / "SKILL.md";
    if (!std::filesystem::exists(f, ec) || !std::filesystem::is_regular_file(f, ec))
        return false;
    out_skill_md = f;
    return true;
}

static bool binary_available(const std::string& name) {
    if (name.empty())
        return false;
#if defined(_WIN32)
    // Prefer PATH lookup via where.exe.
    const std::string cmd = "where " + name + " >nul 2>nul";
#else
    const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
#endif
    const int rc = std::system(cmd.c_str());
    return rc == 0;
}

static SkillPolicy load_policy() {
    SkillPolicy p;
    std::string err;
    nlohmann::json custom;
    if (!media::settings::load_subtree(media::settings::SettingsSubtreeKeys::custom, custom, err))
        return p;
    if (!custom.is_object())
        return p;
    const auto& node = custom.contains("agent_skills") ? custom["agent_skills"] : nlohmann::json();
    if (!node.is_object())
        return p;
    if (node.contains("enabled") && node["enabled"].is_boolean())
        p.enabled = node["enabled"].get<bool>();
    if (node.contains("roaming_enabled") && node["roaming_enabled"].is_boolean())
        p.roaming_enabled = node["roaming_enabled"].get<bool>();
    if (node.contains("workspace_enabled") && node["workspace_enabled"].is_boolean())
        p.workspace_enabled = node["workspace_enabled"].get<bool>();
    if (node.contains("pinned") && node["pinned"].is_array()) {
        for (const auto& it : node["pinned"])
            if (it.is_string())
                p.pinned.push_back(it.get<std::string>());
    }
    if (node.contains("disabled") && node["disabled"].is_array()) {
        for (const auto& it : node["disabled"])
            if (it.is_string())
                p.disabled.push_back(it.get<std::string>());
    }
    dedupe_lower(p.pinned);
    dedupe_lower(p.disabled);
    return p;
}

} // namespace

std::filesystem::path default_roaming_skills_root() {
    return media::settings::get_config_dir() / "skills";
}

std::filesystem::path default_workspace_skills_root(const std::string& cwd_hint) {
    const std::filesystem::path base = cwd_hint.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(cwd_hint);
    return base / "skills";
}

SkillSnapshot discover_skills(const SkillPolicy& policy_in, const std::string& cwd_hint) {
    SkillSnapshot snap;
    snap.roaming_root = default_roaming_skills_root();
    snap.workspace_root = default_workspace_skills_root(cwd_hint);
    snap.policy = policy_in;

    if (snap.policy.pinned.empty() && snap.policy.disabled.empty()) {
        // If caller passed a default/empty policy, hydrate from settings.
        const SkillPolicy from_settings = load_policy();
        if (policy_in.enabled == true
            && policy_in.roaming_enabled == true
            && policy_in.workspace_enabled == true) {
            snap.policy = from_settings;
        }
    }

    dedupe_lower(snap.policy.pinned);
    dedupe_lower(snap.policy.disabled);
    std::unordered_map<std::string, bool> pinned_set;
    std::unordered_map<std::string, bool> disabled_set;
    for (const auto& n : snap.policy.pinned)
        pinned_set[n] = true;
    for (const auto& n : snap.policy.disabled)
        disabled_set[n] = true;

    auto add_from_root = [&](const std::filesystem::path& root, SkillSource src) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
            return;
        for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec)
                break;
            const auto& dir = it->path();
            if (!it->is_directory(ec))
                continue;
            std::filesystem::path skill_md;
            if (!path_has_skill_md(dir, skill_md))
                continue;
            SkillEntry e;
            e.name = dir.filename().string();
            e.skill_md_path = skill_md;
            e.source = src;
            const auto meta = parse_skill_metadata(skill_md);
            e.description = meta.description.empty() ? e.name : meta.description;
            e.always = meta.always;

            // availability
            e.available = true;
            if (!meta.os.empty()) {
                const std::string cur = current_os_tag();
                bool match = false;
                for (const auto& one : meta.os)
                    if (one == cur) {
                        match = true;
                        break;
                    }
                if (!match) {
                    e.available = false;
                    e.missing_requirements.push_back("os:" + cur);
                }
            }
            for (const auto& b : meta.bins) {
                if (!binary_available(b)) {
                    e.available = false;
                    e.missing_requirements.push_back("bin:" + b);
                }
            }
            for (const auto& env : meta.env) {
                const char* v = std::getenv(env.c_str());
                if (!v || !*v) {
                    e.available = false;
                    e.missing_requirements.push_back("env:" + env);
                }
            }

            const std::string key = to_lower(e.name);
            e.pinned = pinned_set.count(key) > 0;
            e.disabled = disabled_set.count(key) > 0;
            e.active = snap.policy.enabled && e.available && !e.disabled && (e.always || e.pinned);
            snap.entries.push_back(std::move(e));
        }
    };

    // Roaming first, then workspace overlay with name replacement.
    if (snap.policy.roaming_enabled)
        add_from_root(snap.roaming_root, SkillSource::Roaming);
    if (snap.policy.workspace_enabled) {
        SkillSnapshot ws;
        std::vector<SkillEntry> ws_entries;
        {
            std::error_code ec;
            if (std::filesystem::exists(snap.workspace_root, ec) && std::filesystem::is_directory(snap.workspace_root, ec)) {
                for (auto it = std::filesystem::directory_iterator(snap.workspace_root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                    if (ec)
                        break;
                    const auto& dir = it->path();
                    if (!it->is_directory(ec))
                        continue;
                    std::filesystem::path skill_md;
                    if (!path_has_skill_md(dir, skill_md))
                        continue;
                    SkillEntry e;
                    e.name = dir.filename().string();
                    e.skill_md_path = skill_md;
                    e.source = SkillSource::Workspace;
                    const auto meta = parse_skill_metadata(skill_md);
                    e.description = meta.description.empty() ? e.name : meta.description;
                    e.always = meta.always;
                    e.available = true;
                    if (!meta.os.empty()) {
                        const std::string cur = current_os_tag();
                        bool match = false;
                        for (const auto& one : meta.os)
                            if (one == cur) {
                                match = true;
                                break;
                            }
                        if (!match) {
                            e.available = false;
                            e.missing_requirements.push_back("os:" + cur);
                        }
                    }
                    for (const auto& b : meta.bins) {
                        if (!binary_available(b)) {
                            e.available = false;
                            e.missing_requirements.push_back("bin:" + b);
                        }
                    }
                    for (const auto& env : meta.env) {
                        const char* v = std::getenv(env.c_str());
                        if (!v || !*v) {
                            e.available = false;
                            e.missing_requirements.push_back("env:" + env);
                        }
                    }
                    const std::string key = to_lower(e.name);
                    e.pinned = pinned_set.count(key) > 0;
                    e.disabled = disabled_set.count(key) > 0;
                    e.active = snap.policy.enabled && e.available && !e.disabled && (e.always || e.pinned);
                    ws_entries.push_back(std::move(e));
                }
            }
        }
        for (auto& ws_e : ws_entries) {
            const std::string ws_key = to_lower(ws_e.name);
            snap.entries.erase(
                std::remove_if(snap.entries.begin(), snap.entries.end(), [&](const SkillEntry& cur) {
                    return to_lower(cur.name) == ws_key;
                }),
                snap.entries.end());
            snap.entries.push_back(std::move(ws_e));
        }
    }

    std::sort(snap.entries.begin(), snap.entries.end(), [](const SkillEntry& a, const SkillEntry& b) {
        return to_lower(a.name) < to_lower(b.name);
    });
    return snap;
}

} // namespace media::llm::skills

