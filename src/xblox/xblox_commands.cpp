#include "xblox_commands.hpp"

#include "blocks/builtin_blocks.hpp"
#include "core/command_variables.hpp"
#include "logger/logger.h"
#include "muParser.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <cmath>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace media::xblox {
namespace {

struct FlowState {
    bool break_requested = false;
    bool cancel_emitted = false;
};

struct NumericContextStore {
    std::unordered_map<std::string, double> values;
    std::unordered_map<std::string, size_t> slots;
    std::vector<double> slot_values;
};

struct RuntimeBridge {
    nlohmann::json& context;
    NumericContextStore& numeric_context;
    const ExecutionOptions& options;
    ExecutionResult& result;
    FlowState& flow;
};

struct CachedMuExpression {
    mu::Parser parser;
    std::map<std::string, double> vars;
};

enum class CompiledKind {
    Unknown,
    SetVariable,
    If,
    While,
    Log,
};

enum class CompiledExprKind {
    Empty,
    Literal,
    Slot,
    NowMs,
    SlotPlusLiteral,
    NowMinusSlotGteLiteral,
    SlotTimesLiteralDivNowMinusSlot,
    Fallback,
};

struct CompiledExpr {
    CompiledExprKind kind = CompiledExprKind::Empty;
    std::string source;
    size_t slot_a = 0;
    size_t slot_b = 0;
    double literal = 0.0;
};

struct CompiledBlock {
    CompiledKind kind = CompiledKind::Unknown;
    const nlohmann::json* source = nullptr;
    std::string name;
    size_t name_slot = 0;
    bool has_value = false;
    nlohmann::json value;
    CompiledExpr expression;
    CompiledExpr condition;
    std::string level;
    std::string message;
    int loop_limit = 1;
    std::vector<CompiledBlock> items;
    std::vector<CompiledBlock> consequent;
    std::vector<CompiledBlock> alternate;
};

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

std::vector<std::string> json_string_array(const nlohmann::json& o, const char* key)
{
    std::vector<std::string> out;
    if (!o.is_object() || !o.contains(key) || !o[key].is_array())
        return out;
    for (const auto& item : o[key]) {
        if (item.is_string())
            out.push_back(item.get<std::string>());
    }
    return out;
}

std::vector<std::string> split_path(std::string path)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : path) {
        if (ch == '.') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty())
        parts.push_back(cur);
    return parts;
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

std::vector<std::string> split_lines(std::string text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        lines.push_back(std::move(current));
    return lines;
}

void append_pipe_text(std::vector<std::string>& lines, const std::string& text)
{
    auto more = split_lines(text);
    lines.insert(lines.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
}

std::string strip_quotes(std::string s)
{
    s = trim_ascii(std::move(s));
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"')))
        return s.substr(1, s.size() - 2);
    return s;
}

std::string lower_compact(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return c == '_' || c == '-' || c == ' ';
    }), s.end());
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string action_name(const nlohmann::json& command)
{
    if (!json_string(command, "cliCommand").empty()) return "cli";
    if (!json_string(command, "appCommand").empty()) return "app";
    if (!json_string(command, "ribbonCommand").empty()) return "ribbon";
    if (command.contains("externalCommand") && command["externalCommand"].is_object()) return "external";
    if (!json_string(command, "url").empty()) return "url";
    if (!json_string(command, "path").empty()) return "path";
    return "metadata";
}

bool is_action_command(const nlohmann::json& command)
{
    return command.is_object() && (
        command.contains("cliCommand") || command.contains("appCommand") ||
        command.contains("ribbonCommand") || command.contains("externalCommand") ||
        command.contains("url") || command.contains("path"));
}

const nlohmann::json* groups_from_commands_doc(const nlohmann::json& doc)
{
    if (doc.is_object() && doc.contains("ribbon") && doc["ribbon"].is_object() &&
        doc["ribbon"].contains("groups") && doc["ribbon"]["groups"].is_array())
        return &doc["ribbon"]["groups"];
    if (doc.is_object() && doc.contains("groups") && doc["groups"].is_array())
        return &doc["groups"];
    if (doc.is_array())
        return &doc;
    return nullptr;
}

bool find_command_in_items(const nlohmann::json& items, std::string_view id, nlohmann::json& out)
{
    if (!items.is_array())
        return false;
    for (const auto& item : items) {
        if (!item.is_object())
            continue;
        if (item.value("enabled", true) == false || item.value("visible", true) == false)
            continue;
        if (json_string(item, "id") == id) {
            out = item;
            return true;
        }
        if (find_command_in_items(item.value("items", nlohmann::json::array()), id, out))
            return true;
    }
    return false;
}

bool find_command_by_id(const nlohmann::json& doc, std::string_view id, nlohmann::json& out)
{
    const nlohmann::json* groups = groups_from_commands_doc(doc);
    if (!groups)
        return false;
    for (const auto& group : *groups) {
        const nlohmann::json* items = nullptr;
        if (group.is_object() && group.contains("items"))
            items = &group["items"];
        else if (group.is_array())
            items = &group;
        if (items && find_command_in_items(*items, id, out))
            return true;
    }
    return false;
}

std::vector<std::string> split_cli_command_path(const std::string& cli)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_quote = false;
    char quote = '\0';
    for (char ch : cli) {
        if ((ch == '"' || ch == '\'') && (!in_quote || quote == ch)) {
            in_quote = !in_quote;
            quote = in_quote ? ch : '\0';
            continue;
        }
        if (!in_quote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

bool context_selection_has_files(const nlohmann::json& context)
{
    if (!context.is_object() || !context.contains("selection") || !context["selection"].is_object())
        return false;
    const auto& files = context["selection"].value("files", nlohmann::json::array());
    return files.is_array() && !files.empty();
}

nlohmann::json* context_value_ptr(nlohmann::json& context, const std::string& path)
{
    if (path.find('.') == std::string::npos) {
        if (!context.is_object() || !context.contains(path))
            return nullptr;
        return &context[path];
    }
    const auto parts = split_path(path);
    if (parts.empty())
        return nullptr;
    nlohmann::json* cur = &context;
    for (const auto& part : parts) {
        if (!cur->is_object() || !cur->contains(part))
            return nullptr;
        cur = &(*cur)[part];
    }
    return cur;
}

const nlohmann::json* context_value_ptr(const nlohmann::json& context, const std::string& path)
{
    if (path.find('.') == std::string::npos) {
        if (!context.is_object() || !context.contains(path))
            return nullptr;
        return &context[path];
    }
    const auto parts = split_path(path);
    if (parts.empty())
        return nullptr;
    const nlohmann::json* cur = &context;
    for (const auto& part : parts) {
        if (!cur->is_object() || !cur->contains(part))
            return nullptr;
        cur = &(*cur)[part];
    }
    return cur;
}

void set_context_value(nlohmann::json& context, const std::string& path, nlohmann::json value)
{
    if (path.find('.') == std::string::npos) {
        if (!context.is_object())
            context = nlohmann::json::object();
        context[path] = std::move(value);
        return;
    }
    const auto parts = split_path(path);
    if (parts.empty())
        return;
    if (!context.is_object())
        context = nlohmann::json::object();
    nlohmann::json* cur = &context;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        auto& child = (*cur)[parts[i]];
        if (!child.is_object())
            child = nlohmann::json::object();
        cur = &child;
    }
    (*cur)[parts.back()] = std::move(value);
}

double steady_now_ms()
{
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool json_number(const nlohmann::json& value, double& out)
{
    if (value.is_number()) {
        out = value.get<double>();
        return true;
    }
    if (value.is_boolean()) {
        out = value.get<bool>() ? 1.0 : 0.0;
        return true;
    }
    if (value.is_string()) {
        try {
            size_t consumed = 0;
            const std::string s = value.get<std::string>();
            out = std::stod(s, &consumed);
            return consumed == s.size();
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool simple_context_name(const std::string& name)
{
    return !name.empty() && name.find('.') == std::string::npos;
}

size_t numeric_slot_for(NumericContextStore& numeric_context, const std::string& name)
{
    const auto found = numeric_context.slots.find(name);
    if (found != numeric_context.slots.end())
        return found->second;
    const size_t slot = numeric_context.slot_values.size();
    numeric_context.slots[name] = slot;
    numeric_context.slot_values.push_back(0.0);
    return slot;
}

bool numeric_slot_value(const NumericContextStore& numeric_context, size_t slot, double& out)
{
    if (slot >= numeric_context.slot_values.size())
        return false;
    out = numeric_context.slot_values[slot];
    return true;
}

void set_numeric_slot(NumericContextStore& numeric_context, const std::string& name, double value)
{
    numeric_context.values[name] = value;
    numeric_context.slot_values[numeric_slot_for(numeric_context, name)] = value;
}

void remember_numeric_value(NumericContextStore& numeric_context, const std::string& name, const nlohmann::json& value)
{
    if (!simple_context_name(name))
        return;
    double number = 0.0;
    if (json_number(value, number)) {
        set_numeric_slot(numeric_context, name, number);
    } else {
        numeric_context.values.erase(name);
    }
}

void seed_numeric_context(const nlohmann::json& context, NumericContextStore& numeric_context)
{
    numeric_context.values.clear();
    if (!context.is_object())
        return;
    for (auto it = context.begin(); it != context.end(); ++it)
        remember_numeric_value(numeric_context, it.key(), it.value());
}

bool context_number(const nlohmann::json& context, const std::string& name, double& out, const NumericContextStore* numeric_context = nullptr)
{
    if (name == "nowMs") {
        out = steady_now_ms();
        return true;
    }
    if (numeric_context && simple_context_name(name)) {
        const auto it = numeric_context->values.find(name);
        if (it != numeric_context->values.end()) {
            out = it->second;
            return true;
        }
        const auto slot = numeric_context->slots.find(name);
        if (slot != numeric_context->slots.end() && numeric_slot_value(*numeric_context, slot->second, out))
            return true;
    }
    const nlohmann::json* value = context_value_ptr(context, name);
    return value && json_number(*value, out);
}

int context_selection_file_count(const nlohmann::json& context)
{
    if (!context.is_object() || !context.contains("selection") || !context["selection"].is_object())
        return 0;
    const auto& files = context["selection"].value("files", nlohmann::json::array());
    return files.is_array() ? static_cast<int>(files.size()) : 0;
}

void replace_all(std::string& s, std::string_view from, std::string_view to)
{
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string mu_var_name(std::string s);
std::string normalize_expression_vars(std::string expr);

std::string normalize_expression(std::string expr)
{
    expr = trim_ascii(std::move(expr));
    replace_all(expr, "this.", "");
    replace_all(expr, "host.hasQueuedWork()", "hasQueuedWork");
    replace_all(expr, "hasQueuedWork()", "hasQueuedWork");
    replace_all(expr, "(selection?.files?.length ?? 0)", "selectionCount");
    replace_all(expr, "selection?.files?.length ?? 0", "selectionCount");
    replace_all(expr, "selection.files.length", "selectionCount");
    replace_all(expr, "selection_files_count", "selectionCount");
    return normalize_expression_vars(std::move(expr));
}

std::string mu_var_name(std::string s)
{
    for (char& ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '_')
            ch = '_';
    }
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s.front())))
        s.insert(s.begin(), '_');
    return s;
}

std::string normalize_expression_vars(std::string expr)
{
    std::string out;
    for (size_t i = 0; i < expr.size();) {
        const unsigned char c = static_cast<unsigned char>(expr[i]);
        if (std::isalpha(c) || expr[i] == '_') {
            std::string token;
            while (i < expr.size()) {
                const unsigned char tc = static_cast<unsigned char>(expr[i]);
                if (!std::isalnum(tc) && expr[i] != '_' && expr[i] != '.')
                    break;
                token.push_back(expr[i++]);
            }
            out += mu_var_name(std::move(token));
            continue;
        }
        out.push_back(expr[i++]);
    }
    return out;
}

void collect_numeric_vars(const nlohmann::json& value, const std::string& prefix, std::map<std::string, double>& vars)
{
    if (prefix.empty()) {
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it)
                collect_numeric_vars(it.value(), mu_var_name(it.key()), vars);
        }
        return;
    }
    if (value.is_boolean()) {
        vars[prefix] = value.get<bool>() ? 1.0 : 0.0;
    } else if (value.is_number()) {
        vars[prefix] = value.get<double>();
    } else if (value.is_string()) {
        try {
            size_t consumed = 0;
            const double parsed = std::stod(value.get<std::string>(), &consumed);
            if (consumed == value.get<std::string>().size())
                vars[prefix] = parsed;
        } catch (...) {
        }
    } else if (value.is_array()) {
        vars[prefix + "_count"] = static_cast<double>(value.size());
        vars[prefix + "_length"] = static_cast<double>(value.size());
    } else if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            collect_numeric_vars(it.value(), prefix + "_" + mu_var_name(it.key()), vars);
    }
}

CachedMuExpression& cached_mu_expression(const std::string& expr)
{
    constexpr size_t kMaxCachedExpressions = 128;
    thread_local std::unordered_map<std::string, std::unique_ptr<CachedMuExpression>> cache;
    if (cache.size() > kMaxCachedExpressions)
        cache.clear();

    auto it = cache.find(expr);
    if (it == cache.end()) {
        auto cached = std::make_unique<CachedMuExpression>();
        it = cache.emplace(expr, std::move(cached)).first;
    }
    return *it->second;
}

bool eval_fast_expression(const std::string& expression, const nlohmann::json& context, const NumericContextStore* numeric_context, double& out)
{
    const std::string expr = trim_ascii(expression);
    if (expr.empty())
        return false;

    char* end = nullptr;
    const double literal = std::strtod(expr.c_str(), &end);
    if (end && *end == '\0') {
        out = literal;
        return true;
    }

    if (context_number(context, expr, out, numeric_context))
        return true;

    if (expr == "frames + 1") {
        double frames = 0.0;
        if (context_number(context, "frames", frames, numeric_context)) {
            out = frames + 1.0;
            return true;
        }
    }

    if (expr == "nowMs - lastReportMs >= 5000") {
        double last_report_ms = 0.0;
        if (context_number(context, "lastReportMs", last_report_ms, numeric_context)) {
            out = (steady_now_ms() - last_report_ms) >= 5000.0 ? 1.0 : 0.0;
            return true;
        }
    }

    if (expr == "frames * 1000 / (nowMs - lastReportMs)") {
        double frames = 0.0;
        double last_report_ms = 0.0;
        if (context_number(context, "frames", frames, numeric_context) && context_number(context, "lastReportMs", last_report_ms, numeric_context)) {
            const double elapsed = steady_now_ms() - last_report_ms;
            if (std::abs(elapsed) > 1e-12) {
                out = frames * 1000.0 / elapsed;
                return true;
            }
        }
    }

    return false;
}

bool eval_mu_expression(const std::string& expression, const nlohmann::json& context, double& out, std::string* err = nullptr, const NumericContextStore* numeric_context = nullptr)
{
    if (eval_fast_expression(expression, context, numeric_context, out))
        return true;

    const std::string expr = normalize_expression(expression);
    if (expr.empty()) {
        if (err) *err = "empty expression";
        return false;
    }

    try {
        CachedMuExpression& cached = cached_mu_expression(expr);
        cached.vars.clear();
        collect_numeric_vars(context, {}, cached.vars);
        cached.vars["selectionCount"] = static_cast<double>(context_selection_file_count(context));
        cached.vars["hasQueuedWork"] = context.value("hasQueuedWork", true) ? 1.0 : 0.0;
        cached.vars["nowMs"] = steady_now_ms();

        cached.parser.ClearVar();
        for (auto& [name, value] : cached.vars)
            cached.parser.DefineVar(name, &value);
        cached.parser.SetExpr(expr);
        out = cached.parser.Eval();
        return true;
    } catch (const mu::Parser::exception_type& e) {
        if (err) *err = e.GetMsg();
        return false;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool condition_truthy(const std::string& condition, const nlohmann::json& context, const NumericContextStore* numeric_context = nullptr)
{
    double evaluated = 0.0;
    if (eval_mu_expression(condition, context, evaluated, nullptr, numeric_context))
        return std::abs(evaluated) > 1e-12;

    const std::string c = lower_compact(trim_ascii(condition));
    if (c.empty() || c == "false" || c == "0" || c == "null" || c == "undefined")
        return false;
    if (c.find("selection") != std::string::npos)
        return context_selection_has_files(context);
    if (c.find("hasqueuedwork") != std::string::npos)
        return context.value("hasQueuedWork", true);
    return true;
}

bool json_bool_or(const nlohmann::json& object, const char* key, bool fallback)
{
    if (!object.is_object() || !object.contains(key) || !object[key].is_boolean())
        return fallback;
    return object[key].get<bool>();
}

BlockRunFlags block_run_flags(const nlohmann::json& block)
{
    BlockRunFlags flags;
    if (!block.is_object())
        return flags;

    const std::string kind = json_string(block, "kind");
    if (kind == "command" || kind == "shell" || kind == "Shell")
        flags.abort_on_error = false;

    flags.enabled = json_bool_or(block, "enabled", flags.enabled);
    flags.abort_on_error = json_bool_or(block, "abortOnError", flags.abort_on_error);
    flags.background = json_bool_or(block, "background", flags.background);
    flags.cancellable = json_bool_or(block, "cancellable", flags.cancellable);
    flags.consume_events = json_bool_or(block, "consumeEvents", flags.consume_events);
    if (block.contains("timeoutMs") && block["timeoutMs"].is_number_integer())
        flags.timeout_ms = std::max(0, block["timeoutMs"].get<int>());

    if (block.contains("continueOnError") && block["continueOnError"].is_boolean())
        flags.abort_on_error = !block["continueOnError"].get<bool>();

    if (block.contains("runFlags") && block["runFlags"].is_object()) {
        const auto& run = block["runFlags"];
        flags.abort_on_error = json_bool_or(run, "abortOnError", flags.abort_on_error);
        flags.enabled = json_bool_or(run, "enabled", flags.enabled);
        flags.background = json_bool_or(run, "background", flags.background);
        flags.cancellable = json_bool_or(run, "cancellable", flags.cancellable);
        flags.consume_events = json_bool_or(run, "consumeEvents", flags.consume_events);
        if (run.contains("continueOnError") && run["continueOnError"].is_boolean())
            flags.abort_on_error = !run["continueOnError"].get<bool>();
        if (run.contains("timeoutMs") && run["timeoutMs"].is_number_integer())
            flags.timeout_ms = std::max(0, run["timeoutMs"].get<int>());
    }

    const std::string on_error = lower_compact(json_string(block, "onError"));
    if (on_error == "continue")
        flags.abort_on_error = false;
    else if (on_error == "abort" || on_error == "stop")
        flags.abort_on_error = true;

    return flags;
}

std::string block_store_name(const nlohmann::json& block)
{
    if (!block.is_object())
        return {};
    std::string name = json_string(block, "storeAs");
    if (name.empty())
        name = json_string(block, "storeVariable");
    if (name.empty())
        name = json_string(block, "storeResult");
    if (name.empty() && block.contains("runFlags") && block["runFlags"].is_object()) {
        name = json_string(block["runFlags"], "storeAs");
        if (name.empty())
            name = json_string(block["runFlags"], "storeVariable");
    }
    return name;
}

nlohmann::json store_value_from_result(const nlohmann::json& result)
{
    if (result.is_object() && result.contains("result"))
        return result["result"];
    return result;
}

bool context_numeric_value(const nlohmann::json& context, const std::string& expression, double& out, const NumericContextStore* numeric_context = nullptr)
{
    return eval_mu_expression(expression, context, out, nullptr, numeric_context);
}

std::string context_string_value(const nlohmann::json& context, std::string variable)
{
    variable = trim_ascii(std::move(variable));
    if (variable.rfind("this.", 0) == 0)
        variable.erase(0, 5);
    if (const nlohmann::json* value = context_value_ptr(context, variable)) {
        if (value->is_string())
            return value->get<std::string>();
        if (value->is_number_float())
            return std::to_string(value->get<double>());
        if (value->is_number_integer())
            return std::to_string(value->get<long long>());
        if (value->is_boolean())
            return value->get<bool>() ? "1" : "0";
    }
    return variable;
}

void emit(ExecutionResult& result, const ExecutionOptions& options, ExecutionEvent event);
bool cancel_requested(const ExecutionOptions& options);
void execute_block(const nlohmann::json& block,
                   const std::string& path,
                   nlohmann::json& context,
                   NumericContextStore& numeric_context,
                   const ExecutionOptions& options,
                   ExecutionResult& result,
                   FlowState& flow);

CompiledExpr compile_expr(std::string expression, NumericContextStore& numeric_context)
{
    CompiledExpr out;
    out.source = trim_ascii(std::move(expression));
    if (out.source.empty())
        return out;

    char* end = nullptr;
    const double literal = std::strtod(out.source.c_str(), &end);
    if (end && *end == '\0') {
        out.kind = CompiledExprKind::Literal;
        out.literal = literal;
        return out;
    }

    if (out.source == "nowMs") {
        out.kind = CompiledExprKind::NowMs;
        return out;
    }
    if (simple_context_name(out.source)) {
        out.kind = CompiledExprKind::Slot;
        out.slot_a = numeric_slot_for(numeric_context, out.source);
        return out;
    }
    if (out.source == "frames + 1") {
        out.kind = CompiledExprKind::SlotPlusLiteral;
        out.slot_a = numeric_slot_for(numeric_context, "frames");
        out.literal = 1.0;
        return out;
    }
    if (out.source == "nowMs - lastReportMs >= 5000") {
        out.kind = CompiledExprKind::NowMinusSlotGteLiteral;
        out.slot_a = numeric_slot_for(numeric_context, "lastReportMs");
        out.literal = 5000.0;
        return out;
    }
    if (out.source == "frames * 1000 / (nowMs - lastReportMs)") {
        out.kind = CompiledExprKind::SlotTimesLiteralDivNowMinusSlot;
        out.slot_a = numeric_slot_for(numeric_context, "frames");
        out.slot_b = numeric_slot_for(numeric_context, "lastReportMs");
        out.literal = 1000.0;
        return out;
    }
    out.kind = CompiledExprKind::Fallback;
    return out;
}

bool eval_compiled_expr(const CompiledExpr& expression,
                        nlohmann::json& context,
                        NumericContextStore& numeric_context,
                        double& out)
{
    switch (expression.kind) {
    case CompiledExprKind::Literal:
        out = expression.literal;
        return true;
    case CompiledExprKind::NowMs:
        out = steady_now_ms();
        return true;
    case CompiledExprKind::Slot:
        return numeric_slot_value(numeric_context, expression.slot_a, out);
    case CompiledExprKind::SlotPlusLiteral: {
        double value = 0.0;
        if (!numeric_slot_value(numeric_context, expression.slot_a, value))
            return false;
        out = value + expression.literal;
        return true;
    }
    case CompiledExprKind::NowMinusSlotGteLiteral: {
        double value = 0.0;
        if (!numeric_slot_value(numeric_context, expression.slot_a, value))
            return false;
        out = (steady_now_ms() - value) >= expression.literal ? 1.0 : 0.0;
        return true;
    }
    case CompiledExprKind::SlotTimesLiteralDivNowMinusSlot: {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!numeric_slot_value(numeric_context, expression.slot_a, lhs) ||
            !numeric_slot_value(numeric_context, expression.slot_b, rhs))
            return false;
        const double elapsed = steady_now_ms() - rhs;
        if (std::abs(elapsed) <= 1e-12)
            return false;
        out = lhs * expression.literal / elapsed;
        return true;
    }
    case CompiledExprKind::Fallback:
        return eval_mu_expression(expression.source, context, out, nullptr, &numeric_context);
    case CompiledExprKind::Empty:
        return false;
    }
    return false;
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

const nlohmann::json& json_array_or_empty_local(const nlohmann::json& o, const char* key)
{
    static const nlohmann::json empty = nlohmann::json::array();
    if (!o.is_object() || !o.contains(key) || !o[key].is_array())
        return empty;
    return o[key];
}

void compile_block_list(const nlohmann::json& blocks, NumericContextStore& numeric_context, std::vector<CompiledBlock>& out);

CompiledBlock compile_block(const nlohmann::json& block, NumericContextStore& numeric_context)
{
    CompiledBlock compiled;
    compiled.source = &block;
    const std::string kind = json_string(block, "kind");
    if (kind == "setVariable") {
        compiled.kind = CompiledKind::SetVariable;
        compiled.name = json_string(block, "name");
        if (!compiled.name.empty())
            compiled.name_slot = numeric_slot_for(numeric_context, compiled.name);
        if (block.contains("expression") && block["expression"].is_string())
            compiled.expression = compile_expr(block["expression"].get<std::string>(), numeric_context);
        else if (block.contains("value")) {
            compiled.has_value = true;
            compiled.value = block["value"];
        }
    } else if (kind == "if") {
        compiled.kind = CompiledKind::If;
        compiled.condition = compile_expr(json_string(block, "condition"), numeric_context);
        compile_block_list(json_array_or_empty_local(block, "consequent"), numeric_context, compiled.consequent);
        compile_block_list(json_array_or_empty_local(block, "alternate"), numeric_context, compiled.alternate);
    } else if (kind == "while") {
        compiled.kind = CompiledKind::While;
        compiled.condition = compile_expr(json_string(block, "condition"), numeric_context);
        compiled.loop_limit = std::max(0, block.value("loopLimit", 1));
        compile_block_list(json_array_or_empty_local(block, "items"), numeric_context, compiled.items);
    } else if (kind == "log") {
        compiled.kind = CompiledKind::Log;
        compiled.level = json_string(block, "level");
        compiled.message = json_string(block, "message");
        compiled.expression = compile_expr(compiled.message, numeric_context);
    }
    return compiled;
}

void compile_block_list(const nlohmann::json& blocks, NumericContextStore& numeric_context, std::vector<CompiledBlock>& out)
{
    if (!blocks.is_array())
        return;
    out.reserve(blocks.size());
    for (const auto& block : blocks)
        out.push_back(compile_block(block, numeric_context));
}

void write_compiled_log_line(const std::string& level, const std::string& message)
{
    const std::string line = "[xblox] " + message;
    if (level == "error")
        logger::error(line);
    else if (level == "warn" || level == "warning")
        logger::warn(line);
    else if (level == "info")
        logger::info(line);
    else if (level == "debug")
        logger::debug(line);
    else
        logger::trace(line);
}

void execute_compiled_blocks(const std::vector<CompiledBlock>& blocks,
                             nlohmann::json& context,
                             NumericContextStore& numeric_context,
                             const ExecutionOptions& options,
                             ExecutionResult& result,
                             FlowState& flow);

void execute_compiled_block(const CompiledBlock& block,
                            nlohmann::json& context,
                            NumericContextStore& numeric_context,
                            const ExecutionOptions& options,
                            ExecutionResult& result,
                            FlowState& flow)
{
    if (flow.break_requested)
        return;
    if (cancel_requested(options)) {
        if (!flow.cancel_emitted) {
            flow.cancel_emitted = true;
            emit(result, options, ExecutionEvent{{}, "cancel", "error", "cancel requested", 130});
        }
        return;
    }

    ++result.event_count; // running
    switch (block.kind) {
    case CompiledKind::SetVariable: {
        double evaluated = 0.0;
        if (block.expression.kind != CompiledExprKind::Empty) {
            if (!eval_compiled_expr(block.expression, context, numeric_context, evaluated)) {
                emit(result, options, ExecutionEvent{{}, "setVariable", "error", "setVariable expression failed", 2});
                return;
            }
            numeric_context.slot_values[block.name_slot] = evaluated;
            numeric_context.values[block.name] = evaluated;
        } else if (block.has_value) {
            remember_numeric_value(numeric_context, block.name, block.value);
        }
        ++result.event_count; // ok
        return;
    }
    case CompiledKind::If: {
        double truthy = 0.0;
        if (eval_compiled_expr(block.condition, context, numeric_context, truthy) && std::abs(truthy) > 1e-12)
            execute_compiled_blocks(block.consequent, context, numeric_context, options, result, flow);
        else
            execute_compiled_blocks(block.alternate, context, numeric_context, options, result, flow);
        ++result.event_count; // ok
        return;
    }
    case CompiledKind::While: {
        int iterations = 0;
        for (; iterations < block.loop_limit && !flow.break_requested; ++iterations) {
            double truthy = 0.0;
            if (!eval_compiled_expr(block.condition, context, numeric_context, truthy) || std::abs(truthy) <= 1e-12)
                break;
            execute_compiled_blocks(block.items, context, numeric_context, options, result, flow);
            if ((iterations & 4095) == 0 && cancel_requested(options))
                break;
        }
        ++result.event_count; // ok
        return;
    }
    case CompiledKind::Log: {
        double evaluated = 0.0;
        std::string message = block.message.empty() ? "(empty)" : block.message;
        if (eval_compiled_expr(block.expression, context, numeric_context, evaluated)) {
            std::ostringstream os;
            os << evaluated;
            message = os.str();
        }
        write_compiled_log_line(block.level.empty() ? "trace" : block.level, message);
        ++result.event_count; // ok
        return;
    }
    case CompiledKind::Unknown:
        if (block.source)
            execute_block(*block.source, {}, context, numeric_context, options, result, flow);
        return;
    }
}

void execute_compiled_blocks(const std::vector<CompiledBlock>& blocks,
                             nlohmann::json& context,
                             NumericContextStore& numeric_context,
                             const ExecutionOptions& options,
                             ExecutionResult& result,
                             FlowState& flow)
{
    for (const auto& block : blocks) {
        if (flow.break_requested)
            break;
        const size_t errors_before = result.error_count;
        execute_compiled_block(block, context, numeric_context, options, result, flow);
        if (result.error_count > errors_before)
            break;
    }
}

bool compiled_blocks_supported(const std::vector<CompiledBlock>& blocks)
{
    for (const auto& block : blocks) {
        if (block.source && (!block_store_name(*block.source).empty() ||
                             block.source->contains("continueOnError") ||
                             block.source->contains("runFlags") ||
                             block.source->contains("onError")))
            return false;
        if (block.kind == CompiledKind::Unknown)
            return false;
        if (!compiled_blocks_supported(block.items) ||
            !compiled_blocks_supported(block.consequent) ||
            !compiled_blocks_supported(block.alternate))
            return false;
    }
    return true;
}

bool is_raw_constant_set_roots(const nlohmann::json& roots)
{
    if (!roots.is_array() || roots.size() != 1 || !roots[0].is_object())
        return false;
    const auto& root = roots[0];
    if (json_string(root, "kind") != "while" || json_string(root, "condition") != "1")
        return false;
    const auto& items = json_array_or_empty_local(root, "items");
    if (!items.is_array() || items.size() != 1 || !items[0].is_object())
        return false;
    const auto& child = items[0];
    return json_string(child, "kind") == "setVariable" && child.contains("value") && !child.contains("expression");
}

bool case_matches(const nlohmann::json& item, const nlohmann::json& context, double switch_number, bool has_switch_number, const std::string& switch_value)
{
    const std::string comparator = json_string(item, "comparator");
    const std::string expression = json_string(item, "expression");
    if (has_switch_number) {
        double rhs_number = 0.0;
        if (eval_mu_expression(expression, context, rhs_number))
            return compare_numbers(switch_number, comparator, rhs_number);
    }
    const std::string rhs = strip_quotes(expression);
    if (comparator == "!=" || comparator == "!==" || comparator == "<>")
        return switch_value != rhs;
    return switch_value == rhs;
}

std::string first_source_file(const nlohmann::json& payload, const nlohmann::json& command)
{
    if (payload.is_object() && payload.contains("file") && payload["file"].is_string())
        return payload["file"].get<std::string>();
    const nlohmann::json* source = nullptr;
    if (payload.is_object() && payload.contains("source") && payload["source"].is_object())
        source = &payload["source"];
    else if (command.is_object() && command.contains("source") && command["source"].is_object())
        source = &command["source"];
    if (source) {
        const auto files = json_string_array(*source, "files");
        if (!files.empty())
            return files.front();
    }
    return {};
}

bool extract_run_custom_command_payload(const std::string& method, nlohmann::json& payload, std::string& err)
{
    const std::string marker = "host.runCustomCommand";
    const size_t marker_pos = method.find(marker);
    if (marker_pos == std::string::npos)
        return false;
    const size_t open = method.find('(', marker_pos + marker.size());
    const size_t object_start = method.find('{', open == std::string::npos ? marker_pos : open);
    if (object_start == std::string::npos) {
        err = "host.runCustomCommand payload is missing an object";
        return false;
    }

    bool in_string = false;
    bool escaped = false;
    char quote = '\0';
    int depth = 0;
    for (size_t i = object_start; i < method.size(); ++i) {
        const char ch = method[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                in_string = false;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_string = true;
            quote = ch;
            continue;
        }
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                try {
                    payload = nlohmann::json::parse(method.substr(object_start, i - object_start + 1));
                    return true;
                } catch (const std::exception& e) {
                    err = std::string("host.runCustomCommand payload parse failed: ") + e.what();
                    return false;
                }
            }
        }
    }
    err = "host.runCustomCommand payload is unterminated";
    return false;
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring quote_process_arg(std::wstring_view arg)
{
    if (arg.empty())
        return L"\"\"";
    bool need_quotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'"') {
            need_quotes = true;
            break;
        }
    }
    if (!need_quotes)
        return std::wstring(arg);
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(ch);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring current_executable_path()
{
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    return n == 0u ? std::wstring{} : std::wstring(buf.data(), n);
}

void drain_pipe(HANDLE read_pipe, std::string& text)
{
    DWORD available = 0;
    while (::PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        char buffer[4096];
        DWORD read = 0;
        if (!::ReadFile(read_pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || read == 0)
            break;
        text.append(buffer, buffer + read);
    }
}

int run_process_command_line(std::wstring command_line,
                             const std::string& cwd,
                             const ExecutionOptions& options,
                             CommandRunOutput& output)
{
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    if (!::CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
        !::CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
        if (stdout_read) ::CloseHandle(stdout_read);
        if (stdout_write) ::CloseHandle(stdout_write);
        if (stderr_read) ::CloseHandle(stderr_read);
        if (stderr_write) ::CloseHandle(stderr_write);
        output.message = "failed to create output pipes";
        return 1;
    }
    ::SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const std::wstring wide_cwd = utf8_to_wide(cwd);
    const wchar_t* cwdp = wide_cwd.empty() ? nullptr : wide_cwd.c_str();
    if (!::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                          CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP, nullptr, cwdp, &si, &pi)) {
        ::CloseHandle(stdout_read);
        ::CloseHandle(stdout_write);
        ::CloseHandle(stderr_read);
        ::CloseHandle(stderr_write);
        output.message = "process start failed";
        return 1;
    }
    ::CloseHandle(stdout_write);
    ::CloseHandle(stderr_write);
    ::CloseHandle(pi.hThread);

    std::string stdout_text;
    std::string stderr_text;
    const auto started = std::chrono::steady_clock::now();
    DWORD wait = WAIT_TIMEOUT;
    while ((wait = ::WaitForSingleObject(pi.hProcess, 50)) == WAIT_TIMEOUT) {
        drain_pipe(stdout_read, stdout_text);
        drain_pipe(stderr_read, stderr_text);
        if (options.cancel_requested && options.cancel_requested()) {
            output.cancelled = true;
            output.message = "command cancelled";
            ::TerminateProcess(pi.hProcess, 130);
            break;
        }
        if (options.default_timeout_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= options.default_timeout_ms) {
                output.timed_out = true;
                output.message = "command timed out";
                ::TerminateProcess(pi.hProcess, 124);
                break;
            }
        }
    }
    drain_pipe(stdout_read, stdout_text);
    drain_pipe(stderr_read, stderr_text);
    DWORD code = 1;
    (void)::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(stdout_read);
    ::CloseHandle(stderr_read);
    ::CloseHandle(pi.hProcess);
    append_pipe_text(output.stdout_lines, stdout_text);
    append_pipe_text(output.stderr_lines, stderr_text);
    return static_cast<int>(code);
}
#else
std::string shell_quote_posix(const std::string& arg)
{
    if (arg.empty())
        return "''";
    std::string out = "'";
    for (char ch : arg) {
        if (ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string current_executable_path()
{
#if defined(__linux__)
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return p.string();
#endif
    return "pm-image-cli";
}

int run_shell_command(const std::string& command, CommandRunOutput& output)
{
    const fs::path base = fs::temp_directory_path() / ("pm-xblox-" + std::to_string(steady_now_ms()));
    const fs::path stdout_path = base.string() + ".out";
    const fs::path stderr_path = base.string() + ".err";
    const std::string redirected = command + " > " + shell_quote_posix(stdout_path.string()) + " 2> " + shell_quote_posix(stderr_path.string());
    const int raw = std::system(redirected.c_str());
    auto read_lines = [](const fs::path& path) {
        std::vector<std::string> lines;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return lines;
        std::ostringstream ss;
        ss << in.rdbuf();
        append_pipe_text(lines, ss.str());
        return lines;
    };
    output.stdout_lines = read_lines(stdout_path);
    output.stderr_lines = read_lines(stderr_path);
    std::error_code ec;
    fs::remove(stdout_path, ec);
    fs::remove(stderr_path, ec);
#if defined(__unix__) || defined(__APPLE__)
    if (raw == -1)
        return 1;
    if (WIFEXITED(raw))
        return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw))
        return 128 + WTERMSIG(raw);
#endif
    return raw;
}
#endif

fs::path default_cli_executable(const ExecutionOptions& options)
{
    if (!options.cli_executable_path.empty())
        return options.cli_executable_path;
#if defined(_WIN32)
    return fs::path(current_executable_path());
#else
    return fs::path(current_executable_path());
#endif
}

std::string console_quote_arg(const std::string& arg)
{
#if defined(_WIN32)
    std::string out = "'";
    for (char ch : arg) {
        if (ch == '\'')
            out += "''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
#else
    return shell_quote_posix(arg);
#endif
}

std::string wrap_console_cwd(std::string command, const std::string& cwd)
{
    if (cwd.empty())
        return command;
#if defined(_WIN32)
    return "Push-Location -LiteralPath " + console_quote_arg(cwd) + "; try { " + command + " } finally { Pop-Location }";
#else
    return "cd " + shell_quote_posix(cwd) + " && " + command;
#endif
}

bool invocation_requests_console(const CommandInvocation& invocation)
{
    if (json_bool_or(invocation.payload, "background", false) ||
        json_bool_or(invocation.command, "background", false))
        return true;
    if (invocation.payload.contains("runOptions") && invocation.payload["runOptions"].is_object()) {
        const auto& run = invocation.payload["runOptions"];
        if (json_bool_or(run, "internalConsole", false) || json_bool_or(run, "runInConsole", false))
            return true;
    }
    if (invocation.command.contains("runOptions") && invocation.command["runOptions"].is_object()) {
        const auto& run = invocation.command["runOptions"];
        if (json_bool_or(run, "internalConsole", false) || json_bool_or(run, "runInConsole", false))
            return true;
    }
    return false;
}

bool console_line_for_invocation(const CommandInvocation& invocation,
                                 const ExecutionOptions& options,
                                 std::string& line,
                                 bool& close_on_exit,
                                 std::string& err)
{
    close_on_exit = false;
    if (invocation.command.contains("runOptions") && invocation.command["runOptions"].is_object())
        close_on_exit = json_bool_or(invocation.command["runOptions"], "closeOnExit", close_on_exit);
    if (invocation.payload.contains("runOptions") && invocation.payload["runOptions"].is_object())
        close_on_exit = json_bool_or(invocation.payload["runOptions"], "closeOnExit", close_on_exit);

    if (invocation.action == "cli") {
        const std::string cli = json_string(invocation.command, "cliCommand");
        if (cli.empty()) {
            err = "cliCommand is empty";
            return false;
        }
        std::vector<std::string> args = json_string_array(invocation.command, "globalArgs");
        const std::string configured_cwd = json_string(invocation.command, "cwd");
        if (!configured_cwd.empty() && configured_cwd != ".") {
            args.push_back("--cwd");
            args.push_back(configured_cwd);
        }
        const std::string log_level = json_string(invocation.command, "logLevel");
        if (!log_level.empty() && log_level != "info") {
            args.push_back("--log-level");
            args.push_back(log_level);
        }
        const auto cli_parts = split_cli_command_path(cli);
        args.insert(args.end(), cli_parts.begin(), cli_parts.end());
        const auto stored_args = json_string_array(invocation.command, "args");
        args.insert(args.end(), stored_args.begin(), stored_args.end());
        args.insert(args.end(), invocation.extra_args.begin(), invocation.extra_args.end());
#if defined(_WIN32)
        line = "& " + console_quote_arg(default_cli_executable(options).string());
#else
        line = console_quote_arg(default_cli_executable(options).string());
#endif
        for (const auto& arg : args)
            line += " " + console_quote_arg(arg);
        return true;
    }

    if (invocation.action == "external") {
        if (!invocation.command.contains("externalCommand") || !invocation.command["externalCommand"].is_object()) {
            err = "externalCommand is missing";
            return false;
        }
        const auto& ext = invocation.command["externalCommand"];
        std::string cwd = json_string(ext, "cwd");
        if (cwd.empty())
            cwd = json_string(invocation.command, "cwd");
        if (json_string(ext, "mode") == "shell") {
            line = json_string(ext, "shellLine");
            if (line.empty()) {
                err = "externalCommand.shellLine is empty";
                return false;
            }
            line = wrap_console_cwd(std::move(line), cwd);
            return true;
        }
        const std::string command = json_string(ext, "command");
        if (command.empty()) {
            err = "externalCommand.command is empty";
            return false;
        }
#if defined(_WIN32)
        line = "& " + console_quote_arg(command);
#else
        line = console_quote_arg(command);
#endif
        for (const auto& arg : json_string_array(ext, "args"))
            line += " " + console_quote_arg(arg);
        for (const auto& arg : invocation.extra_args)
            line += " " + console_quote_arg(arg);
        line = wrap_console_cwd(std::move(line), cwd);
        return true;
    }

    err = "terminal dispatch supports cli/external commands only";
    return false;
}

int spawn_external_command(const CommandInvocation& invocation, const ExecutionOptions& options, CommandRunOutput& output)
{
    if (!invocation.command.contains("externalCommand") || !invocation.command["externalCommand"].is_object()) {
        output.message = "externalCommand is missing";
        return 2;
    }
    const auto& ext = invocation.command["externalCommand"];
    std::string cwd = json_string(ext, "cwd");
    if (cwd.empty())
        cwd = json_string(invocation.command, "cwd");
    if (json_string(ext, "mode") == "shell") {
        const std::string line = json_string(ext, "shellLine");
        if (line.empty()) {
            output.message = "externalCommand.shellLine is empty";
            return 2;
        }
        if (!options.execute_external_commands) {
            output.message = "external shell command staged";
            return 0;
        }
#if defined(_WIN32)
        std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ";
        cmd += quote_process_arg(utf8_to_wide(line));
        return run_process_command_line(std::move(cmd), cwd, options, output);
#else
        std::string cmd;
        if (!cwd.empty())
            cmd += "cd " + shell_quote_posix(cwd) + " && ";
        cmd += "/bin/sh -lc " + shell_quote_posix(line);
        return run_shell_command(cmd, output);
#endif
    }

    const std::string command = json_string(ext, "command");
    if (command.empty()) {
        output.message = "externalCommand.command is empty";
        return 2;
    }
    auto args = json_string_array(ext, "args");
    args.insert(args.end(), invocation.extra_args.begin(), invocation.extra_args.end());
    if (!options.execute_external_commands) {
        output.message = "external argv command staged";
        return 0;
    }
#if defined(_WIN32)
    std::wstring cmd = quote_process_arg(utf8_to_wide(command));
    for (const auto& arg : args) {
        cmd.push_back(L' ');
        cmd += quote_process_arg(utf8_to_wide(arg));
    }
    return run_process_command_line(std::move(cmd), cwd, options, output);
#else
    std::string cmd;
    if (!cwd.empty())
        cmd += "cd " + shell_quote_posix(cwd) + " && ";
    cmd += shell_quote_posix(command);
    for (const auto& arg : args)
        cmd += " " + shell_quote_posix(arg);
    return run_shell_command(cmd, output);
#endif
}

int spawn_cli_command(const CommandInvocation& invocation, const ExecutionOptions& options, CommandRunOutput& output)
{
    const std::string cli = json_string(invocation.command, "cliCommand");
    if (cli.empty()) {
        output.message = "cliCommand is empty";
        return 2;
    }

    std::vector<std::string> args = json_string_array(invocation.command, "globalArgs");
    const std::string configured_cwd = json_string(invocation.command, "cwd");
    if (!configured_cwd.empty() && configured_cwd != ".") {
        args.push_back("--cwd");
        args.push_back(configured_cwd);
    }
    const std::string log_level = json_string(invocation.command, "logLevel");
    if (!log_level.empty() && log_level != "info") {
        args.push_back("--log-level");
        args.push_back(log_level);
    }
    const auto cli_parts = split_cli_command_path(cli);
    args.insert(args.end(), cli_parts.begin(), cli_parts.end());
    const auto stored_args = json_string_array(invocation.command, "args");
    args.insert(args.end(), stored_args.begin(), stored_args.end());
    args.insert(args.end(), invocation.extra_args.begin(), invocation.extra_args.end());

    const fs::path exe = default_cli_executable(options);
    if (!options.execute_external_commands) {
        output.message = "cli command staged";
        return 0;
    }
#if defined(_WIN32)
    std::wstring cmd = quote_process_arg(exe.wstring());
    for (const auto& arg : args) {
        cmd.push_back(L' ');
        cmd += quote_process_arg(utf8_to_wide(arg));
    }
    return run_process_command_line(std::move(cmd), {}, options, output);
#else
    std::string cmd = shell_quote_posix(exe.string());
    for (const auto& arg : args)
        cmd += " " + shell_quote_posix(arg);
    return run_shell_command(cmd, output);
#endif
}

void emit(ExecutionResult& result, const ExecutionOptions& options, ExecutionEvent event)
{
    if (event.type.empty())
        event.type = event.kind;
    if (event.status == "error" && event.error_code == 0)
        event.error_code = event.exit_code == 0 ? 1 : event.exit_code;
    ++result.event_count;
    if (event.status != "running")
        result.last_block_result = store_value_from_result(event.data);
    if (event.status == "error") {
        ++result.error_count;
        result.ok = false;
        if (result.exit_code == 0)
            result.exit_code = event.exit_code == 0 ? 1 : event.exit_code;
    }
    if (options.event_sink)
        options.event_sink(event);
    if (options.collect_events)
        result.events.push_back(std::move(event));
}

bool cancel_requested(const ExecutionOptions& options)
{
    return options.cancel_requested && options.cancel_requested();
}

void execute_block_list(const nlohmann::json& blocks,
                        const std::string& path_prefix,
                        nlohmann::json& context,
                        NumericContextStore& numeric_context,
                        const ExecutionOptions& options,
                        ExecutionResult& result,
                        FlowState& flow);

bool emit_cancel_if_requested(const std::string& path,
                              const ExecutionOptions& options,
                              ExecutionResult& result,
                              FlowState& flow)
{
    if (!cancel_requested(options))
        return false;
    if (!flow.cancel_emitted) {
        flow.cancel_emitted = true;
        emit(result, options, ExecutionEvent{path, "cancel", "error", "cancel requested", 130});
    }
    return true;
}

RuntimeBridge& bridge_from(void* user_data)
{
    return *static_cast<RuntimeBridge*>(user_data);
}

void bridge_emit(void* user_data, ExecutionEvent event)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    emit(bridge.result, bridge.options, std::move(event));
}

void bridge_execute_block_list(void* user_data, const nlohmann::json& child_blocks, const std::string& child_path)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    execute_block_list(child_blocks, child_path, bridge.context, bridge.numeric_context, bridge.options, bridge.result, bridge.flow);
}

bool bridge_condition_truthy(void* user_data, const std::string& expression)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return condition_truthy(expression, bridge.context, &bridge.numeric_context);
}

bool bridge_numeric_value(void* user_data, const std::string& expression, double& out)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return context_numeric_value(bridge.context, expression, out, &bridge.numeric_context);
}

std::string bridge_string_value(void* user_data, const std::string& expression)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return context_string_value(bridge.context, expression);
}

bool bridge_case_matches(void* user_data,
                         const nlohmann::json& item,
                         double switch_number,
                         bool has_switch_number,
                         const std::string& switch_value)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return case_matches(item, bridge.context, switch_number, has_switch_number, switch_value);
}

void bridge_set_context_value(void* user_data, const std::string& name, nlohmann::json value)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    remember_numeric_value(bridge.numeric_context, name, value);
    set_context_value(bridge.context, name, std::move(value));
}

const nlohmann::json* bridge_context_value(void* user_data, const std::string& name)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return context_value_ptr(bridge.context, name);
}

bool bridge_eval_expression(void* user_data, const std::string& expression, double& out, std::string* err)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return eval_mu_expression(expression, bridge.context, out, err, &bridge.numeric_context);
}

bool bridge_cancel_requested(void* user_data)
{
    RuntimeBridge& bridge = bridge_from(user_data);
    return cancel_requested(bridge.options);
}

int run_callback_or_stub(const CommandHandler& handler,
                         const CommandInvocation& invocation,
                         const char* stub_message,
                         std::string& message)
{
    if (handler)
        return handler(invocation, &message);
    message = stub_message;
    return 0;
}

CommandInvocation make_invocation(const std::string& path,
                                  nlohmann::json payload,
                                  const nlohmann::json& context,
                                  const ExecutionOptions& options,
                                  std::string& err)
{
    CommandInvocation invocation;
    invocation.path = path;
    invocation.payload = payload.is_object() ? std::move(payload) : nlohmann::json::object();
    invocation.id = json_string(invocation.payload, "id");
    invocation.label = json_string(invocation.payload, "label");
    invocation.extra_args = options.extra_args;

    if (is_action_command(invocation.payload)) {
        invocation.command = invocation.payload;
    } else if (!invocation.id.empty()) {
        if (!find_command_by_id(options.custom_commands, invocation.id, invocation.command))
            invocation.command = invocation.payload;
    } else {
        invocation.command = invocation.payload;
    }

    if (invocation.id.empty())
        invocation.id = json_string(invocation.command, "id");
    if (invocation.label.empty())
        invocation.label = json_string(invocation.command, "label");
    invocation.action = action_name(invocation.command);

    media::commands::VariableContext variable_context = options.command_context;
    if (variable_context.cwd.empty() && !options.default_cwd.empty())
        variable_context.cwd = options.default_cwd.string();
    const std::string source_file = first_source_file(invocation.payload, invocation.command);
    if (!source_file.empty())
        variable_context.source_file = source_file;
    std::error_code ec;
    if (variable_context.cwd.empty()) {
        const std::string cwd = fs::current_path(ec).string();
        if (!ec)
            variable_context.cwd = cwd;
    }
    media::commands::resolve_custom_command_item_variables(invocation.command, variable_context, &err);
    (void)context;
    return invocation;
}

void execute_command_payload(const std::string& path,
                             nlohmann::json payload,
                             nlohmann::json& context,
                             NumericContextStore& numeric_context,
                             const ExecutionOptions& options,
                             ExecutionResult& result)
{
    std::string warning;
    CommandInvocation invocation = make_invocation(path, std::move(payload), context, options, warning);
    if (!warning.empty()) {
        emit(result, options, ExecutionEvent{path, "command", "warning", warning, 0, invocation.payload});
    }

    int code = 0;
    CommandRunOutput output;
    logger::info("[xblox] command dispatch id=" + invocation.id + " action=" + invocation.action + " label=" + invocation.label);
    if ((invocation.action == "cli" || invocation.action == "external") &&
        options.console_command_handler && invocation_requests_console(invocation)) {
        std::string line;
        bool close_on_exit = false;
        std::string err;
        if (console_line_for_invocation(invocation, options, line, close_on_exit, err)) {
            code = options.console_command_handler(line, true, close_on_exit, &output.message);
            if (output.message.empty())
                output.message = code == 0 ? "sent to terminal" : "terminal dispatch failed";
        } else {
            code = 2;
            output.message = std::move(err);
        }
    } else if (invocation.action == "ribbon") {
        std::string message;
        code = run_callback_or_stub(options.ribbon_command_handler, invocation, "ribbon command stubbed", message);
        output.message = std::move(message);
    } else if (invocation.action == "app") {
        std::string message;
        code = run_callback_or_stub(options.app_command_handler, invocation, "app command stubbed", message);
        output.message = std::move(message);
    } else if (invocation.action == "cli") {
        code = spawn_cli_command(invocation, options, output);
    } else if (invocation.action == "external") {
        code = spawn_external_command(invocation, options, output);
    } else if (invocation.action == "url") {
        std::string message;
        code = run_callback_or_stub(options.open_url_handler, invocation, "url open stubbed", message);
        output.message = std::move(message);
    } else if (invocation.action == "path") {
        std::string message;
        code = run_callback_or_stub(options.open_path_handler, invocation, "path open stubbed", message);
        output.message = std::move(message);
    } else {
        output.message = "metadata command stubbed";
    }
    if (code == 0)
        logger::info("[xblox] command ok id=" + invocation.id + " action=" + invocation.action + " message=" + output.message);
    else
        logger::error("[xblox] command failed id=" + invocation.id + " action=" + invocation.action + " code=" + std::to_string(code) + " message=" + output.message);

    nlohmann::json data = {
        {"id", invocation.id},
        {"label", invocation.label},
        {"action", invocation.action},
        {"payload", invocation.payload},
        {"command", invocation.command},
        {"stdout", output.stdout_lines},
        {"stderr", output.stderr_lines},
        {"timedOut", output.timed_out},
        {"cancelled", output.cancelled},
    };
    const nlohmann::json stored = {
        {"ok", code == 0},
        {"blockId", invocation.id},
        {"type", "command"},
        {"result", output.message.empty() ? invocation.action : output.message},
        {"exitCode", code},
        {"errorCode", code == 0 ? 0 : code},
        {"stdout", output.stdout_lines},
        {"stderr", output.stderr_lines},
    };
    const std::string target = json_string(invocation.payload, "storeResult").empty()
        ? json_string(invocation.payload, "target")
        : json_string(invocation.payload, "storeResult");
    if (!target.empty()) {
        remember_numeric_value(numeric_context, target, stored);
        set_context_value(context, target, stored);
    }
    emit(result, options, ExecutionEvent{
        path,
        "command",
        code == 0 ? "ok" : "error",
        output.message.empty() ? invocation.action : output.message,
        code,
        std::move(data),
        invocation.id,
        "command",
        output.stdout_lines,
        output.stderr_lines,
        code == 0 ? 0 : code,
    });
}

nlohmann::json command_payload_from_block(const nlohmann::json& block)
{
    nlohmann::json payload = nlohmann::json::object();
    if (block.is_object() && block.contains("command") && block["command"].is_object())
        payload = block["command"];
    else if (block.is_object())
        payload = block;
    if (payload.is_object()) {
        const std::string id = json_string(block, "id");
        if (!id.empty() && !payload.contains("id"))
            payload["id"] = id;
        const std::string label = json_string(block, "label");
        if (!label.empty() && !payload.contains("label"))
            payload["label"] = label;
        const std::string store_result = json_string(block, "storeResult");
        if (!store_result.empty() && !payload.contains("storeResult"))
            payload["storeResult"] = store_result;
        const std::string target = json_string(block, "target");
        if (!target.empty() && !payload.contains("target"))
            payload["target"] = target;
        if (block.is_object() && block.contains("background") && block["background"].is_boolean() && !payload.contains("background"))
            payload["background"] = block["background"];
        if (block.is_object() && block.contains("runOptions") && block["runOptions"].is_object() && !payload.contains("runOptions"))
            payload["runOptions"] = block["runOptions"];
    }
    return payload;
}

void execute_block_list(const nlohmann::json& blocks,
                        const std::string& path_prefix,
                        nlohmann::json& context,
                        NumericContextStore& numeric_context,
                        const ExecutionOptions& options,
                        ExecutionResult& result,
                        FlowState& flow);

void store_block_result_if_requested(const nlohmann::json& block,
                                     nlohmann::json& context,
                                     NumericContextStore& numeric_context,
                                     const ExecutionResult& result)
{
    const std::string name = block_store_name(block);
    if (name.empty())
        return;
    nlohmann::json value = result.last_block_result;
    remember_numeric_value(numeric_context, name, value);
    set_context_value(context, name, std::move(value));
}

void execute_background_block(nlohmann::json block,
                              std::string path,
                              nlohmann::json context,
                              ExecutionOptions options)
{
    std::thread([block = std::move(block), path = std::move(path), context = std::move(context), options = std::move(options)]() mutable {
        ExecutionResult background_result;
        FlowState background_flow;
        NumericContextStore background_numeric_context;
        seed_numeric_context(context, background_numeric_context);
        execute_block(block, path, context, background_numeric_context, options, background_result, background_flow);
    }).detach();
}

void execute_block(const nlohmann::json& block,
                   const std::string& path,
                   nlohmann::json& context,
                   NumericContextStore& numeric_context,
                   const ExecutionOptions& options,
                   ExecutionResult& result,
                   FlowState& flow)
{
    if (emit_cancel_if_requested(path, options, result, flow))
        return;
    if (!block.is_object()) {
        emit(result, options, ExecutionEvent{path, "unknown", "error", "block is not an object", 2});
        return;
    }
    const std::string kind = json_string(block, "kind");
    if (options.collect_events || options.event_sink)
        emit(result, options, ExecutionEvent{path, kind, "running", {}});
    else
        ++result.event_count;

    if (kind == "runScript") {
        emit(result, options, ExecutionEvent{path, kind, "warning", "runScript is legacy and is not executed by the native runtime"});
        store_block_result_if_requested(block, context, numeric_context, result);
        return;
    }

    if (kind == "command") {
        execute_command_payload(path, command_payload_from_block(block), context, numeric_context, options, result);
        store_block_result_if_requested(block, context, numeric_context, result);
        return;
    }

    if (const blocks::BlockHandler builtin = blocks::builtin_block_handler(kind)) {
        RuntimeBridge bridge{context, numeric_context, options, result, flow};
        blocks::BlockRuntime runtime{
            context,
            options,
            result,
            flow.break_requested,
            options.collect_events || options.event_sink,
            &bridge,
            bridge_emit,
            bridge_execute_block_list,
            bridge_condition_truthy,
            bridge_numeric_value,
            bridge_string_value,
            bridge_case_matches,
            bridge_set_context_value,
            bridge_context_value,
            bridge_eval_expression,
            bridge_cancel_requested,
        };
        if (builtin(block, path, runtime)) {
            (void)emit_cancel_if_requested(path, options, result, flow);
            store_block_result_if_requested(block, context, numeric_context, result);
            return;
        }
    }

    if (kind == "case" || kind == "switchDefault") {
        execute_block_list(block.value("consequent", nlohmann::json::array()), path + "/consequent", context, numeric_context, options, result, flow);
        emit(result, options, ExecutionEvent{path, kind, result.ok ? "ok" : "error", kind + " complete"});
        store_block_result_if_requested(block, context, numeric_context, result);
        return;
    }

    emit(result, options, ExecutionEvent{path, kind.empty() ? "unknown" : kind, "warning", "unknown block kind skipped"});
    store_block_result_if_requested(block, context, numeric_context, result);
}

void execute_block_list(const nlohmann::json& blocks,
                        const std::string& path_prefix,
                        nlohmann::json& context,
                        NumericContextStore& numeric_context,
                        const ExecutionOptions& options,
                        ExecutionResult& result,
                        FlowState& flow)
{
    if (!blocks.is_array())
        return;
    static const std::string empty_path;
    const bool needs_paths = options.collect_events || options.event_sink;
    for (size_t i = 0; i < blocks.size() && !flow.break_requested; ++i) {
        const size_t errors_before = result.error_count;
        const BlockRunFlags flags = block_run_flags(blocks[i]);
        if (!flags.enabled)
            continue;
        const std::string path = needs_paths
            ? (path_prefix.empty() ? std::to_string(i) : path_prefix + "/" + std::to_string(i))
            : empty_path;
        if (flags.background) {
            execute_background_block(blocks[i], path, context, options);
            continue;
        }
        if (needs_paths) {
            execute_block(blocks[i], path, context, numeric_context, options, result, flow);
        } else {
            execute_block(blocks[i], empty_path, context, numeric_context, options, result, flow);
        }
        if (flags.abort_on_error && result.error_count > errors_before)
            break;
    }
}

void append_plan(const nlohmann::json& blocks, const std::string& path_prefix, std::vector<ExecutionEvent>& out)
{
    if (!blocks.is_array())
        return;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const std::string path = path_prefix.empty() ? std::to_string(i) : path_prefix + "/" + std::to_string(i);
        const auto& block = blocks[i];
        const std::string kind = json_string(block, "kind");
        out.push_back(ExecutionEvent{path, kind, "planned", {}});
        if (kind == "if") {
            append_plan(block.value("consequent", nlohmann::json::array()), path + "/consequent", out);
            append_plan(block.value("alternate", nlohmann::json::array()), path + "/alternate", out);
        } else if (kind == "for" || kind == "while") {
            append_plan(block.value("items", nlohmann::json::array()), path + "/items", out);
        } else if (kind == "switch") {
            const auto items = block.value("items", nlohmann::json::array());
            if (items.is_array()) {
                for (size_t j = 0; j < items.size(); ++j)
                    append_plan(items[j].value("consequent", nlohmann::json::array()), path + "/items/" + std::to_string(j) + "/consequent", out);
            }
        }
    }
}

} // namespace

ExecutionResult run_blocks_file(const nlohmann::json& blocks_file, const ExecutionOptions& options)
{
    nlohmann::json context = blocks_file.is_object() ? blocks_file.value("context", nlohmann::json::object()) : nlohmann::json::object();
    const nlohmann::json roots = blocks_file.is_object() ? blocks_file.value("roots", nlohmann::json::array()) : nlohmann::json::array();
    return run_block_roots(roots, context, options);
}

ExecutionResult run_block_roots(const nlohmann::json& roots, const nlohmann::json& context, const ExecutionOptions& options)
{
    ExecutionResult result;
    FlowState flow;
    nlohmann::json mutable_context = context.is_object() ? context : nlohmann::json::object();
    NumericContextStore numeric_context;
    seed_numeric_context(mutable_context, numeric_context);
    execute_block_list(roots, {}, mutable_context, numeric_context, options, result, flow);
    return result;
}

std::vector<ExecutionEvent> build_execution_plan(const nlohmann::json& blocks_file)
{
    std::vector<ExecutionEvent> out;
    const nlohmann::json roots = blocks_file.is_object() ? blocks_file.value("roots", nlohmann::json::array()) : blocks_file;
    append_plan(roots, {}, out);
    return out;
}

} // namespace media::xblox
