#include "stdafx.h"
#include "win/custom_commands_host.hpp"

#include "core/settings_runtime.hpp"
#include "win/settings_store.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace pmui::custom_commands_host {

namespace {

std::string trim_ascii(std::string s)
{
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

nlohmann::json normalize_commands_document(nlohmann::json doc)
{
    if (!doc.is_object())
        doc = default_commands_doc();
    if (!doc.contains("version"))
        doc["version"] = 1;
    if (!doc.contains("ribbon") || !doc["ribbon"].is_object())
        doc["ribbon"] = nlohmann::json::object();
    if (!doc["ribbon"].contains("groups") || !doc["ribbon"]["groups"].is_array())
        doc["ribbon"]["groups"] = nlohmann::json::array();
    return doc;
}

} // namespace

nlohmann::json default_commands_doc()
{
    return nlohmann::json{{"version", 1}, {"ribbon", {{"groups", nlohmann::json::array()}}}};
}

std::string normalize_command_json_text(std::string json_text)
{
    if (json_text.size() >= 3 && static_cast<unsigned char>(json_text[0]) == 0xEF &&
        static_cast<unsigned char>(json_text[1]) == 0xBB && static_cast<unsigned char>(json_text[2]) == 0xBF) {
        json_text.erase(0, 3);
    }
    return trim_ascii(std::move(json_text));
}

bool load_commands_document(nlohmann::json& out_doc, std::string& err)
{
    std::string raw;
    out_doc = default_commands_doc();
    if (!media::runtime_settings::load_command_json_utf8(raw, err))
        return false;
    if (raw.empty())
        return true;
    try {
        out_doc = normalize_commands_document(nlohmann::json::parse(normalize_command_json_text(std::move(raw))));
        return true;
    } catch (const std::exception& e) {
        err = std::string("commands.json parse failed: ") + e.what();
        return false;
    }
}

bool save_commands_document(const nlohmann::json& document, std::string& err)
{
    const nlohmann::json normalized = normalize_commands_document(document);
    return media::runtime_settings::save_command_json_utf8(normalized.dump(2) + "\n", err);
}

nlohmann::json commands_get_payload()
{
    nlohmann::json doc = default_commands_doc();
    std::string err;
    if (!load_commands_document(doc, err))
        throw std::runtime_error(err.empty() ? "load_command_json_utf8 failed" : err);
    return nlohmann::json{
        {"commandsPath", media::settings::get_command_json_path().string()},
        {"document", std::move(doc)},
    };
}

nlohmann::json commands_save_payload()
{
    return nlohmann::json{{"commandsPath", media::settings::get_command_json_path().string()}};
}

} // namespace pmui::custom_commands_host
