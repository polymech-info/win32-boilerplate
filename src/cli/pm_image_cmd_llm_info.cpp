#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_llm_info.hpp"

#include "cli_tty.hpp"
#include "html/html.h"
#include "llm/mcp_probe.hpp"
#include "llm/llm_image_tool_defaults.hpp"
#include "llm/path_tool_catalog.hpp"
#include "llm/agent_tools.hpp"
#include "llm/agent_skills.hpp"
#include "core/settings_runtime.hpp"
#include "core/settings_store.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_set>

namespace {

static void str_tolower_in_place(std::string& s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/// One compact line: `key:type, …` plus `| required: a, b` from flattened OpenAPI Input.
static std::string brief_openapi_input_signature_line(const nlohmann::json& flat_props, const nlohmann::json& required_arr)
{
    std::ostringstream sig;
    int                n   = 0;
    constexpr int      k_max_props = 28;
    for (auto it = flat_props.begin(); it != flat_props.end() && n < k_max_props; ++it, ++n) {
        if (n)
            sig << ", ";
        sig << it.key() << ":";
        const auto& p = it.value();
        if (!p.is_object()) {
            sig << "?";
            continue;
        }
        std::string t = p.value("type", std::string{});
        if (t == "array")
            sig << "array";
        else if (t == "integer" || t == "number")
            sig << "num";
        else if (t == "boolean")
            sig << "bool";
        else if (t == "object")
            sig << "obj";
        else if (!p.value("format", std::string{}).empty())
            sig << p["format"].get<std::string>();
        else
            sig << (t.empty() ? "str" : t);
    }
    if (static_cast<std::size_t>(n) < flat_props.size())
        sig << ", …";
    std::string req;
    if (required_arr.is_array()) {
        for (const auto& r : required_arr) {
            if (!r.is_string())
                continue;
            if (!req.empty())
                req += ", ";
            req += r.get<std::string>();
        }
    }
    std::string out = sig.str();
    if (!req.empty())
        out += " | required: " + req;
    return out;
}

static nlohmann::json openapi_input_pre_test_json(const std::string& provider, const std::string& model)
{
    nlohmann::json j;
    std::string    pv = provider;
    str_tolower_in_place(pv);
    const bool slug_ok = !model.empty() && model.find('/') != std::string::npos;
    j["applicable"]      = (pv == "replicate" && slug_ok);
    j["resolved_provider"] = provider;
    j["resolved_model"]    = model;
    if (!j["applicable"].get<bool>()) {
        j["note"] = "OpenAPI Input snapshot exists only for Replicate owner/name slugs in replicate-models-cache.json.";
        return j;
    }
    nlohmann::json flat;
    nlohmann::json req;
    std::string    err;
    if (!media::replicate_cli::lookup_replicate_openapi_input_flat(model, flat, req, err)) {
        j["ok"]    = false;
        j["error"] = err;
        return j;
    }
    j["ok"]              = true;
    j["property_count"]  = flat.is_object() ? static_cast<int>(flat.size()) : 0;
    j["signature_line"]  = brief_openapi_input_signature_line(flat, req);
    j["required_fields"] = req;
    return j;
}

static void print_openapi_input_pre_test_text(std::ostream& os, const std::string& provider, const std::string& model)
{
    std::string pv = provider;
    str_tolower_in_place(pv);
    const bool slug_ok = !model.empty() && model.find('/') != std::string::npos;
    if (pv != "replicate" || !slug_ok)
        return;  // not applicable — skip silently, caller already shows provider/model
    nlohmann::json flat;
    nlohmann::json req;
    std::string    err;
    if (!media::replicate_cli::lookup_replicate_openapi_input_flat(model, flat, req, err)) {
        os << "\n_Schema lookup failed:_ " << err << "\n";
        return;
    }

    // Props
    std::ostringstream sig;
    int n = 0;
    constexpr int k_max = 28;
    for (auto it = flat.begin(); it != flat.end() && n < k_max; ++it, ++n) {
        if (n) sig << "  ";
        const auto& p  = it.value();
        std::string t  = p.is_object() ? p.value("type", std::string{}) : std::string{};
        std::string badge;
        if (t == "array") badge = "array";
        else if (t == "integer" || t == "number") badge = "num";
        else if (t == "boolean") badge = "bool";
        else if (t == "object") badge = "obj";
        else if (p.is_object() && !p.value("format", std::string{}).empty())
            badge = p["format"].get<std::string>();
        else badge = t.empty() ? "str" : t;
        sig << "`" << it.key() << ":`" << badge;
    }
    if (static_cast<std::size_t>(n) < flat.size()) sig << "  …";

    // Required
    std::string reqstr;
    if (req.is_array()) {
        for (const auto& r : req) {
            if (!r.is_string()) continue;
            if (!reqstr.empty()) reqstr += ", ";
            reqstr += "`" + r.get<std::string>() + "`";
        }
    }

    os << "\n**Replicate schema** — `" << model << "`\n\n";
    os << sig.str() << "\n";
    if (!reqstr.empty())
        os << "\n**Required:** " << reqstr << "\n";
}

std::string settings_file_hint() {
    if (media::settings::has_settings_read_path_override()) {
        return media::settings::get_settings_effective_read_path().string()
            + " (read for this process; persist: " + media::settings::get_settings_json_path().string() + ")";
    }
    return media::settings::get_settings_json_path().string();
}

void print_mcp_probe_text(std::ostream& os, const nlohmann::json& mcp)
{
    os << "\n---\n\n## MCP\n\n";
    if (mcp.value("skipped", false)) {
        if (mcp.contains("note") && mcp["note"].is_string())
            os << "_" << mcp["note"].get<std::string>() << "_\n";
        return;
    }
    os << "_Profile `mcp.json` — same file the chat agent loads_\n\n";
    const std::string path = mcp.value("mcp_json_path", std::string(""));
    if (!path.empty())
        os << "**Path:** `" << path << "`\n";
    if (mcp.contains("profile_config_dir"))
        os << "**Profile dir:** `" << mcp["profile_config_dir"].get<std::string>() << "`\n";
    if (mcp.contains("note") && mcp["note"].is_string())
        os << "\n_" << mcp["note"].get<std::string>() << "_\n";
    if (mcp.contains("read_error"))
        os << "\n**Read error:** " << mcp["read_error"].get<std::string>() << "\n";
    if (mcp.contains("parse_error"))
        os << "\n**Parse error:** " << mcp["parse_error"].get<std::string>() << "\n";
    if (mcp.contains("config_shape_error"))
        os << "\n**Config error:** " << mcp["config_shape_error"].get<std::string>() << "\n";
    if (mcp.contains("hint") && mcp["hint"].is_string())
        os << "\n_" << mcp["hint"].get<std::string>() << "_\n";

    if (!mcp.value("exists", false)) {
        os << "\n_File missing — no MCP tools merged into the agent._\n";
        return;
    }

    const auto& servers = mcp["servers"];
    if (!servers.is_array() || servers.empty()) {
        os << "\n_(no server entries to show)_\n";
        return;
    }

    for (const auto& s : servers) {
        if (!s.is_object())
            continue;
        const std::string nm = s.value("name", std::string("(?)"));
        os << "\n### " << nm << "\n\n";
        if (s.value("skipped", false)) {
            os << "_Skipped:_ " << s.value("skip_reason", std::string()) << "\n";
            continue;
        }

        const bool hs_ok    = s.value("handshake_ok", false);
        const bool tools_ok = s.value("tools_list_ok", false);
        const int  n        = s.value("tool_count", 0);

        os << "| | |\n|:---|:---|\n";
        os << "| transport | `" << s.value("transport", std::string("?")) << "` |\n";
        os << "| handshake | " << (hs_ok ? "✓ ok" : "✗ **FAILED**");
        if (s.contains("handshake_error"))
            os << " — " << s["handshake_error"].get<std::string>();
        os << " |\n";
        os << "| tools/list | ";
        if (!tools_ok) {
            os << "✗ **FAILED**";
            if (s.contains("tools_list_error"))
                os << " — " << s["tools_list_error"].get<std::string>();
        } else {
            os << "✓ ok (" << n << " tools)";
        }
        os << " |\n";

        if (!tools_ok || !s.contains("tools") || !s["tools"].is_array())
            continue;

        os << "\n";
        int shown = 0;
        for (const auto& t : s["tools"]) {
            if (!t.is_object() || !t.contains("name"))
                continue;
            const std::string tn = t["name"].get<std::string>();
            std::string       td = t.value("description", std::string());
            if (td.size() > 100) { td.resize(97); td += "..."; }
            os << "- `" << tn << "`";
            if (!td.empty()) os << " — " << td;
            os << "\n";
            if (++shown >= 40 && n > 40) {
                os << "\n_… " << (n - shown) << " more — use `--json` for the full list_\n";
                break;
            }
        }
    }
}

// ── llm info tools ────────────────────────────────────────────────────────────

static const char* group_label(pm::llm::AgentToolGroup g) {
    switch (g) {
    case pm::llm::AgentToolGroup::File:      return "File";
    case pm::llm::AgentToolGroup::Image:     return "Image";
    case pm::llm::AgentToolGroup::Utility:   return "Utility";
    case pm::llm::AgentToolGroup::Scheduler: return "Scheduler";
    case pm::llm::AgentToolGroup::Memory:    return "Memory";
    case pm::llm::AgentToolGroup::Computer:  return "Computer";
    }
    return "?";
}

static int cmd_llm_info_tools(std::ostream& os, bool json_out)
{
    const auto* reg  = pm::llm::agent_tool_registry_data();
    const auto  size = pm::llm::agent_tool_registry_size();
    media::runtime_settings::GlobalToolSettings gs;
    std::string gerr;
    (void)media::runtime_settings::load_global_tool_settings(gs, gerr);
    std::unordered_set<std::string> disabled;
    for (const auto& n : gs.disabled_path_tools) {
        std::string k = n;
        str_tolower_in_place(k);
        if (!k.empty()) disabled.insert(std::move(k));
    }

    if (json_out) {
        nlohmann::json arr = nlohmann::json::array();
        for (std::size_t i = 0; i < size; ++i)
            arr.push_back({
                {"name",       reg[i].name},
                {"short_desc", reg[i].short_desc},
                {"group",      group_label(reg[i].group)},
                {"globally_enabled", disabled.count(reg[i].name) == 0},
            });
        arr.push_back({
            {"_global_mcp_tools_enabled", gs.mcp_tools_enabled},
            {"_global_disabled_mcp_servers", gs.disabled_mcp_servers},
            {"_settings_load_error", gerr},
        });
        std::cout << arr.dump(2) << "\n";
        return 0;
    }

    os << "# Built-in agent tools\n\n";
    os << "_" << size << " tools — path-mode catalog sent to the chat agent_\n\n";
    if (!gerr.empty())
        os << "_Global tool settings load error: " << gerr << "_\n\n";
    os << "| # | Group | Name | Global | Description |\n|:--|:--|:---|:--:|:---|\n";
    for (std::size_t i = 0; i < size; ++i) {
        const bool on = disabled.count(reg[i].name) == 0;
        os << "| " << (i + 1)
           << " | " << group_label(reg[i].group)
           << " | `" << reg[i].name << "`"
           << " | " << (on ? "✓" : "✗")
           << " | " << reg[i].short_desc
           << " |\n";
    }
    os << "\n_Global MCP tools enabled:_ " << (gs.mcp_tools_enabled ? "yes" : "no") << "\n";
    if (!gs.disabled_mcp_servers.empty()) {
        os << "_Global disabled MCP servers:_ ";
        for (size_t i = 0; i < gs.disabled_mcp_servers.size(); ++i) {
            if (i) os << ", ";
            os << "`" << gs.disabled_mcp_servers[i] << "`";
        }
        os << "\n";
    }
    os << "\n";
    return 0;
}

static const char* skill_source_label(media::llm::skills::SkillSource s) {
    switch (s) {
    case media::llm::skills::SkillSource::Roaming: return "roaming";
    case media::llm::skills::SkillSource::Workspace: return "workspace";
    }
    return "unknown";
}

static int cmd_llm_info_skills(std::ostream& os, bool json_out)
{
    media::runtime_settings::AgentSkillsSettings cfg;
    std::string serr;
    (void)media::runtime_settings::load_agent_skills_settings(cfg, serr);
    media::llm::skills::SkillPolicy policy;
    policy.enabled = cfg.enabled;
    policy.roaming_enabled = cfg.roaming_enabled;
    policy.workspace_enabled = cfg.workspace_enabled;
    policy.pinned = cfg.pinned;
    policy.disabled = cfg.disabled;
    const auto snap = media::llm::skills::discover_skills(policy, std::filesystem::current_path().string());

    if (json_out) {
        nlohmann::json out;
        out["policy"] = nlohmann::json{
            {"enabled", snap.policy.enabled},
            {"roaming_enabled", snap.policy.roaming_enabled},
            {"workspace_enabled", snap.policy.workspace_enabled},
            {"pinned", snap.policy.pinned},
            {"disabled", snap.policy.disabled},
        };
        out["roots"] = nlohmann::json{
            {"roaming", snap.roaming_root.string()},
            {"workspace", snap.workspace_root.string()},
        };
        out["skills"] = nlohmann::json::array();
        for (const auto& e : snap.entries) {
            out["skills"].push_back({
                {"name", e.name},
                {"description", e.description},
                {"source", skill_source_label(e.source)},
                {"available", e.available},
                {"active", e.active},
                {"always", e.always},
                {"pinned", e.pinned},
                {"disabled", e.disabled},
                {"missing_requirements", e.missing_requirements},
                {"path", e.skill_md_path.string()},
            });
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    os << "# Agent skills\n\n";
    if (!serr.empty())
        os << "_Skill settings load warning: " << serr << "_\n\n";
    os << "| Root | Path |\n|:---|:---|\n";
    os << "| roaming | `" << snap.roaming_root.string() << "` |\n";
    os << "| workspace | `" << snap.workspace_root.string() << "` |\n\n";
    os << "| Policy | Value |\n|:---|:---|\n";
    os << "| enabled | " << (snap.policy.enabled ? "yes" : "no") << " |\n";
    os << "| roaming_enabled | " << (snap.policy.roaming_enabled ? "yes" : "no") << " |\n";
    os << "| workspace_enabled | " << (snap.policy.workspace_enabled ? "yes" : "no") << " |\n";
    os << "| pinned | " << (snap.policy.pinned.empty() ? "_(none)_" : ("`" + snap.policy.pinned.front() + (snap.policy.pinned.size() > 1 ? ", ...`" : "`"))) << " |\n";
    os << "| disabled | " << (snap.policy.disabled.empty() ? "_(none)_" : ("`" + snap.policy.disabled.front() + (snap.policy.disabled.size() > 1 ? ", ...`" : "`"))) << " |\n\n";
    if (snap.entries.empty()) {
        os << "_No skills discovered._\n";
        return 0;
    }
    os << "| Name | Source | Available | Active | Description |\n";
    os << "|:---|:---|:---:|:---:|:---|\n";
    for (const auto& e : snap.entries) {
        os << "| `" << e.name << "` | " << skill_source_label(e.source)
           << " | " << (e.available ? "✓" : "✗")
           << " | " << (e.active ? "✓" : "✗")
           << " | " << e.description << " |\n";
        if (!e.available && !e.missing_requirements.empty()) {
            os << "|  |  |  |  | missing: `";
            for (std::size_t i = 0; i < e.missing_requirements.size(); ++i) {
                if (i) os << ", ";
                os << e.missing_requirements[i];
            }
            os << "` |\n";
        }
    }
    os << "\n";
    return 0;
}

// ── llm info providers ────────────────────────────────────────────────────────

static int cmd_llm_info_providers(std::ostream& os, const media::runtime_settings::ProviderMap& pm, bool json_out)
{
    if (json_out) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [name, e] : pm) {
            arr.push_back({{"name", name},
                           {"api_key_set", !e.api_key.empty()},
                           {"base_url", e.base_url},
                           {"default_model", e.default_model}});
        }
        std::cout << arr.dump(2) << "\n";
        return 0;
    }

    os << "# Configured providers\n\n";
    if (pm.empty()) {
        os << "_None configured._\n";
        return 0;
    }

    os << "| Name | Key | Base URL | Default model |\n";
    os << "|:---|:---:|:---|:---|\n";
    for (const auto& [name, e] : pm) {
        const std::string url   = e.base_url.empty()      ? "_(default)_"   : ("`" + e.base_url + "`");
        const std::string model = e.default_model.empty() ? "_(none)_"      : ("`" + e.default_model + "`");
        const std::string key   = e.api_key.empty()       ? "✗" : "✓";
        os << "| `" << name << "` | " << key << " | " << url << " | " << model << " |\n";
    }
    os << "\n";
    return 0;
}

// ── llm info models ───────────────────────────────────────────────────────────

static int cmd_llm_info_models(std::ostream& os, PmImageCliState&                            st,
                                const media::runtime_settings::ProviderMap& pm)
{
    std::string provider = st.llm_info_models_provider;
    for (auto& c : provider)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Resolve credentials from app settings (same path as provider models list).
    std::string api_key;
    std::string base_url;
    media_cli::apply_image_ai_credentials_from_app(provider, false, api_key, base_url);

    const bool force_refresh = st.llm_info_models_no_cache;

    // ── pixlwiz ──────────────────────────────────────────────────────────────
    if (provider == "pixlwiz") {
        std::vector<media::pixlwiz_cli::PixlWizModelRow> rows;
        std::string err;
        if (!media::pixlwiz_cli::list_pixlwiz_catalog_models(api_key, base_url, rows, err, force_refresh)) {
            std::cerr << "llm info models: pixlwiz: " << err << "\n";
            return 1;
        }
        if (st.llm_info_json) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : rows) {
                arr.push_back({{"id", r.id},
                               {"name", r.name},
                               {"description", r.description},
                               {"url", r.open_url}});
            }
            std::cout << arr.dump(2) << "\n";
            return 0;
        }
        const std::string ep = base_url.empty() ? "https://llm.polymech.info/models" : base_url + "/models";
        os << "# PixlWiz proxy models\n\n";
        os << "_" << rows.size() << " models — `" << ep << "`_\n\n";
        for (const auto& r : rows) {
            os << "- **`" << r.id << "`**";
            if (!r.name.empty() && r.name != r.id)
                os << " (" << r.name << ")";
            os << "\n";
            if (!r.description.empty()) {
                std::string d = r.description;
                if (d.size() > 120) { d.resize(117); d += "..."; }
                os << "  " << d << "\n";
            }
        }
        os << "\n";
        return 0;
    }

    // ── openai ───────────────────────────────────────────────────────────────
    if (provider == "openai") {
        std::vector<media::openai_cli::OpenAIModelRow> rows;
        std::string err;
        if (!media::openai_cli::list_openai_models(api_key, base_url, rows, err, force_refresh)) {
            std::cerr << "llm info models: openai: " << err << "\n";
            return 1;
        }
        if (st.llm_info_json) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : rows)
                arr.push_back({{"id", r.id}, {"owned_by", r.owned_by}});
            std::cout << arr.dump(2) << "\n";
            return 0;
        }
        const std::string ep = base_url.empty() ? "https://api.openai.com/v1/models" : base_url + "/models";
        os << "# OpenAI models\n\n";
        os << "_" << rows.size() << " models — `" << ep << "`_\n\n";
        for (const auto& r : rows) {
            os << "- **`" << r.id << "`**";
            if (!r.owned_by.empty()) os << "  _(owned\\_by: " << r.owned_by << ")_";
            os << "\n";
        }
        os << "\n";
        return 0;
    }

    // ── openrouter ───────────────────────────────────────────────────────────
    if (provider == "openrouter") {
        std::vector<media::openrouter_cli::OpenRouterCatalogModelRow> rows;
        std::string err;
        if (!media::openrouter_cli::list_openrouter_catalog_models(api_key, base_url, rows, err, force_refresh)) {
            std::cerr << "llm info models: openrouter: " << err << "\n";
            return 1;
        }
        if (st.llm_info_json) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : rows)
                arr.push_back({{"id", r.id}, {"name", r.name}, {"description", r.description}});
            std::cout << arr.dump(2) << "\n";
            return 0;
        }
        constexpr int k_max_shown = 60;
        os << "# OpenRouter models\n\n";
        os << "_" << rows.size() << " total";
        if (static_cast<int>(rows.size()) > k_max_shown)
            os << " — showing first " << k_max_shown << "; use `--json` for the full list";
        os << "_\n\n";
        int shown = 0;
        for (const auto& r : rows) {
            os << "- **`" << r.id << "`**\n";
            if (!r.detail_head.empty())
                os << "  " << r.detail_head << "\n";
            if (++shown >= k_max_shown) break;
        }
        os << "\n";
        return 0;
    }

    // ── replicate ────────────────────────────────────────────────────────────
    if (provider == "replicate") {
        os << "# Replicate models\n\n"
           << "_The Replicate catalog is paginated and too large to display inline._\n\n"
           << "Use `provider models list --provider replicate --api-key <key>` for the full JSON response.\n";
        return 0;
    }

    // ── generic: look up in ProviderMap ──────────────────────────────────────
    const media::settings_types::ProviderEntry* entry = nullptr;
    for (const auto& [name, e] : pm) {
        std::string lower = name;
        for (auto& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == provider) { entry = &e; break; }
    }
    if (!entry) {
        std::cerr << "llm info models: provider \"" << st.llm_info_models_provider
                  << "\" not found in app settings.\n"
                  << "  Run `llm info providers` to see configured names.\n"
                  << "  Providers with live catalog support: openai, openrouter, pixlwiz\n";
        return 1;
    }
    if (st.llm_info_json) {
        std::cout << nlohmann::json{
            {"provider",      st.llm_info_models_provider},
            {"api_key_set",   !entry->api_key.empty()},
            {"base_url",      entry->base_url},
            {"default_model", entry->default_model},
            {"note",          "Live catalog not available; showing configured defaults only."}
        }.dump(2) << "\n";
        return 0;
    }
    os << "# Provider — `" << st.llm_info_models_provider << "`\n\n";
    os << "| Setting | Value |\n|:---|:---|\n";
    os << "| api_key | " << (entry->api_key.empty() ? "✗ _(not set)_" : "✓ _(set)_") << " |\n";
    os << "| base_url | " << (entry->base_url.empty() ? "_(default)_" : ("`" + entry->base_url + "`")) << " |\n";
    os << "| default_model | " << (entry->default_model.empty() ? "_(none)_" : ("`" + entry->default_model + "`")) << " |\n\n";
    os << "_Live model catalog not supported for this provider. Providers with catalog support: `openai`, `openrouter`, `pixlwiz`_\n";
    return 0;
}

} // namespace

int pm_image_cmd_llm_info(CLI::App& app, PmImageCliState& st) {
    (void)app;
    std::string load_err;
    media::runtime_settings::ChatProviderSettings cs;
    const bool chat_ok = media::runtime_settings::load_chat_provider(cs, load_err);

    std::string img_provider;
    std::string img_model;
    media_cli::apply_image_ai_cli_defaults_from_app(img_provider, img_model, false, false);

    std::string vid_provider;
    std::string vid_model;
    {
        nlohmann::json empty_tool_opts = nlohmann::json::object();
        media::llm::apply_chat_video_defaults_for_tool_options(vid_provider, vid_model, empty_tool_opts);
    }

    // Effective text LLM defaults (same guard chain as `llm agent` when flags are omitted).
    std::string ag_router  = cs.router;
    std::string ag_model   = cs.model;
    std::string ag_base;
    std::string ag_key;
    int         ag_timeout = cs.timeout_ms;
    int         ag_max     = cs.max_iterations;
    if (ag_timeout <= 0) ag_timeout  = 60'000;
    if (ag_max <= 0) ag_max           = 8;
    media_cli::fill_chat_llm_credentials_from_app_settings(ag_key, ag_base, ag_router);

    // duplicates --meta-compare-json-llm (empty CLI flags).
    std::string dup_router  = cs.router;
    std::string dup_model   = cs.model;
    std::string dup_base;
    std::string dup_key;
    int         dup_timeout = cs.timeout_ms;
    if (dup_timeout <= 0) dup_timeout  = 60'000;
    media_cli::fill_chat_llm_credentials_from_app_settings(dup_key, dup_base, dup_router);

    media::runtime_settings::ProviderMap pm;
    std::string                         pm_err;
    (void)media::runtime_settings::load_providers(pm, pm_err);

    // ── sub-subcommand dispatch ───────────────────────────────────────────────
    if (st.llm_info_tools_cmd && st.llm_info_tools_cmd->parsed()) {
        std::ostringstream oss;
        const int rc = cmd_llm_info_tools(oss, st.llm_info_json);
        if (!st.llm_info_json) {
            bool want_render = (st.llm_info_markdown == "render") ||
                               (st.llm_info_markdown != "plain" && cli_tty::stdout_is_tty());
            bool use_color   = (st.llm_info_color == "always") ||
                               (st.llm_info_color != "never" && cli_tty::stdout_is_tty()
                                && !cli_tty::env_requests_plain_output());
#if defined(_WIN32)
            if (want_render && use_color)
                cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif
            std::string out = oss.str();
            if (want_render) {
                html::MdOptions mdopts{};
                int w = cli_tty::terminal_width_columns();
                if (w <= 0) w = 80;
                out = html::markdown_to_terminal(out, mdopts, use_color, w);
            }
            std::cout << out;
            if (out.empty() || out.back() != '\n') std::cout << '\n';
        }
        return rc;
    }
    if (st.llm_info_skills_cmd && st.llm_info_skills_cmd->parsed()) {
        std::ostringstream oss;
        const int rc = cmd_llm_info_skills(oss, st.llm_info_json);
        if (!st.llm_info_json) {
            bool want_render = (st.llm_info_markdown == "render") ||
                               (st.llm_info_markdown != "plain" && cli_tty::stdout_is_tty());
            bool use_color   = (st.llm_info_color == "always") ||
                               (st.llm_info_color != "never" && cli_tty::stdout_is_tty()
                                && !cli_tty::env_requests_plain_output());
#if defined(_WIN32)
            if (want_render && use_color)
                cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif
            std::string out = oss.str();
            if (want_render) {
                html::MdOptions mdopts{};
                int w = cli_tty::terminal_width_columns();
                if (w <= 0) w = 80;
                out = html::markdown_to_terminal(out, mdopts, use_color, w);
            }
            std::cout << out;
            if (out.empty() || out.back() != '\n') std::cout << '\n';
        }
        return rc;
    }
    if (st.llm_info_providers_cmd && st.llm_info_providers_cmd->parsed()) {
        std::ostringstream oss;
        const int rc = cmd_llm_info_providers(oss, pm, st.llm_info_json);
        if (!st.llm_info_json) {
            bool want_render = (st.llm_info_markdown == "render") ||
                               (st.llm_info_markdown != "plain" && cli_tty::stdout_is_tty());
            bool use_color   = (st.llm_info_color == "always") ||
                               (st.llm_info_color != "never" && cli_tty::stdout_is_tty()
                                && !cli_tty::env_requests_plain_output());
#if defined(_WIN32)
            if (want_render && use_color)
                cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif
            std::string out = oss.str();
            if (want_render) {
                html::MdOptions mdopts{};
                int w = cli_tty::terminal_width_columns();
                if (w <= 0) w = 80;
                out = html::markdown_to_terminal(out, mdopts, use_color, w);
            }
            std::cout << out;
            if (out.empty() || out.back() != '\n') std::cout << '\n';
        }
        return rc;
    }
    if (st.llm_info_models_cmd && st.llm_info_models_cmd->parsed()) {
        std::ostringstream oss;
        const int rc = cmd_llm_info_models(oss, st, pm);
        if (!st.llm_info_json) {
            bool want_render = (st.llm_info_markdown == "render") ||
                               (st.llm_info_markdown != "plain" && cli_tty::stdout_is_tty());
            bool use_color   = (st.llm_info_color == "always") ||
                               (st.llm_info_color != "never" && cli_tty::stdout_is_tty()
                                && !cli_tty::env_requests_plain_output());
#if defined(_WIN32)
            if (want_render && use_color)
                cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif
            std::string out = oss.str();
            if (want_render) {
                html::MdOptions mdopts{};
                int w = cli_tty::terminal_width_columns();
                if (w <= 0) w = 80;
                out = html::markdown_to_terminal(out, mdopts, use_color, w);
            }
            std::cout << out;
            if (out.empty() || out.back() != '\n') std::cout << '\n';
        }
        return rc;
    }

    // Image recognition (vision) — explicit chat fields only.
    std::string ir_provider = cs.image_recognition_provider;
    std::string ir_model    = cs.image_recognition_model;

    auto key_set = [](const std::string& s) -> bool { return !s.empty(); };

    nlohmann::json mcp_probe;
    if (st.llm_info_no_mcp_probe) {
        mcp_probe = nlohmann::json{{"skipped", true},
                                   {"note",
                                    "Probe skipped (--no-mcp-probe): no mcp.json handshake or tools/list; embedded "
                                    "in-process MCP (if enabled) is unchanged."},
                                   {"servers", nlohmann::json::array()}};
    } else {
        try {
            mcp_probe = media::llm::mcp::probe_mcp_config({});
        } catch (const std::exception& e) {
            mcp_probe = nlohmann::json{
                {"skipped", true},
                {"probe_error", e.what()},
                {"note",
                 "MCP probe failed unexpectedly; showing settings without live MCP handshake/tools-list."},
                {"servers", nlohmann::json::array()}};
        } catch (...) {
            mcp_probe = nlohmann::json{
                {"skipped", true},
                {"probe_error", "unknown exception"},
                {"note",
                 "MCP probe failed unexpectedly; showing settings without live MCP handshake/tools-list."},
                {"servers", nlohmann::json::array()}};
        }
    }

    if (st.llm_info_json) {
        nlohmann::json j;
        j["settings_file"] = settings_file_hint();
        if (!load_err.empty())
            j["settings_warning"] = load_err;
        j["settings_chat_ok"] = chat_ok;

        j["chat"] = {{"router", cs.router},
                     {"model", cs.model},
                     {"timeout_ms", cs.timeout_ms},
                     {"max_iterations", cs.max_iterations},
                     {"api_key_set", key_set(ag_key)},
                     {"image_provider", cs.image_provider},
                     {"image_model", cs.image_model},
                     {"image_recognition_provider", cs.image_recognition_provider},
                     {"image_recognition_model", cs.image_recognition_model},
                     {"video_provider", cs.video_provider},
                     {"video_model", cs.video_model},
                     {"stt_provider", cs.stt_provider},
                     {"stt_model", cs.stt_model},
                     {"tts_provider", cs.tts_provider},
                     {"tts_model", cs.tts_model}};

        j["image_tools_resolved"] = {{"provider", img_provider}, {"model", img_model}};
        j["openapi_input_pre_test"]     = openapi_input_pre_test_json(img_provider, img_model);
        j["video_tools_resolved"]       = {{"provider", vid_provider}, {"model", vid_model}};
        j["openapi_input_pre_test_video"] = openapi_input_pre_test_json(vid_provider, vid_model);
        j["image_tools_default_source"] = "explicit chat.image_provider/image_model or command/tool overrides";
        j["video_tools_default_source"] = "explicit chat.video_provider/video_model or tool overrides";
        j["image_recognition_resolved"] = {{"provider", ir_provider}, {"model", ir_model}};
        j["image_recognition_default_source"] = "explicit chat.image_recognition_provider/image_recognition_model";

        j["providers"] = nlohmann::json::object();
        for (const auto& [name, e] : pm) {
            nlohmann::json row;
            row["api_key_set"]   = key_set(e.api_key);
            row["base_url"]      = e.base_url;
            row["default_model"] = e.default_model;
            j["providers"][name] = std::move(row);
        }

        j["llm_agent_effective"] = {{"router", ag_router},
                                     {"model", ag_model},
                                     {"base_url", ag_base},
                                     {"timeout_ms", ag_timeout},
                                     {"max_iterations", ag_max}};

        j["duplicates_meta_json_llm_effective"] = {{"router", dup_router},
                                                    {"model", dup_model},
                                                    {"base_url", dup_base},
                                                    {"timeout_ms", dup_timeout}};

        j["cli_overrides"] = nlohmann::json::array();
        j["cli_overrides"].push_back(
            nlohmann::json{{"command", "llm agent"},
                           {"overrides", "--router, --model, --api-key, --base-url, --timeout-ms, --max-iter"}});
        j["cli_overrides"].push_back(
            nlohmann::json{{"command", "find (with --llm)"},
                           {"overrides", "--provider, --model, --api-key"},
                           {"notes", "Image-side cataloguer + judge; uses chat.image_provider/model when flags omitted."}});
        j["cli_overrides"].push_back(
            nlohmann::json{
                {"command", "transform"},
                {"overrides", "--provider, --model, --api-key"},
            });
        j["cli_overrides"].push_back(
            nlohmann::json{
                {"command", "meta"},
                {"overrides", "--provider, --model, --api-key"},
            });
        j["cli_overrides"].push_back(
            nlohmann::json{
                {"command", "create"},
                {"overrides", "--provider, --model, --api-key"},
            });
        j["cli_overrides"].push_back(nlohmann::json{
            {"command", "duplicates (only with --meta-compare-json-llm)"},
            {"overrides", "--llm-router, --llm-model, --llm-api-key, --llm-base-url, --llm-timeout-ms"},
            {"notes", "Text chat router/model; same fallbacks as llm agent when those flags are omitted."}});

        j["mcp"] = mcp_probe;

        nlohmann::json tool_arr = nlohmann::json::array();
        for (const auto& t : media::llm::path::tool_catalog())
            tool_arr.push_back({{"name", t.name}, {"description", t.description}});
        j["builtin_tools"] = std::move(tool_arr);

        std::cout << j.dump(2) << "\n";
        return 0;
    }

    std::ostringstream oss;

    // helpers
    auto val = [](const std::string& s, const char* empty_label = "_(empty)_") -> std::string {
        return s.empty() ? empty_label : ("`" + s + "`");
    };
    auto val_int = [](int v) -> std::string {
        return "`" + std::to_string(v) + "`";
    };

    // ── Header ────────────────────────────────────────────────────────────────
    oss << "# LLM / AI settings\n\n";
    oss << "**File:** `" << settings_file_hint() << "`\n";
#if defined(_WIN32)
    oss << "\n> On Windows the app reloads this file when its timestamp changes, "
           "so long-running processes stay in sync with edits or copies to AppData.\n";
#endif
    if (!chat_ok && !load_err.empty())
        oss << "\n> **Warning:** " << load_err << "\n";

    // ── Chat ──────────────────────────────────────────────────────────────────
    oss << "\n---\n\n## Chat\n\n";
    oss << "_Key `chat` in settings — used by `llm agent`, Chat UI, "
           "`duplicates --meta-compare-json-llm`_\n\n";
    oss << "| Setting | Value |\n|:---|:---|\n";
    oss << "| router | " << val(cs.router) << " |\n";
    oss << "| model | " << val(cs.model) << " |\n";
    oss << "| timeout\\_ms | " << val_int(cs.timeout_ms) << " |\n";
    oss << "| max\\_iterations | " << val_int(cs.max_iterations) << " |\n";
    oss << "| api\\_key | " << (key_set(ag_key) ? "✓ _(set)_" : "✗ _(missing)_") << " |\n";
    oss << "| image\\_provider | " << val(cs.image_provider) << " |\n";
    oss << "| image\\_model | " << val(cs.image_model) << " |\n";
    oss << "| image\\_recognition\\_provider | " << val(cs.image_recognition_provider) << " |\n";
    oss << "| image\\_recognition\\_model | " << val(cs.image_recognition_model) << " |\n";
    oss << "| video\\_provider | " << val(cs.video_provider) << " |\n";
    oss << "| video\\_model | " << val(cs.video_model) << " |\n";

    // ── Voice & Audio ─────────────────────────────────────────────────────────
    oss << "\n### Voice & Audio\n\n";
    oss << "| Setting | Value |\n|:---|:---|\n";
    {
        const std::string stt_p = cs.stt_provider.empty() ? "_(empty → pixlwiz)_"              : ("`" + cs.stt_provider + "`");
        const std::string stt_m = cs.stt_model.empty()    ? "_(empty → pixlwiz-speech-to-text)_" : ("`" + cs.stt_model + "`");
        const std::string tts_p = cs.tts_provider.empty() ? "_(empty → pixlwiz)_"              : ("`" + cs.tts_provider + "`");
        const std::string tts_m = cs.tts_model.empty()    ? "_(empty → pixlwiz-speech)_"       : ("`" + cs.tts_model + "`");
        oss << "| stt\\_provider | " << stt_p << " |\n";
        oss << "| stt\\_model | " << stt_m << " |\n";
        oss << "| tts\\_provider | " << tts_p << " |\n";
        oss << "| tts\\_model | " << tts_m << " |\n";
    }

    // ── Image — resolved ──────────────────────────────────────────────────────
    oss << "\n---\n\n## Image — resolved defaults\n\n";
    oss << "_`transform`, `meta`, `find --llm`, `create`, chat image tools — "
           "when `--provider`/`--model` are omitted_\n\n";
    oss << "| | |\n|:---|:---|\n";
    oss << "| provider | " << val(img_provider) << " |\n";
    oss << "| model | " << val(img_model) << " |\n";
    print_openapi_input_pre_test_text(oss, img_provider, img_model);
#ifdef FEATURE_COMMAND_VIDEO
    // ── Video — resolved ──────────────────────────────────────────────────────
    oss << "\n---\n\n## Video — resolved defaults\n\n";
    oss << "_`create_video` path tool when no tool JSON overrides are set_\n\n";
    oss << "| | |\n|:---|:---|\n";
    oss << "| provider | " << val(vid_provider) << " |\n";
    oss << "| model | " << val(vid_model) << " |\n";
    print_openapi_input_pre_test_text(oss, vid_provider, vid_model);
#endif
    // ── Image recognition — resolved ─────────────────────────── ────────────────
    oss << "\n---\n\n## Image recognition — resolved\n\n";
    oss << "_`describe`, `classify`, vision paths — explicit `chat.image_recognition_*` fields only_\n\n";
    oss << "| | |\n|:---|:---|\n";
    oss << "| provider | " << val(ir_provider) << " |\n";
    oss << "| model | " << val(ir_model) << " |\n";

    // ── llm agent — effective ─────────────────────────────────────────────────
    oss << "\n---\n\n## llm agent — effective defaults\n\n";
    oss << "_Applies when `--router`, `--model`, `--timeout-ms`, `--max-iter`, `--base-url` are omitted_\n\n";
    oss << "| Setting | Value |\n|:---|:---|\n";
    oss << "| router | " << val(ag_router) << " |\n";
    oss << "| model | " << val(ag_model) << " |\n";
    oss << "| base\\_url | " << val(ag_base) << " |\n";
    oss << "| timeout\\_ms | " << val_int(ag_timeout) << " |\n";
    oss << "| max\\_iterations | " << val_int(ag_max) << " |\n";
#ifdef FEATURE_COMMAND_DUPLICATES
    // ── duplicates ────────────────────────────────────────────────────────────
    oss << "\n---\n\n## duplicates + `--meta-compare-json-llm`\n\n";
    oss << "_Applies when `--llm-*` flags are omitted_\n\n";
    oss << "| Setting | Value |\n|:---|:---|\n";
    oss << "| router | " << val(dup_router) << " |\n";
    oss << "| model | " << val(dup_model) << " |\n";
    oss << "| base\\_url | " << val(dup_base) << " |\n";
    oss << "| timeout\\_ms | " << val_int(dup_timeout) << " |\n";
#endif
    // ── Per-command overrides ─────────────────────────────────────────────────
    oss << "\n---\n\n## Per-command overrides\n\n";
    oss << "_Global app defaults above apply when these flags are omitted_\n\n";
    oss << "| Command | Flags |\n|:---|:---|\n";
    oss << "| `find --llm` | `--provider` `--model` `--api-key` |\n";
    oss << "| `transform` | `--provider` `--model` `--api-key` |\n";
    oss << "| `meta` | `--provider` `--model` `--api-key` |\n";
    oss << "| `create` | `--provider` `--model` `--api-key` |\n";
    oss << "| `duplicates --meta-compare-json-llm` | `--llm-router` `--llm-model` `--llm-api-key` `--llm-base-url` `--llm-timeout-ms` |\n";
    oss << "| `llm agent` | `--router` `--model` `--api-key` `--base-url` `--timeout-ms` `--max-iter` |\n";

    print_mcp_probe_text(oss, mcp_probe);

    // ── Built-in tools ────────────────────────────────────────────────────────
    {
        const auto catalog = media::llm::path::tool_catalog();
        oss << "\n---\n\n## Built-in agent tools\n\n";
        oss << "_" << catalog.size() << " tools — use `llm info tools` for full descriptions_\n\n";
        oss << "| # | Name |\n|:--|:---|\n";
        int i = 0;
        for (const auto& t : catalog)
            oss << "| " << ++i << " | `" << t.name << "` |\n";
        oss << "\n";
    }

    // ── markdown rendering ────────────────────────────────────────────────────
    bool want_render = (st.llm_info_markdown == "render") ||
                       (st.llm_info_markdown != "plain" && cli_tty::stdout_is_tty());
    bool use_color   = (st.llm_info_color == "always") ||
                       (st.llm_info_color != "never" && cli_tty::stdout_is_tty()
                        && !cli_tty::env_requests_plain_output());
#if defined(_WIN32)
    if (want_render && use_color)
        cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif
    std::string out = oss.str();
    if (want_render) {
        html::MdOptions mdopts{};
        int w = cli_tty::terminal_width_columns();
        if (w <= 0) w = 80;
        out = html::markdown_to_terminal(out, mdopts, use_color, w);
    }
    std::cout << out;
    if (out.empty() || out.back() != '\n') std::cout << '\n';

    return 0;
}
