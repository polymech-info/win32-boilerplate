#include "blocks/data_blocks.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace media::xblox::blocks {
namespace {

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

std::string lower_ascii(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::vector<std::string> split_filter_path(std::string filter)
{
    std::vector<std::string> parts;
    if (!filter.empty() && filter.front() == '.')
        filter.erase(filter.begin());
    std::string current;
    for (char ch : filter) {
        if (ch == '.') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        parts.push_back(std::move(current));
    return parts;
}

const nlohmann::json* select_path(const nlohmann::json& value, const std::string& filter)
{
    if (filter.empty() || filter == ".")
        return &value;
    const auto parts = split_filter_path(filter);
    const nlohmann::json* current = &value;
    for (const auto& part : parts) {
        if (!current)
            return nullptr;
        if (current->is_object()) {
            if (!current->contains(part))
                return nullptr;
            current = &(*current)[part];
            continue;
        }
        if (current->is_array()) {
            try {
                size_t consumed = 0;
                const size_t index = static_cast<size_t>(std::stoull(part, &consumed));
                if (consumed != part.size() || index >= current->size())
                    return nullptr;
                current = &(*current)[index];
                continue;
            } catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }
    return current;
}

bool parse_jq_input(const nlohmann::json& input, nlohmann::json& parsed, std::string& err)
{
    try {
        if (input.is_object() && input.contains("raw") && input["raw"].is_string()) {
            parsed = nlohmann::json::parse(input["raw"].get<std::string>());
            return true;
        }
        if (input.is_string()) {
            parsed = nlohmann::json::parse(input.get<std::string>());
            return true;
        }
        parsed = input;
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool parse_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string parser = lower_ascii(json_string(block, "parser").empty() ? "jq" : json_string(block, "parser"));
    if (parser != "jq") {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "Parse", "error", "unsupported parser: " + parser, 2});
        return true;
    }

    const std::string input_name = json_string(block, "input").empty() ? (json_string(block, "from").empty() ? "PREVIOUS" : json_string(block, "from")) : json_string(block, "input");
    const nlohmann::json* input = runtime.context_value(runtime.user_data, input_name);
    if (!input) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "Parse", "error", "parse input is missing: " + input_name, 2});
        return true;
    }

    nlohmann::json parsed;
    std::string err;
    if (!parse_jq_input(*input, parsed, err)) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "Parse", "error", "jq parse failed: " + err, 2});
        return true;
    }

    const std::string filter = json_string(block, "filter").empty() ? (json_string(block, "query").empty() ? "." : json_string(block, "query")) : json_string(block, "filter");
    const nlohmann::json* selected = select_path(parsed, filter);
    if (!selected) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "Parse", "error", "jq filter returned no value: " + filter, 2});
        return true;
    }

    nlohmann::json result = *selected;
    runtime.set_context_value(runtime.user_data, "PREVIOUS", result);
    const std::string target = json_string(block, "target");
    if (!target.empty())
        runtime.set_context_value(runtime.user_data, target, result);

    runtime.emit(runtime.user_data, ExecutionEvent{
        path,
        "Parse",
        "ok",
        "parse complete",
        0,
        {{"parser", parser}, {"filter", filter}, {"input", input_name}, {"result", result}},
        json_string(block, "id"),
        "data",
    });
    return true;
}

} // namespace

BlockHandler data_block_handler(const std::string& kind)
{
    if (kind == "Parse" || kind == "parse")
        return parse_block;
    return nullptr;
}

void register_data_blocks(BlockRegistry& registry)
{
    const nlohmann::json params = nlohmann::json::array({
        {{"name", "parser"}, {"type", "string"}, {"default", "jq"}},
        {{"name", "filter"}, {"type", "string"}, {"default", "."}},
        {{"name", "input"}, {"type", "string"}, {"default", "PREVIOUS"}},
        {{"name", "storeAs"}, {"type", "string"}},
    });
    registry["Parse"] = block_descriptor(
        "Parse", parse_block, "Parse JSON", "Data", "Parse PREVIOUS or named input with a jq-style selector.",
        {{"kind", "Parse"}, {"parser", "jq"}, {"filter", "."}, {"storeAs", "parsed"}},
        params);
    registry["parse"] = block_descriptor(
        "parse", parse_block, "Parse", "Data", "Parse PREVIOUS or named input with a jq-style selector.",
        {{"kind", "parse"}, {"parser", "jq"}, {"filter", "."}, {"storeAs", "parsed"}},
        params, nlohmann::json::object(), 0xffffffffu, false);
}

} // namespace media::xblox::blocks
