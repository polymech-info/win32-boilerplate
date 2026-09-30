#include "blocks/builtin_blocks.hpp"

#include "blocks/app_blocks.hpp"
#include "blocks/data_blocks.hpp"
#if defined(FEATURE_MODBUS_XBLOX) && FEATURE_MODBUS_XBLOX
#include "blocks/modbus_blocks.hpp"
#endif
#include "blocks/network_blocks.hpp"
#include "blocks/shell_blocks.hpp"
#include "logger/logger.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <sstream>
#include <thread>

namespace media::xblox::blocks {
namespace {

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

const nlohmann::json& json_array_or_empty(const nlohmann::json& o, const char* key)
{
    static const nlohmann::json empty = nlohmann::json::array();
    if (!o.is_object() || !o.contains(key) || !o[key].is_array())
        return empty;
    return o[key];
}

void emit_ok(BlockRuntime& runtime, ExecutionEvent event)
{
    if (runtime.events_enabled)
        runtime.emit(runtime.user_data, std::move(event));
    else
        ++runtime.result.event_count;
}

std::string child_path(const BlockRuntime& runtime, const std::string& path, const char* suffix)
{
    if (!runtime.events_enabled)
        return {};
    return path + suffix;
}

std::string indexed_child_path(const BlockRuntime& runtime, const std::string& path, const char* prefix, size_t index, const char* suffix)
{
    if (!runtime.events_enabled)
        return {};
    return path + prefix + std::to_string(index) + suffix;
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

bool compare_numbers(double lhs, const std::string& comparator, double rhs)
{
    if (comparator == "!=" || comparator == "!==" || comparator == "<>")
        return std::abs(lhs - rhs) > 1e-12;
    if (comparator == ">" || comparator == "gt")
        return lhs > rhs;
    if (comparator == ">=" || comparator == "gte")
        return lhs >= rhs;
    if (comparator == "<" || comparator == "lt")
        return lhs < rhs;
    if (comparator == "<=" || comparator == "lte")
        return lhs <= rhs;
    return std::abs(lhs - rhs) <= 1e-12;
}

bool is_raw_constant_set_loop(const nlohmann::json& block, std::string& name, nlohmann::json& value)
{
    if (json_string(block, "condition") != "1")
        return false;
    const auto& items = json_array_or_empty(block, "items");
    if (!items.is_array() || items.size() != 1 || !items[0].is_object())
        return false;
    const auto& child = items[0];
    if (json_string(child, "kind") != "setVariable" || child.contains("expression") || !child.contains("value"))
        return false;
    name = json_string(child, "name");
    if (name.empty())
        return false;
    value = child["value"];
    return true;
}

int block_loop_limit(const nlohmann::json& block, const ExecutionOptions& options)
{
    const int fallback = std::max(1, options.max_loop_iterations);
    return std::max(0, block.value("loopLimit", fallback));
}

std::string normalize_log_level(std::string level)
{
    level = trim_ascii(std::move(level));
    for (char& ch : level)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (level.empty())
        return "trace";
    if (level == "warning")
        return "warn";
    if (level == "err")
        return "error";
    if (level == "critical")
        return "error";
    if (level == "trace" || level == "debug" || level == "info" || level == "warn" || level == "error")
        return level;
    return "trace";
}

std::string json_value_to_string(const nlohmann::json& value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_number_float()) {
        std::ostringstream os;
        os << value.get<double>();
        return os.str();
    }
    if (value.is_number_integer())
        return std::to_string(value.get<long long>());
    if (value.is_number_unsigned())
        return std::to_string(value.get<unsigned long long>());
    if (value.is_boolean())
        return value.get<bool>() ? "true" : "false";
    if (value.is_null())
        return "null";
    return value.dump();
}

void write_logger_line(const std::string& level, const std::string& message)
{
    const std::string line = "[xblox] " + message;
    if (level == "error")
        logger::error(line);
    else if (level == "warn")
        logger::warn(line);
    else if (level == "info")
        logger::info(line);
    else if (level == "debug")
        logger::debug(line);
    else
        logger::trace(line);
}

bool wait_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const int ms = std::max(0, block.value("ms", 0));
    if (runtime.options.wait_blocks && ms > 0) {
        int remaining = ms;
        while (remaining > 0 && !runtime.cancel_requested(runtime.user_data)) {
            const int slice = std::min(remaining, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remaining -= slice;
        }
        if (runtime.cancel_requested(runtime.user_data))
            return true;
    }
    runtime.emit(runtime.user_data, ExecutionEvent{path, "wait", "ok", "wait complete"});
    return true;
}

bool log_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string level = normalize_log_level(json_string(block, "level"));
    std::string message = json_string(block, "message");
    if (message.empty() && block.contains("value"))
        message = json_value_to_string(block["value"]);

    if (message.empty()) {
        message = json_value_to_string(runtime.context);
    } else {
        double evaluated = 0.0;
        std::string err;
        if (const nlohmann::json* value = runtime.context_value(runtime.user_data, message)) {
            message = json_value_to_string(*value);
        } else if (runtime.eval_expression(runtime.user_data, message, evaluated, &err)) {
            message = json_value_to_string(evaluated);
        }
    }

    write_logger_line(level, message);
    nlohmann::json result = {{"level", level}, {"message", message}};
    runtime.emit(runtime.user_data, ExecutionEvent{path, "log", "ok", message, 0, result});
    return true;
}

bool break_block(const nlohmann::json&, const std::string& path, BlockRuntime& runtime)
{
    runtime.break_requested = true;
    runtime.emit(runtime.user_data, ExecutionEvent{path, "break", "ok", "break requested"});
    return true;
}

bool set_variable_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string name = json_string(block, "name");
    if (name.empty()) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "setVariable", "error", "setVariable is missing name", 2});
        return true;
    }

    nlohmann::json value = nullptr;
    const std::string value_from = json_string(block, "valueFrom").empty() ? json_string(block, "from") : json_string(block, "valueFrom");
    if (!value_from.empty()) {
        if (const nlohmann::json* found = runtime.context_value(runtime.user_data, value_from))
            value = *found;
    } else if (block.contains("expression") && block["expression"].is_string() && !block["expression"].get<std::string>().empty()) {
        double evaluated = 0.0;
        std::string err;
        if (!runtime.eval_expression(runtime.user_data, block["expression"].get<std::string>(), evaluated, &err)) {
            runtime.emit(runtime.user_data, ExecutionEvent{path, "setVariable", "error", "setVariable expression failed: " + err, 2});
            return true;
        }
        value = evaluated;
    } else if (block.contains("value")) {
        value = block["value"];
    }

    runtime.set_context_value(runtime.user_data, name, value);
    runtime.set_context_value(runtime.user_data, "PREVIOUS", value);
    if (runtime.events_enabled)
        runtime.emit(runtime.user_data, ExecutionEvent{path, "setVariable", "ok", "variable set", 0, {{"name", name}, {"value", value}}});
    else
        ++runtime.result.event_count;
    return true;
}

bool get_variable_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string name = json_string(block, "name");
    if (name.empty()) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "getVariable", "error", "getVariable is missing name", 2});
        return true;
    }

    const nlohmann::json* found = runtime.context_value(runtime.user_data, name);
    nlohmann::json value = found ? *found : nullptr;
    const std::string target = json_string(block, "target");
    if (!target.empty())
        runtime.set_context_value(runtime.user_data, target, value);
    runtime.set_context_value(runtime.user_data, "PREVIOUS", value);
    if (runtime.events_enabled)
        runtime.emit(runtime.user_data, ExecutionEvent{path, "getVariable", "ok", "variable read", 0, {{"name", name}, {"target", target}, {"value", value}}});
    else
        ++runtime.result.event_count;
    return true;
}

bool if_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    if (runtime.condition_truthy(runtime.user_data, json_string(block, "condition"))) {
        runtime.execute_block_list(runtime.user_data, json_array_or_empty(block, "consequent"), child_path(runtime, path, "/consequent"));
    } else {
        bool ran_else_if = false;
        const auto& else_ifs = json_array_or_empty(block, "elseIfBlocks");
        if (else_ifs.is_array()) {
            for (size_t i = 0; i < else_ifs.size(); ++i) {
                if (runtime.condition_truthy(runtime.user_data, json_string(else_ifs[i], "condition"))) {
                    runtime.execute_block_list(runtime.user_data, json_array_or_empty(else_ifs[i], "consequent"),
                                               indexed_child_path(runtime, path, "/elseIfBlocks/", i, "/consequent"));
                    ran_else_if = true;
                    break;
                }
            }
        }
        if (!ran_else_if)
            runtime.execute_block_list(runtime.user_data, json_array_or_empty(block, "alternate"), child_path(runtime, path, "/alternate"));
    }
    emit_ok(runtime, ExecutionEvent{path, "if", runtime.result.ok ? "ok" : "error", "if complete"});
    return true;
}

bool for_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string variable = block.value("variable", std::string{"i"});
    const std::string comparator = json_string(block, "comparator");
    const std::string modifier = trim_ascii(json_string(block, "modifier"));
    const int limit = block_loop_limit(block, runtime.options);

    double current = 0.0;
    double final_value = 0.0;
    std::string err;
    if (!runtime.eval_expression(runtime.user_data, json_string(block, "initial"), current, &err)) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "for", "error", "for initial expression failed: " + err, 2});
        return true;
    }
    if (!runtime.eval_expression(runtime.user_data, json_string(block, "final"), final_value, &err)) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "for", "error", "for final expression failed: " + err, 2});
        return true;
    }

    int iterations = 0;
    while (iterations < limit && compare_numbers(current, comparator, final_value) && !runtime.break_requested && !runtime.cancel_requested(runtime.user_data)) {
        runtime.set_context_value(runtime.user_data, variable, current);
        runtime.set_context_value(runtime.user_data, "loop.index", iterations);
        runtime.execute_block_list(runtime.user_data, json_array_or_empty(block, "items"), child_path(runtime, path, "/items"));
        if (runtime.break_requested)
            break;

        if (modifier.empty()) {
            current += 1.0;
        } else if (modifier[0] == '+' || modifier[0] == '-') {
            double delta = 0.0;
            if (!runtime.eval_expression(runtime.user_data, modifier, delta, &err)) {
                runtime.emit(runtime.user_data, ExecutionEvent{path, "for", "error", "for modifier expression failed: " + err, 2});
                return true;
            }
            current += delta;
        } else {
            runtime.set_context_value(runtime.user_data, variable, current);
            double next = 0.0;
            if (!runtime.eval_expression(runtime.user_data, modifier, next, &err)) {
                runtime.emit(runtime.user_data, ExecutionEvent{path, "for", "error", "for modifier expression failed: " + err, 2});
                return true;
            }
            current = next;
        }
        ++iterations;
    }
    runtime.set_context_value(runtime.user_data, variable, current);
    emit_ok(runtime, ExecutionEvent{path, "for", runtime.result.ok ? "ok" : "error", "for body complete", 0, {{"iterations", iterations}}});
    return true;
}

bool while_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const int limit = block_loop_limit(block, runtime.options);
    std::string raw_name;
    nlohmann::json raw_value;
    if (!runtime.events_enabled && !runtime.break_requested && is_raw_constant_set_loop(block, raw_name, raw_value)) {
        constexpr int kCancelPollInterval = 4096;
        int iterations = 0;
        volatile double raw_sink = 0.0;
        double raw_number = 0.0;
        if (raw_value.is_number())
            raw_number = raw_value.get<double>();
        else if (raw_value.is_boolean())
            raw_number = raw_value.get<bool>() ? 1.0 : 0.0;
        while (iterations < limit && !runtime.break_requested) {
            const int remaining = limit - iterations;
            const int chunk = std::min(kCancelPollInterval, remaining);
            for (int i = 0; i < chunk; ++i) {
                raw_sink = raw_number;
                runtime.result.event_count += 2;
                ++iterations;
            }
            if (runtime.cancel_requested(runtime.user_data))
                break;
        }
        (void)raw_sink;
        runtime.set_context_value(runtime.user_data, raw_name, std::move(raw_value));
        emit_ok(runtime, ExecutionEvent{path, "while", runtime.result.ok ? "ok" : "error", "while raw body complete", 0, {{"iterations", iterations}}});
        return true;
    }

    int iterations = 0;
    for (; iterations < limit && runtime.condition_truthy(runtime.user_data, json_string(block, "condition")) && !runtime.break_requested && !runtime.cancel_requested(runtime.user_data); ++iterations)
        runtime.execute_block_list(runtime.user_data, json_array_or_empty(block, "items"), child_path(runtime, path, "/items"));
    emit_ok(runtime, ExecutionEvent{path, "while", runtime.result.ok ? "ok" : "error", "while body complete", 0, {{"iterations", iterations}}});
    return true;
}

bool switch_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string switch_expr = json_string(block, "variable");
    double switch_number = 0.0;
    const bool has_switch_number = runtime.numeric_value(runtime.user_data, switch_expr, switch_number);
    const std::string switch_value = has_switch_number ? std::string{} : runtime.string_value(runtime.user_data, switch_expr);
    const auto& items = json_array_or_empty(block, "items");
    int fallback = -1;
    int matched = -1;
    if (items.is_array()) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (json_string(items[i], "kind") == "switchDefault")
                fallback = static_cast<int>(i);
            else if (matched < 0 && runtime.case_matches(runtime.user_data, items[i], switch_number, has_switch_number, switch_value))
                matched = static_cast<int>(i);
        }
    }
    const int index = matched >= 0 ? matched : fallback;
    if (index >= 0)
        runtime.execute_block_list(runtime.user_data, json_array_or_empty(items[static_cast<size_t>(index)], "consequent"),
                                   indexed_child_path(runtime, path, "/items/", static_cast<size_t>(index), "/consequent"));
    emit_ok(runtime, ExecutionEvent{path, "switch", runtime.result.ok ? "ok" : "error", "switch complete"});
    return true;
}

} // namespace

BlockHandler builtin_block_handler(const std::string& kind)
{
    BlockRegistry registry;
    register_builtin_blocks(registry);
    const auto it = registry.find(kind);
    return it == registry.end() ? nullptr : it->second.handler;
}

void register_builtin_blocks(BlockRegistry& registry)
{
    register_data_blocks(registry);
    register_network_blocks(registry);
    register_shell_blocks(registry);
    register_app_blocks(registry);
#if defined(FEATURE_MODBUS_XBLOX) && FEATURE_MODBUS_XBLOX
    register_modbus_blocks(registry);
#endif
    registry["wait"] = block_descriptor(
        "wait", wait_block, "Wait", "Flow", "Sleep for a fixed number of milliseconds.",
        {{"kind", "wait"}, {"ms", 500}},
        nlohmann::json::array({{{"name", "ms"}, {"type", "integer"}, {"default", 500}}}));
    registry["log"] = block_descriptor(
        "log", log_block, "Log", "Context", "Log a message, expression, variable, or the whole scope.",
        {{"kind", "log"}, {"level", "info"}, {"message", "PREVIOUS"}},
        nlohmann::json::array({{{"name", "level"}, {"type", "string"}, {"default", "info"}}, {{"name", "message"}, {"type", "string"}}}));
    registry["break"] = block_descriptor("break", break_block, "Break", "Flow", "Break out of the current loop.", {{"kind", "break"}});
    registry["setVariable"] = block_descriptor(
        "setVariable", set_variable_block, "Set Variable", "Context", "Write a value into root scope.",
        {{"kind", "setVariable"}, {"name", "value"}, {"value", nullptr}},
        nlohmann::json::array({{{"name", "name"}, {"type", "string"}, {"required", true}}, {{"name", "value"}, {"type", "json"}}, {{"name", "expression"}, {"type", "string"}}}));
    registry["getVariable"] = block_descriptor(
        "getVariable", get_variable_block, "Get Variable", "Context", "Read a value from root scope.",
        {{"kind", "getVariable"}, {"name", "value"}, {"target", "PREVIOUS"}},
        nlohmann::json::array({{{"name", "name"}, {"type", "string"}, {"required", true}}, {{"name", "target"}, {"type", "string"}}}));
    registry["if"] = block_descriptor(
        "if", if_block, "If", "Flow", "Run consequent or alternate blocks based on a condition.",
        {{"kind", "if"}, {"condition", "true"}, {"consequent", nlohmann::json::array()}, {"alternate", nlohmann::json::array()}});
    registry["for"] = block_descriptor(
        "for", for_block, "For", "Flow", "Run child blocks over a numeric range.",
        {{"kind", "for"}, {"initial", "0"}, {"final", "3"}, {"comparator", "<"}, {"modifier", "+1"}, {"items", nlohmann::json::array()}});
    registry["while"] = block_descriptor(
        "while", while_block, "While", "Flow", "Run child blocks while a condition is true.",
        {{"kind", "while"}, {"condition", "false"}, {"loopLimit", 10}, {"items", nlohmann::json::array()}});
    registry["switch"] = block_descriptor(
        "switch", switch_block, "Switch", "Flow", "Run the first matching case.",
        {{"kind", "switch"}, {"variable", "mode"}, {"items", nlohmann::json::array()}});
}

std::vector<BlockDescriptor> registered_block_definitions()
{
    BlockRegistry registry;
    register_builtin_blocks(registry);
    std::vector<BlockDescriptor> definitions;
    definitions.reserve(registry.size());
    for (const auto& [_, descriptor] : registry) {
        if (descriptor.palette_visible)
            definitions.push_back(descriptor);
    }
    std::sort(definitions.begin(), definitions.end(), [](const BlockDescriptor& a, const BlockDescriptor& b) {
        if (a.group != b.group)
            return a.group < b.group;
        return a.kind < b.kind;
    });
    return definitions;
}

} // namespace media::xblox::blocks
