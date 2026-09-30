#pragma once

#include "runtime.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace media::xblox::blocks {

struct BlockRuntime {
    using EmitFn = void (*)(void*, ExecutionEvent);
    using ExecuteBlockListFn = void (*)(void*, const nlohmann::json&, const std::string&);
    using ConditionTruthyFn = bool (*)(void*, const std::string&);
    using NumericValueFn = bool (*)(void*, const std::string&, double&);
    using StringValueFn = std::string (*)(void*, const std::string&);
    using CaseMatchesFn = bool (*)(void*, const nlohmann::json&, double, bool, const std::string&);
    using SetContextValueFn = void (*)(void*, const std::string&, nlohmann::json);
    using ContextValueFn = const nlohmann::json* (*)(void*, const std::string&);
    using EvalExpressionFn = bool (*)(void*, const std::string&, double&, std::string*);
    using CancelRequestedFn = bool (*)(void*);

    nlohmann::json& context;
    const ExecutionOptions& options;
    ExecutionResult& result;
    bool& break_requested;
    bool events_enabled = true;
    void* user_data = nullptr;

    EmitFn emit = nullptr;
    ExecuteBlockListFn execute_block_list = nullptr;
    ConditionTruthyFn condition_truthy = nullptr;
    NumericValueFn numeric_value = nullptr;
    StringValueFn string_value = nullptr;
    CaseMatchesFn case_matches = nullptr;
    SetContextValueFn set_context_value = nullptr;
    ContextValueFn context_value = nullptr;
    EvalExpressionFn eval_expression = nullptr;
    CancelRequestedFn cancel_requested = nullptr;
};

using BlockHandler = bool (*)(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime);

struct BlockDescriptor {
    std::string kind;
    std::string label;
    std::string description;
    std::string group;
    nlohmann::json default_block = nlohmann::json::object();
    nlohmann::json params = nlohmann::json::array();
    nlohmann::json flags = nlohmann::json::object();
    std::uint32_t platform_mask = 0xffffffffu;
    bool palette_visible = true;
    BlockHandler handler = nullptr;
};

using BlockRegistry = std::unordered_map<std::string, BlockDescriptor>;

inline BlockDescriptor block_descriptor(std::string kind,
                                        BlockHandler handler,
                                        std::string label = {},
                                        std::string group = {},
                                        std::string description = {},
                                        nlohmann::json default_block = nlohmann::json::object(),
                                        nlohmann::json params = nlohmann::json::array(),
                                        nlohmann::json flags = nlohmann::json::object(),
                                        std::uint32_t platform_mask = 0xffffffffu,
                                        bool palette_visible = true)
{
    BlockDescriptor descriptor;
    descriptor.kind = std::move(kind);
    descriptor.label = std::move(label);
    descriptor.description = std::move(description);
    descriptor.group = std::move(group);
    descriptor.default_block = std::move(default_block);
    descriptor.params = std::move(params);
    descriptor.flags = std::move(flags);
    descriptor.platform_mask = platform_mask;
    descriptor.palette_visible = palette_visible;
    descriptor.handler = handler;
    return descriptor;
}

BlockHandler builtin_block_handler(const std::string& kind);
void register_builtin_blocks(BlockRegistry& registry);
std::vector<BlockDescriptor> registered_block_definitions();

} // namespace media::xblox::blocks
