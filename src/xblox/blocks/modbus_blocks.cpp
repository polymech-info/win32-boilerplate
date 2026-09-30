#include "blocks/modbus_blocks.hpp"

#include "modbus/modbus_client.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace media::xblox::blocks {
namespace {

std::string json_string(const nlohmann::json& o, const char* key, std::string fallback = {})
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::move(fallback);
}

int json_int(const nlohmann::json& o, const char* key, int fallback = 0)
{
    if (!o.is_object() || !o.contains(key))
        return fallback;
    const auto& v = o[key];
    if (v.is_number_integer())
        return v.get<int>();
    if (v.is_number_unsigned())
        return static_cast<int>(v.get<std::uint64_t>());
    if (v.is_number_float())
        return static_cast<int>(v.get<double>());
    return fallback;
}

bool json_bool(const nlohmann::json& o, const char* key, bool fallback = false)
{
    return o.is_object() && o.contains(key) && o[key].is_boolean() ? o[key].get<bool>() : fallback;
}

std::vector<std::uint16_t> uint16_array(const nlohmann::json& block, const char* key)
{
    std::vector<std::uint16_t> out;
    if (!block.is_object() || !block.contains(key) || !block[key].is_array())
        return out;
    for (const auto& value : block[key]) {
        if (value.is_number_integer() || value.is_number_unsigned())
            out.push_back(static_cast<std::uint16_t>(value.get<int>()));
    }
    return out;
}

std::vector<std::uint8_t> uint8_array(const nlohmann::json& block, const char* key)
{
    std::vector<std::uint8_t> out;
    if (!block.is_object() || !block.contains(key) || !block[key].is_array())
        return out;
    for (const auto& value : block[key]) {
        if (value.is_boolean())
            out.push_back(value.get<bool>() ? 1 : 0);
        else if (value.is_number_integer() || value.is_number_unsigned())
            out.push_back(value.get<int>() ? 1 : 0);
    }
    return out;
}

void emit_event(BlockRuntime& runtime,
                const std::string& path,
                const std::string& kind,
                bool ok,
                std::string message,
                nlohmann::json payload,
                const std::string& block_id)
{
    payload["ok"] = ok;
    payload["kind"] = kind;
    ExecutionEvent event;
    event.path = path;
    event.kind = kind;
    event.status = ok ? "ok" : "error";
    event.message = std::move(message);
    event.exit_code = ok ? 0 : 1;
    event.error_code = ok ? 0 : 1;
    event.data = std::move(payload);
    event.block_id = block_id;
    event.type = "modbus";
    runtime.emit(runtime.user_data, std::move(event));
}

void emit_error(BlockRuntime& runtime,
                const std::string& path,
                const std::string& kind,
                std::string message,
                const std::string& block_id)
{
    emit_event(runtime, path, kind, false, message, {{"error", std::move(message)}}, block_id);
}

void attach_result(BlockRuntime& runtime, nlohmann::json& payload, nlohmann::json value)
{
    payload["result"] = value;
    runtime.set_context_value(runtime.user_data, "PREVIOUS", std::move(value));
}

media::modbus::ServerOptions server_options_from_block(const nlohmann::json& block, std::string& err)
{
    media::modbus::ServerOptions options;
    const std::string url = json_string(block, "url", "tcp:127.0.0.1:15020");
    if (!media::modbus::parse_endpoint(url, options.endpoint, err))
        return options;
    options.unit_id = json_int(block, "unitId", json_int(block, "slave", options.endpoint.slave));
    options.holding_register_count = json_int(block, "registerCount", 128);
    options.coil_count = json_int(block, "coilCount", 128);
    options.holding_registers = uint16_array(block, "holdingRegisters");
    if (options.holding_registers.empty())
        options.holding_registers = uint16_array(block, "registers");
    options.coils = uint8_array(block, "coils");
    options.duration_ms = json_int(block, "durationMs", json_int(block, "duration_ms", 0));
    options.max_requests = json_int(block, "maxRequests", json_int(block, "max_requests", 0));
    options.poll_timeout_ms = json_int(block, "pollTimeoutMs", 100);
    options.debug = json_bool(block, "debug");
    return options;
}

bool modbus_server_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = json_string(block, "id");
    std::string err;
    auto options = server_options_from_block(block, err);
    if (!err.empty())
        return (emit_error(runtime, path, "modbusServer", "modbusServer: " + err, id), true);

    media::modbus::ServerStats stats;
    const bool ok = media::modbus::run_tcp_server(options,
        [&runtime]() { return runtime.cancel_requested && runtime.cancel_requested(runtime.user_data); },
        stats);

    nlohmann::json payload{
        {"tool", "modbus_server"},
        {"url", stats.url},
        {"requests", stats.requests},
        {"replies", stats.replies},
        {"connections", stats.connections},
        {"elapsedMs", stats.elapsed_ms},
    };
    if (!stats.error.empty())
        payload["error"] = stats.error;
    attach_result(runtime, payload, nlohmann::json{
        {"url", stats.url},
        {"requests", stats.requests},
        {"replies", stats.replies},
        {"connections", stats.connections},
    });
    emit_event(runtime, path, "modbusServer", ok, ok ? "modbus server stopped" : stats.error, std::move(payload), id);
    return true;
}

bool modbus_read_holding_registers_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = json_string(block, "id");
    const std::string url = json_string(block, "url");
    if (url.empty())
        return (emit_error(runtime, path, "modbusReadHoldingRegisters", "modbusReadHoldingRegisters: 'url' is required", id), true);
    const int address = json_int(block, "address", 0);
    const int count = std::max(1, json_int(block, "count", 1));
    const int slave = json_int(block, "slave", json_int(block, "unitId", 0));
    const int timeout_ms = json_int(block, "timeoutMs", runtime.options.default_timeout_ms);

    std::vector<std::uint16_t> values;
    std::string err;
    const bool ok = media::modbus::read_holding_registers(url, address, count, slave, timeout_ms, values, err);
    if (!ok)
        return (emit_error(runtime, path, "modbusReadHoldingRegisters", "modbusReadHoldingRegisters: " + err, id), true);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto value : values)
        arr.push_back(value);
    nlohmann::json payload{{"tool", "modbus_read_holding_registers"}, {"url", url}, {"address", address}, {"count", count}, {"values", arr}};
    attach_result(runtime, payload, arr);
    emit_event(runtime, path, "modbusReadHoldingRegisters", true, "holding registers read", std::move(payload), id);
    return true;
}

bool modbus_write_register_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = json_string(block, "id");
    const std::string url = json_string(block, "url");
    if (url.empty())
        return (emit_error(runtime, path, "modbusWriteRegister", "modbusWriteRegister: 'url' is required", id), true);
    const int address = json_int(block, "address", 0);
    const auto value = static_cast<std::uint16_t>(json_int(block, "value", 0));
    const int slave = json_int(block, "slave", json_int(block, "unitId", 0));
    const int timeout_ms = json_int(block, "timeoutMs", runtime.options.default_timeout_ms);

    std::string err;
    const bool ok = media::modbus::write_register(url, address, value, slave, timeout_ms, err);
    if (!ok)
        return (emit_error(runtime, path, "modbusWriteRegister", "modbusWriteRegister: " + err, id), true);

    nlohmann::json result{{"address", address}, {"value", value}};
    nlohmann::json payload{{"tool", "modbus_write_register"}, {"url", url}, {"address", address}, {"value", value}};
    attach_result(runtime, payload, result);
    emit_event(runtime, path, "modbusWriteRegister", true, "register written", std::move(payload), id);
    return true;
}

bool modbus_write_registers_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = json_string(block, "id");
    const std::string url = json_string(block, "url");
    if (url.empty())
        return (emit_error(runtime, path, "modbusWriteRegisters", "modbusWriteRegisters: 'url' is required", id), true);
    const int address = json_int(block, "address", 0);
    const auto values = uint16_array(block, "values");
    const int slave = json_int(block, "slave", json_int(block, "unitId", 0));
    const int timeout_ms = json_int(block, "timeoutMs", runtime.options.default_timeout_ms);
    if (values.empty())
        return (emit_error(runtime, path, "modbusWriteRegisters", "modbusWriteRegisters: 'values' is required", id), true);

    std::string err;
    const bool ok = media::modbus::write_registers(url, address, values, slave, timeout_ms, err);
    if (!ok)
        return (emit_error(runtime, path, "modbusWriteRegisters", "modbusWriteRegisters: " + err, id), true);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto value : values)
        arr.push_back(value);
    nlohmann::json result{{"address", address}, {"values", arr}};
    nlohmann::json payload{{"tool", "modbus_write_registers"}, {"url", url}, {"address", address}, {"values", arr}};
    attach_result(runtime, payload, result);
    emit_event(runtime, path, "modbusWriteRegisters", true, "registers written", std::move(payload), id);
    return true;
}

nlohmann::json runtime_params()
{
    return nlohmann::json::array({
        {{"name", "enabled"}, {"type", "boolean"}, {"default", true}},
        {{"name", "continueOnError"}, {"type", "boolean"}, {"default", false}},
        {{"name", "onError"}, {"type", "string"}},
        {{"name", "storeAs"}, {"type", "string"}},
        {{"name", "background"}, {"type", "boolean"}, {"default", false}},
    });
}

nlohmann::json append_runtime_params(nlohmann::json params)
{
    for (const auto& param : runtime_params())
        params.push_back(param);
    return params;
}

struct Spec {
    const char* kind;
    const char* label;
    const char* description;
    BlockHandler handler;
    nlohmann::json default_block;
    nlohmann::json params;
};

const std::vector<Spec>& specs()
{
    static const std::vector<Spec> items = {
        {"modbusServer", "Modbus Server", "Run a Modbus TCP server until cancelled, durationMs expires, or maxRequests is reached.",
            modbus_server_block,
            {{"kind", "modbusServer"}, {"url", "tcp:127.0.0.1:15020"}, {"holdingRegisters", {11, 22, 33}}, {"durationMs", 3000}},
            {{{"name", "url"}, {"type", "string"}, {"default", "tcp:127.0.0.1:15020"}},
             {{"name", "holdingRegisters"}, {"type", "integer[]"}},
             {{"name", "registerCount"}, {"type", "integer"}, {"default", 128}},
             {{"name", "durationMs"}, {"type", "integer"}, {"default", 0}},
             {{"name", "maxRequests"}, {"type", "integer"}, {"default", 0}},
             {{"name", "unitId"}, {"type", "integer"}, {"default", 1}}}},
        {"modbusReadHoldingRegisters", "Modbus Read Holding Registers", "Read holding registers from a Modbus TCP/RTU endpoint.",
            modbus_read_holding_registers_block,
            {{"kind", "modbusReadHoldingRegisters"}, {"url", "tcp:127.0.0.1:15020"}, {"address", 0}, {"count", 3}, {"storeAs", "values"}},
            {{{"name", "url"}, {"type", "string"}, {"required", true}},
             {{"name", "address"}, {"type", "integer"}, {"default", 0}},
             {{"name", "count"}, {"type", "integer"}, {"default", 1}},
             {{"name", "slave"}, {"type", "integer"}},
             {{"name", "timeoutMs"}, {"type", "integer"}, {"default", 30000}}}},
        {"modbusWriteRegister", "Modbus Write Register", "Write a single holding register.",
            modbus_write_register_block,
            {{"kind", "modbusWriteRegister"}, {"url", "tcp:127.0.0.1:15020"}, {"address", 0}, {"value", 123}},
            {{{"name", "url"}, {"type", "string"}, {"required", true}},
             {{"name", "address"}, {"type", "integer"}, {"default", 0}},
             {{"name", "value"}, {"type", "integer"}, {"required", true}},
             {{"name", "slave"}, {"type", "integer"}},
             {{"name", "timeoutMs"}, {"type", "integer"}, {"default", 30000}}}},
        {"modbusWriteRegisters", "Modbus Write Registers", "Write multiple holding registers.",
            modbus_write_registers_block,
            {{"kind", "modbusWriteRegisters"}, {"url", "tcp:127.0.0.1:15020"}, {"address", 0}, {"values", {101, 202}}},
            {{{"name", "url"}, {"type", "string"}, {"required", true}},
             {{"name", "address"}, {"type", "integer"}, {"default", 0}},
             {{"name", "values"}, {"type", "integer[]"}, {"required", true}},
             {{"name", "slave"}, {"type", "integer"}},
             {{"name", "timeoutMs"}, {"type", "integer"}, {"default", 30000}}}},
    };
    return items;
}

} // namespace

BlockHandler modbus_block_handler(const std::string& kind)
{
    for (const auto& spec : specs()) {
        if (kind == spec.kind)
            return spec.handler;
    }
    return nullptr;
}

void register_modbus_blocks(BlockRegistry& registry)
{
    for (const auto& spec : specs()) {
        registry[spec.kind] = block_descriptor(
            spec.kind,
            spec.handler,
            spec.label,
            "Modbus",
            spec.description,
            spec.default_block,
            append_runtime_params(spec.params));
    }
}

} // namespace media::xblox::blocks
