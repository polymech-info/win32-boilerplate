#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace pmui::custom_commands_host {

nlohmann::json default_commands_doc();
std::string normalize_command_json_text(std::string json_text);

bool load_commands_document(nlohmann::json& out_doc, std::string& err);
bool save_commands_document(const nlohmann::json& document, std::string& err);

nlohmann::json commands_get_payload();
nlohmann::json commands_save_payload();

} // namespace pmui::custom_commands_host
