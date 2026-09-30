#include "pm_image_cli_helpers.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>

#include <nlohmann/json.hpp>

#include "constants.hpp"
#include "logger/logger.h"
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
#include "win/ui_next/ui_log_file.hpp"
#endif
#if defined(_WIN32)
#include "win/settings_store.hpp"
#endif
#include "core/app_image_provider.hpp"
#include "core/settings_runtime.hpp"
#include "core/transform.hpp"

namespace media_cli {

std::string join_src_semicolons(const std::vector<std::string> &v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ';';
        s += v[i];
    }
    return s;
}

void parse_agent_disable_tools(const std::string &raw, bool no_tools, std::vector<std::string> &out) {
    out.clear();
    if (raw.empty() || no_tools) {
        if (no_tools && !raw.empty())
            std::cerr
                << "llm agent: --disable-tools is ignored when --no-tools is set\n";
        return;
    }
    // Current tool IDs — mirrors path_tool_catalog.cpp tool_catalog() order.
    static const char* k_known[] = {
        "list_images", "file_glob", "file_read", "file_search",
        "image_resize", "image_transform", "image_create", "create_video",
        "image_understand", "image_from_camera",
        "write_file", "speak",
        "schedule_at", "schedule_in", "schedule_every", "schedule_cancel", "schedule_list",
        "memory_read", "memory_write", "memory_append_event",
    };
    auto is_known = [](const std::string& l) {
        for (const char* k : k_known)
            if (l == k) return true;
        return false;
    };
    for (size_t i = 0; i < raw.size();) {
        size_t j = i;
        while (j < raw.size() && raw[j] != ',' && raw[j] != ';') ++j;
        std::string tok = raw.substr(i, j - i);
        const auto b = tok.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) {
            if (j < raw.size()) ++j;
            i = j;
            continue;
        }
        const auto e  = tok.find_last_not_of(" \t\r\n");
        tok           = tok.substr(b, e - b + 1);
        for (auto& c : tok)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tok.empty()) {
            if (j < raw.size()) ++j;
            i = j;
            continue;
        }
        // CLI typo guard: when users pass `--disable-tools --single-turn`,
        // some shells/parsers can forward `--single-turn` as the option value.
        // Ignore option-shaped tokens here instead of treating them as tool ids.
        if (tok.rfind("--", 0) == 0) {
            if (j < raw.size()) ++j;
            i = j;
            continue;
        }
        if (!is_known(tok)) {
            std::cerr << "llm agent: unknown tool in --disable-tools: " << tok << "\n"
                      << "  valid tool ids: ";
            bool first = true;
            for (const char* k : k_known) {
                if (!first) std::cerr << ", ";
                std::cerr << k;
                first = false;
            }
            std::cerr << "\n";
            // Still add it — tool_catalog_excluding ignores unrecognised names.
            out.push_back(std::move(tok));
        } else {
            out.push_back(std::move(tok));
        }
        if (j < raw.size()) ++j;
        i = j;
    }
}

void trim_surrounding_quotes(std::string &s) {
    while (!s.empty() && (s.front() == '\'' || s.front() == '"')) s.erase(0, 1);
    while (!s.empty() && (s.back() == '\'' || s.back() == '"')) s.pop_back();
}

bool resolve_transform_preset_id(const std::string& id_in, media::TransformOptions& topts, std::string& err) {
    err.clear();
#if !defined(_WIN32)
    (void)id_in;
    (void)topts;
    err = "--preset-id from app settings is only supported on Windows (use --prompt on this platform)";
    return false;
#else
    std::string                                 id = id_in;
    trim_surrounding_quotes(id);
    std::vector<media::settings::ExplorerPreset> presets;
    std::string                                 perr;
    if (!media::settings::load_explorer_presets(presets, perr)) {
        err = perr;
        return false;
    }
    for (const auto& p : presets) {
        if (p.id == id && p.op == "transform") {
            media::apply_transform_options_from_json(p.args, topts);
            return true;
        }
    }
    if (id.size() > 5 && id.compare(0, 5, "chat-") == 0) {
        nlohmann::json cw;
        if (!media::settings::load_chat_web(cw, perr) || !cw.is_object()) {
            err = perr.empty() ? std::string("load chat_web") : perr;
            return false;
        }
        const std::string qid = id.substr(5);
        if (!cw.contains("quick_actions") || !cw["quick_actions"].is_array()) {
            err = "chat_web.quick_actions missing in settings";
            return false;
        }
        for (const auto& el : cw["quick_actions"]) {
            if (!el.is_object() || !el.contains("id") || !el["id"].is_string()) continue;
            const std::string el_id = el["id"].get<std::string>();
            if (el_id != qid && el_id != id) continue;
            nlohmann::json args = nlohmann::json::object();
            if (el.contains("prompt") && el["prompt"].is_string()) args["prompt"] = el["prompt"].get<std::string>();
            media::apply_transform_options_from_json(args, topts);
            return true;
        }
    }
    err = "unknown --preset-id: " + id;
    return false;
#endif
}

void apply_image_ai_cli_defaults_from_app(std::string& provider, std::string& model, bool user_set_provider,
                                         bool user_set_model) {
    std::string err;
    media::runtime_settings::ChatProviderSettings cs;
    if (media::runtime_settings::load_chat_provider(cs, err)) {
        if (!user_set_provider && provider.empty() && !cs.image_provider.empty()) provider = cs.image_provider;
        if (!user_set_model && model.empty() && !cs.image_model.empty()) model = cs.image_model;
    }
}

void apply_image_recognition_cli_defaults_from_app(std::string& provider, std::string& model,
                                                   bool user_set_provider, bool user_set_model) {
    std::string err;
    media::runtime_settings::ChatProviderSettings cs;
    if (media::runtime_settings::load_chat_provider(cs, err)) {
        if (!user_set_provider && provider.empty() && !cs.image_recognition_provider.empty())
            provider = cs.image_recognition_provider;
        if (!user_set_model && model.empty() && !cs.image_recognition_model.empty())
            model = cs.image_recognition_model;
    }
}

void apply_image_ai_credentials_from_app(const std::string& provider, bool dry_run, std::string& api_key,
                                         std::string& base_url) {
    media::runtime_settings::merge_provider_credentials(provider, dry_run, api_key, base_url);
}

void fill_chat_llm_credentials_from_app_settings(std::string& api_key, std::string& base_url,
                                                 const std::string& router) {
    if (router.empty() || (!api_key.empty() && !base_url.empty()))
        return;
    media::runtime_settings::merge_provider_credentials(router, false, api_key, base_url);
}

void fill_chat_llm_api_key_from_app_settings(std::string& api_key, const std::string& router) {
    std::string ignored_base_url;
    fill_chat_llm_credentials_from_app_settings(api_key, ignored_base_url, router);
}

int fail_image_ai_requires_provider_model(const char* subcmd, const std::string& provider, const std::string& model) {
    const std::string explain =
        std::string(subcmd)
        + ": missing provider or model after resolving settings — set Chat image_provider and image_model "
          "in app settings, or pass --provider and --model. "
          "Settings directory: %APPDATA%\\" + std::string(pm::brand::k_config_subpath_u8) + "\\";
    std::cerr << explain << "\n";
    std::cerr << subcmd << ": resolved provider=\"" << provider << "\" model=\"" << model << "\"\n";
    logger::error(explain);
    logger::error(std::string(subcmd) + ": resolved provider=\"" + provider + "\" model=\"" + model + "\"");
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    pmui::ui_log_file_begin_session();
    pmui::ui_log_file_event(explain.c_str());
    pmui::ui_log_file_eventf("%s: resolved provider=\"%s\" model=\"%s\"", subcmd, provider.c_str(), model.c_str());
    pmui::ui_log_file_end_session();
#endif
    return 1;
}

} // namespace media_cli
