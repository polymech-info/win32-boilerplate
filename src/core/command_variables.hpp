#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace media::commands {

struct VariableContext {
    std::string cwd;
    std::string source_file;
    std::vector<std::string> selection_paths;
};

using VariableMap = std::unordered_map<std::string, std::string>;

VariableMap make_variable_map(const VariableContext& context);

std::string resolve_variables(std::string_view value,
                              const VariableMap& vars,
                              std::string* err = nullptr);

void resolve_custom_command_item_variables(nlohmann::json& item,
                                           const VariableContext& context,
                                           std::string* err = nullptr);

} // namespace media::commands
