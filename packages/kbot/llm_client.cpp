#include "llm_client.h"
#include "logger/logger.h"
#include <curl/curl.h>
#include <curl/websockets.h>
#include <liboai.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <string_view>

namespace polymech {
namespace kbot {

/** OpenRouter bills against a completion ceiling; omitting `max_tokens` lets many models default to ~64k,
 *  which can exceed the balance check ("need more credits or fewer max_tokens") even with credits left.
 *  For chat we still keep a sane ceiling; for tool-agent loops we use the max OpenRouter accepts so that
 *  large `app_batch` payloads (20-30+ keystroke steps ≈ ~1500-2500 output tokens of JSON) are never
 *  truncated mid-arguments — truncation would arrive as `{}` and cause the agent to retry the same plan
 *  in a loop. uint16_t maxes at 65535 which is well over what any model emits in one response. */
static std::optional<uint16_t> max_tokens_cap_for_router(std::string_view router, bool tool_agent_loop)
{
    if (router != "openrouter")
        return std::nullopt;
    if (tool_agent_loop)
        return static_cast<uint16_t>(65535);
    return static_cast<uint16_t>(16384);
}

/** Rough size of what OpenRouter counts as input (not just the user's last sentence): messages plus the
 *  entire `tools` JSON. MCP + path tools often contribute hundreds of KB of schema text. */
static size_t approximate_openrouter_input_chars(
    const std::vector<ChatMessage>& messages,
    const nlohmann::json& tools)
{
    size_t n = 0;
    for (const auto& m : messages) {
        n += m.role.size() + m.content.size() + m.tool_call_id.size();
        if (!m.tool_calls.empty())
            n += m.tool_calls.dump().size();
    }
    if (tools.is_array() && !tools.empty())
        n += tools.dump().size();
    return n;
}

/** OpenRouter preflight: reserved completion tokens must fit under a balance-derived ceiling once input
 *  (including tool definitions) is accounted for. Shrink max_tokens when the serialized payload is large.
 *
 *  Tool-agent loops do NOT shrink — they need full headroom for multi-step batch payloads. Any cap below
 *  the actual response size truncates assistant arguments to `{}` and breaks tool calls. The chat path
 *  (no tools) keeps the bucket table since most replies are short and balance checks still apply. */
static uint16_t openrouter_shrink_max_tokens_for_payload(uint16_t base_cap,
                                                         size_t   approx_chars,
                                                         bool     tool_agent_loop)
{
    if (tool_agent_loop)
        return base_cap;                      // no shrinking for tool loops; let the model emit freely
    uint16_t cap = base_cap;
    /* Parenthesize std::min / std::max so Windows headers cannot break them with min/max macros. */
    if (approx_chars > 180000)
        cap = (std::min)(cap, static_cast<uint16_t>(384));
    else if (approx_chars > 110000)
        cap = (std::min)(cap, static_cast<uint16_t>(512));
    else if (approx_chars > 70000)
        cap = (std::min)(cap, static_cast<uint16_t>(768));
    else if (approx_chars > 42000)
        cap = (std::min)(cap, static_cast<uint16_t>(1024));
    else if (approx_chars > 24000)
        cap = (std::min)(cap, static_cast<uint16_t>(2048));
    else if (approx_chars > 12000)
        cap = (std::min)(cap, static_cast<uint16_t>(4096));
    return (std::max)(static_cast<uint16_t>(256), cap);
}

/** Backwards-compatible overload for non-tool-loop callers. */
static uint16_t openrouter_shrink_max_tokens_for_payload(uint16_t base_cap, size_t approx_chars)
{
    return openrouter_shrink_max_tokens_for_payload(base_cap, approx_chars, false);
}

static std::string realtime_base_url_for(const std::string& base_url) {
    if (base_url.empty())
        return "wss://api.openai.com/v1/realtime";
    std::string u = base_url;
    const auto scheme_pos = u.find("://");
    if (scheme_pos != std::string::npos) {
        const std::string scheme = u.substr(0, scheme_pos);
        if (scheme == "https") u.replace(0, scheme_pos, "wss");
        else if (scheme == "http") u.replace(0, scheme_pos, "ws");
    }
    while (!u.empty() && u.back() == '/') u.pop_back();
    if (u.size() < 9 || u.substr(u.size() - 9) != "/realtime")
        u += "/realtime";
    return u;
}

struct RealtimePromptParts {
    std::string system;
    std::string user;
};

static RealtimePromptParts derive_realtime_prompt_parts(const std::vector<ChatMessage>& messages) {
    RealtimePromptParts out;
    std::string latest_user;
    std::string latest_system;
    for (const auto& m : messages) {
        if (m.role == "system" && !m.content.empty())
            latest_system = m.content;
        else if (m.role == "user" && !m.content.empty())
            latest_user = m.content;
    }
    out.system = std::move(latest_system);
    out.user = std::move(latest_user);
    return out;
}

LLMClient::LLMClient(const KBotOptions& opts)
    : api_key_(opts.api_key),
      model_(opts.model),
      router_(opts.router),
      llm_timeout_ms_(opts.llm_timeout_ms),
      response_format_json_(opts.response_format_json) {
    
    // Set default base_url_ according to client.ts mappings
    if (opts.base_url.empty()) {
        if (router_ == "openrouter")      base_url_ = "https://openrouter.ai/api/v1";
        else if (router_ == "openai")     base_url_ = ""; // liboai uses the default URL automatically
        else if (router_ == "deepseek")   base_url_ = "https://api.deepseek.com/v1";
        else if (router_ == "huggingface")base_url_ = "https://api-inference.huggingface.co/v1";
        else if (router_ == "ollama")     base_url_ = "http://localhost:11434/v1";
        else if (router_ == "fireworks")  base_url_ = "https://api.fireworks.ai/v1";
        else if (router_ == "gemini")     base_url_ = "https://generativelanguage.googleapis.com/v1beta"; // or gemini openai compat endpt
        else if (router_ == "xai")        base_url_ = "https://api.x.ai/v1";
        else                              base_url_ = "https://api.openai.com/v1"; // Fallback to openai API
    } else {
        base_url_ = opts.base_url;
    }

    // Default models based on router (from client.ts)
    if (model_.empty()) {
        if (router_ == "openrouter")       model_ = "anthropic/claude-sonnet-4";
        else if (router_ == "openai")      model_ = "gpt-4o";
        else if (router_ == "deepseek")    model_ = "deepseek-chat";
        else if (router_ == "huggingface") model_ = "meta-llama/2";
        else if (router_ == "ollama")      model_ = "llama3.2";
        else if (router_ == "fireworks")   model_ = "llama-v2-70b-chat";
        else if (router_ == "gemini")      model_ = "gemini-1.5-pro";
        else if (router_ == "xai")         model_ = "grok-1";
        else                               model_ = "gpt-4o";
    }
}

LLMClient::~LLMClient() = default;

LLMResponse LLMClient::execute_chat(const std::string& prompt) {
    LLMResponse res;
    
    logger::debug("LLMClient::execute_chat: Starting. api_key length: " + std::to_string(api_key_.length()));
    if (api_key_.empty()) {
        res.success = false;
        res.error = "API Key is empty.";
        return res;
    }

    logger::debug("LLMClient::execute_chat: base_url_: " + base_url_);
    liboai::OpenAI oai_impl(base_url_.empty() ? "https://api.openai.com/v1" : base_url_);

    logger::debug("LLMClient::execute_chat: Setting API Key");
    bool success = oai_impl.auth.SetKey(api_key_);
    if (!success) {
        res.success = false;
        res.error = "Failed to set API Key in liboai.";
        return res;
    }

    if (llm_timeout_ms_ > 0) {
        oai_impl.auth.SetMaxTimeout(llm_timeout_ms_);
        logger::info("LLMClient: HTTP timeout set to " + std::to_string(llm_timeout_ms_) + " ms");
    }

    std::string target_model = model_.empty() ? "gpt-4o" : model_;
    logger::debug("LLMClient::execute_chat: Target model: " + target_model);

    logger::info("LLMClient: calling ChatCompletion (prompt chars=" + std::to_string(prompt.size()) + ")");
    logger::debug("LLMClient::execute_chat: Init Conversation");
    liboai::Conversation convo;
    convo.AddUserData(prompt);

    std::optional<nlohmann::json> response_format;
    if (!response_format_json_.empty()) {
        try {
            response_format = nlohmann::json::parse(response_format_json_);
        } catch (const std::exception& e) {
            logger::warn("LLMClient: invalid --response-format / response_format_json, ignoring: " +
                         std::string(e.what()));
        }
    }

    logger::debug("LLMClient::execute_chat: Calling create()");
    const std::optional<uint16_t> max_out = max_tokens_cap_for_router(router_, false);
    try {
        liboai::Response response = oai_impl.ChatCompletion->create(
            target_model,
            convo,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            max_out,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            response_format);
        logger::info("LLMClient: ChatCompletion returned (HTTP " + std::to_string(response.status_code) + ")");
        logger::debug("LLMClient::execute_chat: Got response with status: " + std::to_string(response.status_code));

        // liboai may not populate raw_json for custom base URLs — parse content directly.
        nlohmann::json j;
        bool json_ok = false;
        if (!response.raw_json.empty() && response.raw_json.contains("choices")) {
            j = response.raw_json;
            json_ok = true;
        } else if (!response.content.empty()) {
            try {
                j = nlohmann::json::parse(response.content);
                json_ok = j.contains("choices");
            } catch (...) {}
        }

        if (!json_ok || j["choices"].empty()) {
            res.success = false;
            if (json_ok && j.contains("error")) {
                res.error = "API Error: " + j["error"].dump();
            } else {
                res.error = "Invalid response format: no choices found. Raw: " + response.content;
            }
            return res;
        }

        res.success = true;
        res.text = j["choices"][0]["message"]["content"].get<std::string>();

        /* Usage, model, cost (OpenRouter), etc. — everything except message bodies in choices. */
        try {
            nlohmann::json meta = nlohmann::json::object();
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key() == "choices") continue;
                meta[it.key()] = it.value();
            }
            if (!meta.empty()) res.provider_meta_json = meta.dump();
        } catch (...) {
            /* keep text; omit provider_meta_json */
        }

    } catch (std::exception& e) {
        logger::error("LLMClient::execute_chat: Exception caught: " + std::string(e.what()));
        res.success = false;
        res.error = e.what();
    } catch (...) {
        logger::error("LLMClient::execute_chat: Unknown exception caught");
        res.success = false;
        res.error = "Unknown error occurred inside LLMClient execute_chat.";
    }

    return res;
}

// ── execute_chat_messages — multi-turn / multi-tool ────────────────────────

LLMResponse LLMClient::execute_chat_messages(
    const std::vector<ChatMessage>& messages,
    const nlohmann::json& tools,
    const nlohmann::json& tool_choice)
{
    LLMResponse res;

    if (api_key_.empty()) {
        res.success = false;
        res.error = "API Key is empty.";
        return res;
    }
    if (messages.empty()) {
        res.success = false;
        res.error = "messages is empty (need at least a user message).";
        return res;
    }

    liboai::OpenAI oai_impl(base_url_.empty() ? "https://api.openai.com/v1" : base_url_);
    if (!oai_impl.auth.SetKey(api_key_)) {
        res.success = false;
        res.error = "Failed to set API Key in liboai.";
        return res;
    }
    if (llm_timeout_ms_ > 0) {
        oai_impl.auth.SetMaxTimeout(llm_timeout_ms_);
        logger::info("LLMClient: HTTP timeout set to " + std::to_string(llm_timeout_ms_) + " ms");
    }

    const std::string target_model = model_.empty() ? "gpt-4o" : model_;
    const std::string effective_url = (base_url_.empty() ? std::string("https://api.openai.com/v1") : base_url_)
                                       + "/chat/completions";
    const int n_tools = tools.is_array() ? (int)tools.size() : 0;
    logger::info("LLMClient[chat]: POST " + effective_url
                 + " model=" + target_model
                 + " msgs=" + std::to_string(messages.size())
                 + " tools=" + std::to_string(n_tools)
                 + " router=" + (router_.empty() ? "?" : router_)
                 + " timeout_ms=" + std::to_string(llm_timeout_ms_));
    // Debug: log tool names so schema errors are traceable.
    if (n_tools > 0) {
        std::string tool_names;
        for (const auto& t : tools) {
            if (!tool_names.empty()) tool_names += ", ";
            if (t.contains("function") && t["function"].is_object())
                tool_names += t["function"].value("name", std::string("?"));
            else
                tool_names += t.value("name", std::string("?"));
        }
        logger::debug("LLMClient[chat]: tools=[" + tool_names + "]");
    }

    // Build a Conversation by hand so we can express the tool-role messages
    // and the assistant tool_calls turns that liboai's higher-level
    // SetSystemData / AddUserData helpers don't cover.
    liboai::Conversation convo;
    {
        nlohmann::json msgs = nlohmann::json::array();
        for (const auto& m : messages) {
            nlohmann::json j;
            j["role"] = m.role;
            // OpenAI spec: assistant turns with tool_calls may carry null content.
            if (m.content.empty() && !m.tool_calls.empty() && m.role == "assistant") {
                j["content"] = nullptr;
            } else {
                j["content"] = m.content;
            }
            if (!m.tool_call_id.empty()) j["tool_call_id"] = m.tool_call_id;
            if (m.tool_calls.is_array() && !m.tool_calls.empty()) j["tool_calls"] = m.tool_calls;
            msgs.push_back(std::move(j));
        }
        // Import the constructed conversation. Conversation::Import expects the
        // Export() format: { "messages": [...] }.
        nlohmann::json packed; packed["messages"] = std::move(msgs);
        if (!convo.Import(packed.dump())) {
            res.success = false;
            res.error = "LLMClient: Conversation::Import failed for the supplied messages.";
            return res;
        }
    }

    std::optional<nlohmann::json> tools_opt;
    if (tools.is_array() && !tools.empty()) tools_opt = tools;
    std::optional<nlohmann::json> tool_choice_opt;
    if (!tool_choice.is_null()) tool_choice_opt = tool_choice;

    const auto t_start      = std::chrono::steady_clock::now();
    const bool is_tool_loop = tools.is_array() && !tools.empty();
    std::optional<uint16_t> max_out = max_tokens_cap_for_router(router_, is_tool_loop);
    if (max_out && router_ == "openrouter") {
        const size_t approx_in = approximate_openrouter_input_chars(messages, tools);
        *max_out = openrouter_shrink_max_tokens_for_payload(*max_out, approx_in, is_tool_loop);
        logger::info("LLMClient[chat]: openrouter approx_input_chars=" + std::to_string(approx_in)
                     + " max_tokens=" + std::to_string(static_cast<unsigned>(*max_out))
                     + (is_tool_loop ? " (tool-loop)" : ""));
    }

    try {
        liboai::Response response = oai_impl.ChatCompletion->create(
            target_model,
            convo,
            std::nullopt,    // function_call (legacy) — leave unset
            std::nullopt,    // temperature
            std::nullopt,    // top_p
            std::nullopt,    // n
            std::nullopt,    // stream
            std::nullopt,    // stop
            max_out,         // max_tokens — lower on OpenRouter for agent/tool loops (large prompts)
            std::nullopt,    // presence_penalty
            std::nullopt,    // frequency_penalty
            std::nullopt,    // logit_bias
            std::nullopt,    // user
            std::nullopt,    // response_format
            tools_opt,       // tools (NEW)
            tool_choice_opt  // tool_choice (NEW)
        );

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        logger::info("LLMClient[chat]: HTTP " + std::to_string(response.status_code)
                     + " in " + std::to_string(elapsed_ms) + " ms"
                     + " resp_bytes=" + std::to_string(response.content.size()));

        // Parse the top-level JSON the same way execute_chat does — liboai may
        // not populate raw_json for custom base URLs.
        nlohmann::json j;
        bool json_ok = false;
        if (!response.raw_json.empty() && response.raw_json.contains("choices")) {
            j = response.raw_json;
            json_ok = true;
        } else if (!response.content.empty()) {
            try {
                j = nlohmann::json::parse(response.content);
                json_ok = j.contains("choices");
            } catch (...) {}
        }

        if (!json_ok || j["choices"].empty()) {
            res.success = false;
            if (json_ok && j.contains("error")) {
                // Surface the OpenRouter / OpenAI-style {"error":{"message":...,"code":...,"type":...}}
                // shape directly so the user sees the model id / quota / auth issue verbatim.
                std::string detail;
                if (j["error"].is_object()) {
                    if (j["error"].contains("message") && j["error"]["message"].is_string())
                        detail = j["error"]["message"].get<std::string>();
                    else
                        detail = j["error"].dump();
                } else {
                    detail = j["error"].dump();
                }
                res.error = "API Error (HTTP " + std::to_string(response.status_code) + "): " + detail;
            } else {
                std::string raw_preview = response.content;
                if (raw_preview.size() > 400) raw_preview = raw_preview.substr(0, 400) + "\xE2\x80\xA6";
                res.error = "Invalid response format (HTTP " + std::to_string(response.status_code)
                             + "): no choices found. Raw: " + raw_preview;
            }
            logger::error("LLMClient[chat]: " + res.error);
            return res;
        }

        const auto& msg = j["choices"][0]["message"];

        // Detect output truncation: finish_reason="length" means max_tokens was hit.
        // Tool calls generated under truncation often have missing/empty arguments.
        const std::string finish_reason =
            (j["choices"][0].contains("finish_reason") && j["choices"][0]["finish_reason"].is_string())
            ? j["choices"][0]["finish_reason"].get<std::string>() : std::string{};
        const bool truncated = (finish_reason == "length");
        if (truncated)
            logger::warn("LLMClient[chat]: finish_reason=length — output was truncated by max_tokens");

        // Either text content or tool_calls — both are success.
        if (msg.contains("content") && msg["content"].is_string()) {
            res.text = msg["content"].get<std::string>();
        }
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            for (const auto& tc : msg["tool_calls"]) {
                ToolCall call;
                if (tc.contains("id")    && tc["id"].is_string())   call.id   = tc["id"].get<std::string>();
                if (tc.contains("function") && tc["function"].is_object()) {
                    const auto& fn = tc["function"];
                    if (fn.contains("name") && fn["name"].is_string()) call.name = fn["name"].get<std::string>();
                    if (fn.contains("arguments")) {
                        // OpenAI spec: arguments is a JSON string. Parse it.
                        if (fn["arguments"].is_string()) {
                            const std::string raw = fn["arguments"].get<std::string>();
                            auto parsed = nlohmann::json::parse(raw, nullptr, /*allow_exceptions*/ false);
                            call.arguments = parsed.is_discarded() ? nlohmann::json::object() : std::move(parsed);
                        } else if (fn["arguments"].is_object()) {
                            call.arguments = fn["arguments"];
                        } else {
                            call.arguments = nlohmann::json::object();
                        }
                    }
                }
                // Drop tool calls with empty arguments when output was truncated —
                // the model did not finish generating them.
                if (truncated && call.arguments.is_object() && call.arguments.empty()) {
                    logger::warn("LLMClient[chat]: dropping truncated tool_call name="
                                 + call.name + " (no arguments due to max_tokens)");
                    continue;
                }
                res.tool_calls.push_back(std::move(call));
            }
        }
        res.success = true;
        logger::info("LLMClient[chat]: ok \xE2\x80\x94 text_chars=" + std::to_string(res.text.size())
                     + " tool_calls=" + std::to_string(res.tool_calls.size()));

        // Stash everything except `choices` for diagnostics (cost / model id / OpenRouter extras).
        try {
            nlohmann::json meta = nlohmann::json::object();
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key() == "choices") continue;
                meta[it.key()] = it.value();
            }
            if (!meta.empty()) res.provider_meta_json = meta.dump();
        } catch (...) {}
    } catch (const std::exception& e) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        const std::string what = e.what();
        logger::error(std::string("LLMClient[chat]: exception after ") + std::to_string(elapsed_ms)
                      + " ms: " + what);
        // Schema-related errors: dump the full tools JSON so the bad definition is identifiable.
        if (tools.is_array() && !tools.empty()
                && (what.find("schema") != std::string::npos
                    || what.find("function") != std::string::npos
                    || what.find("properties") != std::string::npos)) {
            logger::error("LLMClient[chat]: tools JSON that caused the error:\n" + tools.dump(2));
        }
        res.success = false;
        res.error = what;
    } catch (...) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        logger::error("LLMClient[chat]: unknown exception after " + std::to_string(elapsed_ms) + " ms");
        res.success = false;
        res.error = "Unknown error inside LLMClient::execute_chat_messages.";
    }

    return res;
}

// ── execute_responses — POST /responses (OpenAI Responses API) ────────────

namespace {

/// Convert tools from Chat Completions format to Responses API format.
/// Chat:      {"type":"function","function":{"name":"...","description":"...","parameters":{...}}}
/// Responses: {"type":"function","name":"...","description":"...","parameters":{...}}
nlohmann::json convert_tools_to_responses_format(const nlohmann::json& tools)
{
    if (!tools.is_array()) return nlohmann::json::array();
    nlohmann::json out = nlohmann::json::array();
    for (const auto& t : tools) {
        if (!t.is_object()) continue;
        if (t.value("type", std::string()) == "function"
                && t.contains("function") && t["function"].is_object()) {
            nlohmann::json flat = t["function"];
            flat["type"] = "function";
            out.push_back(std::move(flat));
        } else {
            out.push_back(t);
        }
    }
    return out;
}

void strip_empty_enum_values_in_schema(nlohmann::json& node, int& removed_count)
{
    if (node.is_object()) {
        if (node.contains("enum") && node["enum"].is_array()) {
            nlohmann::json cleaned = nlohmann::json::array();
            for (const auto& v : node["enum"]) {
                if (v.is_string() && v.get<std::string>().empty()) {
                    ++removed_count;
                    continue;
                }
                cleaned.push_back(v);
            }
            node["enum"] = std::move(cleaned);
        }
        for (auto it = node.begin(); it != node.end(); ++it) {
            strip_empty_enum_values_in_schema(it.value(), removed_count);
        }
    } else if (node.is_array()) {
        for (auto& v : node) {
            strip_empty_enum_values_in_schema(v, removed_count);
        }
    }
}

} // namespace

std::string preview_json_or_text(const std::string& content, std::size_t max_len = 2000) {
    std::string out = content;
    if (out.size() > max_len)
        out = out.substr(0, max_len) + "...";
    return out;
}

std::string extract_provider_error_detail(const std::string& body) {
    if (body.empty()) return {};
    try {
        const auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object() || !j.contains("error") || !j["error"].is_object())
            return {};
        const auto& err = j["error"];
        std::string provider_name;
        std::string provider_status;
        std::string provider_message;
        if (err.contains("metadata") && err["metadata"].is_object()) {
            const auto& m = err["metadata"];
            if (m.contains("provider_name") && m["provider_name"].is_string())
                provider_name = m["provider_name"].get<std::string>();
            if (m.contains("raw") && m["raw"].is_string()) {
                const std::string raw = m["raw"].get<std::string>();
                const auto jr = nlohmann::json::parse(raw, nullptr, false);
                if (jr.is_object() && jr.contains("error") && jr["error"].is_object()) {
                    const auto& pe = jr["error"];
                    if (pe.contains("status") && pe["status"].is_string())
                        provider_status = pe["status"].get<std::string>();
                    if (pe.contains("message") && pe["message"].is_string())
                        provider_message = pe["message"].get<std::string>();
                }
            }
        }
        std::string out;
        if (!provider_name.empty())
            out += "provider=" + provider_name;
        if (!provider_status.empty()) {
            if (!out.empty()) out += " ";
            out += "status=" + provider_status;
        }
        if (!provider_message.empty()) {
            if (!out.empty()) out += " ";
            out += "message=" + provider_message;
        }
        return out;
    } catch (...) {
        return {};
    }
}

LLMResponse LLMClient::execute_responses(
    const std::vector<ChatMessage>& messages,
    const nlohmann::json& tools,
    const nlohmann::json& tool_choice,
    const std::string& previous_response_id)
{
    LLMResponse res;

    if (api_key_.empty()) {
        res.success = false;
        res.error = "API Key is empty.";
        return res;
    }
    if (messages.empty()) {
        res.success = false;
        res.error = "messages is empty.";
        return res;
    }

    const std::string eff_base = base_url_.empty() ? "https://api.openai.com/v1" : base_url_;
    liboai::OpenAI oai_impl(eff_base);
    if (!oai_impl.auth.SetKey(api_key_)) {
        res.success = false;
        res.error = "Failed to set API Key in liboai.";
        return res;
    }
    if (llm_timeout_ms_ > 0)
        oai_impl.auth.SetMaxTimeout(llm_timeout_ms_);

    const std::string target_model = model_.empty() ? "gpt-4o" : model_;
    const int n_tools = tools.is_array() ? static_cast<int>(tools.size()) : 0;
    logger::info("LLMClient[responses]: POST " + eff_base + "/responses"
                 + " model=" + target_model
                 + (previous_response_id.empty()
                    ? " msgs=" + std::to_string(messages.size()) + " [stateless]"
                    : " [stateful prev=" + previous_response_id.substr(0, 16) + "…]")
                 + " tools=" + std::to_string(n_tools));
    if (n_tools > 0) {
        std::string tool_names;
        for (const auto& t : tools) {
            if (!tool_names.empty()) tool_names += ", ";
            if (t.contains("function") && t["function"].is_object())
                tool_names += t["function"].value("name", std::string("?"));
            else
                tool_names += t.value("name", std::string("?"));
        }
        logger::debug("LLMClient[responses]: tools=[" + tool_names + "]");
    }

    // ── Build input[] and request body ────────────────────────────────────
    nlohmann::json request;
    request["model"] = target_model;
    // OpenRouter currently validates `store` as false-only on /responses.
    // Keep true for native OpenAI chaining behavior, false for OpenRouter.
    request["store"] = (router_ == "openrouter") ? false : true;
    // OpenRouter may reject `include` on /responses for some routed models;
    // encrypted reasoning content is optional for our loop, so only request it
    // on native OpenAI endpoints for now.
    if (router_ == "openai")
        request["include"] = nlohmann::json::array({"reasoning.encrypted_content"});

    nlohmann::json responses_tools;
    if (n_tools > 0) {
        responses_tools = convert_tools_to_responses_format(tools);
        int removed_empty_enums = 0;
        strip_empty_enum_values_in_schema(responses_tools, removed_empty_enums);
        if (removed_empty_enums > 0) {
            logger::warn("LLMClient[responses]: sanitized tools schema, removed "
                         + std::to_string(removed_empty_enums)
                         + " empty enum entries for provider compatibility");
        }
    }

    bool use_stateful = !previous_response_id.empty();
    if (use_stateful && router_ == "openrouter") {
        logger::warn("LLMClient[responses]: disabling previous_response_id chaining for OpenRouter "
                     "to avoid routed-provider incompatibilities; using stateless replay");
        use_stateful = false;
    }
    if (use_stateful) {
        // ── Stateful (chained) mode ────────────────────────────────────────
        // Only send the tool results from the most recent round; the full
        // conversation history, system prompt, and tool schemas are cached
        // server-side under previous_response_id. This avoids re-sending
        // the entire context (tool schemas + file contents + reasoning blobs)
        // on every round, which would otherwise blow past 40k tokens on
        // multi-step agent tasks.
        nlohmann::json input = nlohmann::json::array();
        // Walk messages from the end, collecting "tool" role messages until
        // we hit the preceding "assistant" turn (which is already cached).
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == "assistant") break;
            if (it->role == "tool") {
                input.insert(input.begin(), nlohmann::json{
                    {"type",    "function_call_output"},
                    {"call_id", it->tool_call_id},
                    {"output",  it->content}
                });
            }
        }
        if (input.empty()) {
            logger::warn("LLMClient[responses]: stateful round had no tool outputs; "
                         "falling back to stateless request to avoid empty-input provider error");
            use_stateful = false;
        } else {
            request["previous_response_id"] = previous_response_id;
            request["input"] = std::move(input);
            // Tools are re-sent each round so the model can use them from the new output.
            if (n_tools > 0) {
                request["tools"] = responses_tools;
                if (!tool_choice.is_null() && !tool_choice.is_discarded())
                    request["tool_choice"] = tool_choice;
                else
                    request["tool_choice"] = "auto";
            }
            logger::debug("LLMClient[responses]: stateful chain — prev=" + previous_response_id);
        }
    }
    if (!use_stateful) {
        // ── Stateless (first round) mode ───────────────────────────────────
        // Translate the full message history into input[] + instructions.
        std::string instructions;
        nlohmann::json input = nlohmann::json::array();

        for (const auto& m : messages) {
            if (m.role == "system") {
                instructions = m.content;
                continue;
            }
            if (m.role == "user") {
                input.push_back(nlohmann::json{
                    {"type",    "message"},
                    {"role",    "user"},
                    {"content", nlohmann::json::array({
                        nlohmann::json{{"type","input_text"},{"text", m.content}}
                    })}
                });
            } else if (m.role == "assistant") {
                if (m.tool_calls.is_array() && !m.tool_calls.empty()) {
                    if (m.responses_output_items.is_array() && !m.responses_output_items.empty()) {
                        // Replay verbatim: reasoning items carry summary + encrypted_content;
                        // function_call items carry all original fields. Different gpt-5 versions
                        // require different fields (gpt-5.2 needs encrypted_content, gpt-5.5
                        // needs summary), so we store and replay the complete item.
                        for (const auto& raw : m.responses_output_items)
                            input.push_back(raw);
                    } else {
                        // Fallback: reconstruct function_call items from tool_calls[] array.
                        // Used for /chat/completions history or pre-stateful conversations.
                        for (const auto& tc : m.tool_calls) {
                            const std::string call_id = tc.value("id", std::string());
                            std::string name, arguments;
                            if (tc.contains("function") && tc["function"].is_object()) {
                                name = tc["function"].value("name", std::string());
                                if (tc["function"].contains("arguments")) {
                                    const auto& args = tc["function"]["arguments"];
                                    if (args.is_string())
                                        arguments = args.get<std::string>();
                                    else
                                        arguments = args.dump();
                                } else {
                                    arguments = "{}";
                                }
                            }
                            const std::string item_id = tc.value("item_id", std::string());
                            nlohmann::json fc_item{
                                {"type",      "function_call"},
                                {"call_id",   call_id},
                                {"name",      std::move(name)},
                                {"arguments", std::move(arguments)}
                            };
                            if (!item_id.empty())
                                fc_item["id"] = item_id;
                            input.push_back(std::move(fc_item));
                        }
                    }
                } else {
                    input.push_back(nlohmann::json{
                        {"type",    "message"},
                        {"role",    "assistant"},
                        {"content", nlohmann::json::array({
                            nlohmann::json{{"type","output_text"},{"text", m.content}}
                        })}
                    });
                }
            } else if (m.role == "tool") {
                input.push_back(nlohmann::json{
                    {"type",    "function_call_output"},
                    {"call_id", m.tool_call_id},
                    {"output",  m.content}
                });
            }
        }
        request["input"] = std::move(input);
        if (!instructions.empty())
            request["instructions"] = instructions;
        if (n_tools > 0) {
            request["tools"] = responses_tools;
            if (!tool_choice.is_null() && !tool_choice.is_discarded())
                request["tool_choice"] = tool_choice;
            else
                request["tool_choice"] = "auto";
        }
    }

    if (router_ == "openrouter") {
        const bool   is_tool_loop = (n_tools > 0);
        const size_t approx_in    = approximate_openrouter_input_chars(messages, tools);
        uint16_t     max_out      = max_tokens_cap_for_router(router_, is_tool_loop)
                                      .value_or(static_cast<uint16_t>(1024));
        max_out = openrouter_shrink_max_tokens_for_payload(max_out, approx_in, is_tool_loop);
        request["max_output_tokens"] = static_cast<unsigned>(max_out);
        logger::info("LLMClient[responses]: openrouter approx_input_chars=" + std::to_string(approx_in)
                     + " max_output_tokens=" + std::to_string(static_cast<unsigned>(max_out))
                     + (is_tool_loop ? " (tool-loop)" : ""));
    }

    // ── HTTP call ──────────────────────────────────────────────────────────
    const auto t_start = std::chrono::steady_clock::now();
    try {
        liboai::Response response = oai_impl.Responses->create(request);

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        logger::info("LLMClient[responses]: HTTP " + std::to_string(response.status_code)
                     + " in " + std::to_string(elapsed_ms) + " ms"
                     + " resp_bytes=" + std::to_string(response.content.size()));

        // ── Parse response ─────────────────────────────────────────────────
        nlohmann::json j;
        bool json_ok = false;
        if (!response.raw_json.empty() && response.raw_json.is_object()) {
            j = response.raw_json;
            json_ok = true;
        } else if (!response.content.empty()) {
            try {
                j = nlohmann::json::parse(response.content);
                json_ok = j.is_object();
            } catch (...) {}
        }

        if (!json_ok) {
            res.success = false;
            std::string preview = response.content;
            if (preview.size() > 400) preview = preview.substr(0, 400) + "\xe2\x80\xa6";
            res.error = "Responses API: invalid JSON (HTTP "
                        + std::to_string(response.status_code) + "): " + preview;
            logger::error("LLMClient[responses]: " + res.error);
            return res;
        }

        // API-level error envelope
        if (j.contains("error") && j["error"].is_object()) {
            const auto& e = j["error"];
            const std::string detail = e.contains("message") && e["message"].is_string()
                ? e["message"].get<std::string>() : e.dump();
            res.success = false;
            res.error = "Responses API error (HTTP "
                        + std::to_string(response.status_code) + "): " + detail;
            logger::error("LLMClient[responses]: " + res.error);
            return res;
        }

        // status == "failed" | "cancelled"
        const std::string status = j.value("status", std::string());
        if (status == "failed" || status == "cancelled") {
            std::string detail;
            if (j.contains("error") && j["error"].is_object())
                detail = j["error"].value("message", j["error"].dump());
            res.success = false;
            res.error = "Responses API: status=" + status + (detail.empty() ? "" : ": " + detail);
            logger::error("LLMClient[responses]: " + res.error);
            return res;
        }

        // ── Extract output items ───────────────────────────────────────────
        std::string text_accum;
        std::string reasoning_accum;
        if (j.contains("output") && j["output"].is_array()) {
            for (const auto& item : j["output"]) {
                const std::string itype = item.value("type", std::string());
                if (itype == "reasoning") {
                    // Store the complete item for verbatim replay. Different model versions
                    // have different requirements: gpt-5.2 needs encrypted_content, gpt-5.5
                    // needs summary. Storing everything keeps us compatible with both.
                    res.responses_output_items.push_back(item);
                    // Also extract human-readable summary text for Event::Thinking display.
                    if (item.contains("summary") && item["summary"].is_array()) {
                        for (const auto& s : item["summary"]) {
                            if (s.value("type", std::string()) == "summary_text"
                                    && s.contains("text") && s["text"].is_string()) {
                                if (!reasoning_accum.empty()) reasoning_accum += '\n';
                                reasoning_accum += s["text"].get<std::string>();
                            }
                        }
                    }
                } else if (itype == "message") {
                    if (item.contains("content") && item["content"].is_array()) {
                        for (const auto& c : item["content"]) {
                            const std::string ctype = c.value("type", std::string());
                            if ((ctype == "output_text" || ctype == "text")
                                    && c.contains("text") && c["text"].is_string())
                                text_accum += c["text"].get<std::string>();
                        }
                    }
                } else if (itype == "function_call") {
                    // Store verbatim for replay (must follow its reasoning item in input[]).
                    res.responses_output_items.push_back(item);
                    ToolCall call;
                    // item_id: the "fc_…" item identifier — must be replayed as
                    // input[].function_call.id in the next round.
                    call.item_id = item.value("id", std::string());
                    // call_id: used in function_call_output.call_id for result correlation.
                    call.id = item.contains("call_id") && item["call_id"].is_string()
                              ? item["call_id"].get<std::string>()
                              : call.item_id;
                    call.name = item.value("name", std::string());
                    const std::string args_str = item.value("arguments", std::string("{}"));
                    auto parsed = nlohmann::json::parse(args_str, nullptr, /*exceptions*/ false);
                    call.arguments = parsed.is_discarded() ? nlohmann::json::object() : std::move(parsed);
                    res.tool_calls.push_back(std::move(call));
                } else {
                    // Forward-compatible: preserve any other non-message output items.
                    res.responses_output_items.push_back(item);
                }
            }
        }

        res.text           = std::move(text_accum);
        res.reasoning_text = std::move(reasoning_accum);
        // Capture the response ID for stateful chaining in the next round.
        if (j.contains("id") && j["id"].is_string())
            res.response_id = j["id"].get<std::string>();
        res.success = true;
        logger::info("LLMClient[responses]: ok \xe2\x80\x94 text_chars=" + std::to_string(res.text.size())
                     + " tool_calls=" + std::to_string(res.tool_calls.size())
                     + (res.reasoning_text.empty() ? "" :
                        " reasoning_chars=" + std::to_string(res.reasoning_text.size())));

        // Stash top-level meta for usage aggregation (omit "output")
        try {
            nlohmann::json meta = nlohmann::json::object();
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key() == "output") continue;
                meta[it.key()] = it.value();
            }
            if (!meta.empty()) res.provider_meta_json = meta.dump();
        } catch (...) {}

    } catch (const liboai::exception::OpenAIException& e) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        const std::string what = e.what();
        std::string detail_for_error;
        logger::error("LLMClient[responses]: exception after " + std::to_string(elapsed_ms)
                      + " ms: " + what);
        logger::error("LLMClient[responses]: request payload preview:\n"
                      + preview_json_or_text(request.dump(2)));
        logger::error("LLMClient[responses]: request base_url=" + eff_base
                      + " model=" + target_model
                      + " tool_count=" + std::to_string(n_tools)
                      + (previous_response_id.empty()
                         ? " state=stateless"
                         : " state=stateful"));

        const long http_status = e.HttpStatusCode();
        const std::string& body = e.ResponseBody();
        if (http_status > 0 || !body.empty()) {
            logger::error("LLMClient[responses]: liboai error HTTP " + std::to_string(http_status)
                          + " body:\n" + preview_json_or_text(body, 6000));
            detail_for_error = " [responses_http="
                + std::to_string(http_status)
                + " body=" + preview_json_or_text(body, 1200) + "]";
            const std::string extracted = extract_provider_error_detail(body);
            if (!extracted.empty()) {
                logger::error("LLMClient[responses]: provider detail: " + extracted);
                detail_for_error += " [responses_provider_detail=" + extracted + "]";
            }
        }

        if (tools.is_array() && !tools.empty()
                && (what.find("schema") != std::string::npos
                    || what.find("function") != std::string::npos
                    || what.find("properties") != std::string::npos)) {
            logger::error("LLMClient[responses]: tools JSON that caused the error:\n" + tools.dump(2));
        }
        res.success = false;
        res.error = what + detail_for_error;
    } catch (const std::exception& e) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        const std::string what = e.what();
        logger::error("LLMClient[responses]: exception after " + std::to_string(elapsed_ms)
                      + " ms: " + what);
        logger::error("LLMClient[responses]: request payload preview:\n"
                      + preview_json_or_text(request.dump(2)));
        logger::error("LLMClient[responses]: request base_url=" + eff_base
                      + " model=" + target_model
                      + " tool_count=" + std::to_string(n_tools)
                      + (previous_response_id.empty()
                         ? " state=stateless"
                         : " state=stateful"));
        if (tools.is_array() && !tools.empty()
                && (what.find("schema") != std::string::npos
                    || what.find("function") != std::string::npos
                    || what.find("properties") != std::string::npos)) {
            logger::error("LLMClient[responses]: tools JSON that caused the error:\n" + tools.dump(2));
        }
        res.success = false;
        res.error = what;
    } catch (...) {
        res.success = false;
        res.error = "Unknown error inside LLMClient::execute_responses.";
    }

    return res;
}

LLMResponse LLMClient::execute_responses_stream(
    const std::vector<ChatMessage>& messages,
    const nlohmann::json& tools,
    const nlohmann::json& tool_choice,
    const std::string& previous_response_id,
    std::function<bool(const std::string&)> on_text_delta)
{
    LLMResponse res;

    if (api_key_.empty()) {
        res.success = false;
        res.error = "API Key is empty.";
        return res;
    }
    if (messages.empty()) {
        res.success = false;
        res.error = "messages is empty.";
        return res;
    }

    const std::string eff_base = base_url_.empty() ? "https://api.openai.com/v1" : base_url_;
    liboai::OpenAI oai_impl(eff_base);
    if (!oai_impl.auth.SetKey(api_key_)) {
        res.success = false;
        res.error = "Failed to set API Key in liboai.";
        return res;
    }
    if (llm_timeout_ms_ > 0)
        oai_impl.auth.SetMaxTimeout(llm_timeout_ms_);

    const std::string target_model = model_.empty() ? "gpt-4o" : model_;
    const int n_tools = tools.is_array() ? static_cast<int>(tools.size()) : 0;
    logger::info("LLMClient[responses-stream]: POST " + eff_base + "/responses"
                 + " model=" + target_model
                 + (previous_response_id.empty()
                    ? " msgs=" + std::to_string(messages.size()) + " [stateless]"
                    : " [stateful prev=" + previous_response_id.substr(0, 16) + "…]")
                 + " tools=" + std::to_string(n_tools));

    nlohmann::json request;
    request["model"] = target_model;
    request["store"] = (router_ == "openrouter") ? false : true;
    if (router_ == "openai")
        request["include"] = nlohmann::json::array({"reasoning.encrypted_content"});

    nlohmann::json responses_tools;
    if (n_tools > 0) {
        responses_tools = convert_tools_to_responses_format(tools);
        int removed_empty_enums = 0;
        strip_empty_enum_values_in_schema(responses_tools, removed_empty_enums);
        if (removed_empty_enums > 0) {
            logger::warn("LLMClient[responses-stream]: sanitized tools schema, removed "
                         + std::to_string(removed_empty_enums)
                         + " empty enum entries for provider compatibility");
        }
    }

    bool use_stateful = !previous_response_id.empty();
    if (use_stateful && router_ == "openrouter") {
        logger::warn("LLMClient[responses-stream]: disabling previous_response_id chaining for OpenRouter "
                     "to avoid routed-provider incompatibilities; using stateless replay");
        use_stateful = false;
    }
    if (use_stateful) {
        nlohmann::json input = nlohmann::json::array();
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == "assistant") break;
            if (it->role == "tool") {
                input.insert(input.begin(), nlohmann::json{
                    {"type",    "function_call_output"},
                    {"call_id", it->tool_call_id},
                    {"output",  it->content}
                });
            }
        }
        if (input.empty()) {
            logger::warn("LLMClient[responses-stream]: stateful round had no tool outputs; "
                         "falling back to stateless request to avoid empty-input provider error");
            use_stateful = false;
        } else {
            request["previous_response_id"] = previous_response_id;
            request["input"] = std::move(input);
            if (n_tools > 0) {
                request["tools"] = responses_tools;
                if (!tool_choice.is_null() && !tool_choice.is_discarded())
                    request["tool_choice"] = tool_choice;
                else
                    request["tool_choice"] = "auto";
            }
        }
    }
    if (!use_stateful) {
        std::string instructions;
        nlohmann::json input = nlohmann::json::array();
        for (const auto& m : messages) {
            if (m.role == "system") {
                instructions = m.content;
                continue;
            }
            if (m.role == "user") {
                input.push_back(nlohmann::json{
                    {"type",    "message"},
                    {"role",    "user"},
                    {"content", nlohmann::json::array({
                        nlohmann::json{{"type","input_text"},{"text", m.content}}
                    })}
                });
            } else if (m.role == "assistant") {
                if (m.tool_calls.is_array() && !m.tool_calls.empty()) {
                    if (m.responses_output_items.is_array() && !m.responses_output_items.empty()) {
                        for (const auto& raw : m.responses_output_items)
                            input.push_back(raw);
                    } else {
                        for (const auto& tc : m.tool_calls) {
                            const std::string call_id = tc.value("id", std::string());
                            std::string name, arguments;
                            if (tc.contains("function") && tc["function"].is_object()) {
                                name = tc["function"].value("name", std::string());
                                if (tc["function"].contains("arguments")) {
                                    const auto& args = tc["function"]["arguments"];
                                    if (args.is_string()) arguments = args.get<std::string>();
                                    else                  arguments = args.dump();
                                } else {
                                    arguments = "{}";
                                }
                            }
                            const std::string item_id = tc.value("item_id", std::string());
                            nlohmann::json fc_item{
                                {"type",      "function_call"},
                                {"call_id",   call_id},
                                {"name",      std::move(name)},
                                {"arguments", std::move(arguments)}
                            };
                            if (!item_id.empty())
                                fc_item["id"] = item_id;
                            input.push_back(std::move(fc_item));
                        }
                    }
                } else {
                    input.push_back(nlohmann::json{
                        {"type",    "message"},
                        {"role",    "assistant"},
                        {"content", nlohmann::json::array({
                            nlohmann::json{{"type","output_text"},{"text", m.content}}
                        })}
                    });
                }
            } else if (m.role == "tool") {
                input.push_back(nlohmann::json{
                    {"type",    "function_call_output"},
                    {"call_id", m.tool_call_id},
                    {"output",  m.content}
                });
            }
        }
        request["input"] = std::move(input);
        if (!instructions.empty())
            request["instructions"] = instructions;
        if (n_tools > 0) {
            request["tools"] = responses_tools;
            if (!tool_choice.is_null() && !tool_choice.is_discarded())
                request["tool_choice"] = tool_choice;
            else
                request["tool_choice"] = "auto";
        }
    }

    std::string text_accum;
    std::string reasoning_accum;
    std::string sse_buf;
    nlohmann::json completed_response;
    std::string stream_error;

    auto process_output_item = [&](const nlohmann::json& item) {
        const std::string itype = item.value("type", std::string());
        if (itype == "reasoning") {
            res.responses_output_items.push_back(item);
            if (item.contains("summary") && item["summary"].is_array()) {
                for (const auto& s : item["summary"]) {
                    if (s.value("type", std::string()) == "summary_text"
                            && s.contains("text") && s["text"].is_string()) {
                        if (!reasoning_accum.empty()) reasoning_accum += '\n';
                        reasoning_accum += s["text"].get<std::string>();
                    }
                }
            }
        } else if (itype == "message") {
            if (item.contains("content") && item["content"].is_array()) {
                for (const auto& c : item["content"]) {
                    const std::string ctype = c.value("type", std::string());
                    if ((ctype == "output_text" || ctype == "text")
                            && c.contains("text") && c["text"].is_string()) {
                        text_accum += c["text"].get<std::string>();
                    }
                }
            }
        } else if (itype == "function_call") {
            res.responses_output_items.push_back(item);
            ToolCall call;
            call.item_id = item.value("id", std::string());
            call.id = item.contains("call_id") && item["call_id"].is_string()
                      ? item["call_id"].get<std::string>()
                      : call.item_id;
            call.name = item.value("name", std::string());
            const std::string args_str = item.value("arguments", std::string("{}"));
            auto parsed = nlohmann::json::parse(args_str, nullptr, false);
            call.arguments = parsed.is_discarded() ? nlohmann::json::object() : std::move(parsed);
            res.tool_calls.push_back(std::move(call));
        } else {
            res.responses_output_items.push_back(item);
        }
    };

    const auto t_start = std::chrono::steady_clock::now();
    try {
        liboai::Response response = oai_impl.Responses->create(
            request,
            std::optional<liboai::Responses::StreamCallback>(
                [&](std::string chunk, intptr_t) -> bool {
                    // Normalize CRLF framing to LF so SSE block splitting works
                    // across providers/proxies (OpenRouter, LiteLLM, etc).
                    for (char ch : chunk) {
                        if (ch != '\r') sse_buf.push_back(ch);
                    }
                    for (;;) {
                        const size_t sep = sse_buf.find("\n\n");
                        if (sep == std::string::npos) break;
                        std::string evt = sse_buf.substr(0, sep);
                        sse_buf.erase(0, sep + 2);
                        std::string data;
                        std::string event_name;
                        size_t line_start = 0;
                        while (line_start <= evt.size()) {
                            size_t line_end = evt.find('\n', line_start);
                            if (line_end == std::string::npos) line_end = evt.size();
                            std::string line = evt.substr(line_start, line_end - line_start);
                            if (line.rfind("data:", 0) == 0) {
                                std::string payload = line.size() > 5 ? line.substr(5) : std::string();
                                if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);
                                if (!data.empty()) data.push_back('\n');
                                data += payload;
                            } else if (line.rfind("event:", 0) == 0) {
                                event_name = line.size() > 6 ? line.substr(6) : std::string();
                                if (!event_name.empty() && event_name[0] == ' ')
                                    event_name.erase(0, 1);
                            }
                            if (line_end == evt.size()) break;
                            line_start = line_end + 1;
                        }
                        if (data.empty() || data == "[DONE]") continue;
                        const auto j = nlohmann::json::parse(data, nullptr, false);
                        if (j.is_discarded() || !j.is_object()) continue;
                        std::string typ = j.value("type", std::string());
                        if (typ.empty()) typ = event_name;
                        if (typ == "error") {
                            stream_error = "Responses stream error: " + j.dump();
                            return false;
                        }
                        if (typ == "response.output_text.delta"
                                && j.contains("delta") && j["delta"].is_string()) {
                            const std::string d = j["delta"].get<std::string>();
                            text_accum += d;
                            if (on_text_delta && !d.empty()) {
                                if (!on_text_delta(d)) {
                                    stream_error = "Responses stream cancelled by callback";
                                    return false;
                                }
                            }
                        } else if (typ == "response.output_item.done"
                                   && j.contains("item") && j["item"].is_object()) {
                            process_output_item(j["item"]);
                        } else if (typ == "response.completed" || typ == "response.done") {
                            if (j.contains("response") && j["response"].is_object())
                                completed_response = j["response"];
                        } else if (typ == "response.failed") {
                            stream_error = "Responses stream failed: " + j.dump();
                            return false;
                        }
                    }
                    return true;
                }));

        if (!stream_error.empty()) {
            res.success = false;
            res.error = stream_error;
            return res;
        }

        nlohmann::json j = completed_response;
        if ((j.is_null() || j.empty()) && !response.content.empty()) {
            const auto parsed = nlohmann::json::parse(response.content, nullptr, false);
            if (parsed.is_object()) j = parsed;
        }

        if (j.is_object()) {
            if (j.contains("id") && j["id"].is_string())
                res.response_id = j["id"].get<std::string>();
            if (j.contains("output") && j["output"].is_array()) {
                for (const auto& item : j["output"])
                    process_output_item(item);
            }
            try {
                nlohmann::json meta = nlohmann::json::object();
                for (auto it = j.begin(); it != j.end(); ++it) {
                    if (it.key() == "output") continue;
                    meta[it.key()] = it.value();
                }
                if (!meta.empty()) res.provider_meta_json = meta.dump();
            } catch (...) {}
        } else {
            try {
                const auto m = nlohmann::json::parse(response.content, nullptr, false);
                if (m.is_object()) res.provider_meta_json = m.dump();
            } catch (...) {}
        }

        res.text = std::move(text_accum);
        res.reasoning_text = std::move(reasoning_accum);
        res.success = true;

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t_start).count();
        logger::info("LLMClient[responses-stream]: ok in " + std::to_string(elapsed_ms)
                     + " ms text_chars=" + std::to_string(res.text.size())
                     + " tool_calls=" + std::to_string(res.tool_calls.size()));
    } catch (const liboai::exception::OpenAIException& e) {
        const std::string what = e.what();
        std::string detail_for_error;
        const long http_status = e.HttpStatusCode();
        const std::string& body = e.ResponseBody();
        if (http_status > 0 || !body.empty()) {
            detail_for_error = " [responses_http="
                + std::to_string(http_status)
                + " body=" + preview_json_or_text(body, 1200) + "]";
            const std::string extracted = extract_provider_error_detail(body);
            if (!extracted.empty())
                detail_for_error += " [responses_provider_detail=" + extracted + "]";
        }
        res.success = false;
        res.error = what + detail_for_error;
    } catch (const std::exception& e) {
        res.success = false;
        res.error = e.what();
    } catch (...) {
        res.success = false;
        res.error = "Unknown error inside LLMClient::execute_responses_stream.";
    }

    return res;
}

LLMResponse LLMClient::execute_realtime_text(const std::vector<ChatMessage>& messages)
{
    LLMResponse res;
    if (api_key_.empty()) {
        res.success = false;
        res.error = "API Key is empty.";
        return res;
    }
    const RealtimePromptParts parts = derive_realtime_prompt_parts(messages);
    if (parts.user.empty()) {
        res.success = false;
        res.error = "messages is empty (no user content to send).";
        return res;
    }

    const std::string target_model = model_.empty() ? "gpt-4o-realtime-preview" : model_;
    const std::string ws_url = realtime_base_url_for(base_url_) + "?model=" + target_model;
    logger::info("LLMClient[realtime]: WS connect " + ws_url);

    CURL* c = curl_easy_init();
    if (!c) {
        res.success = false;
        res.error = "curl_easy_init failed";
        return res;
    }

    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + api_key_).c_str());
    curl_easy_setopt(c, CURLOPT_URL, ws_url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    if (llm_timeout_ms_ > 0)
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(llm_timeout_ms_));

    const auto t_start = std::chrono::steady_clock::now();
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        res.success = false;
        res.error = std::string("Realtime connect failed: ") + curl_easy_strerror(rc);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        return res;
    }

    auto send_event = [&](const nlohmann::json& j) -> bool {
        const std::string wire = j.dump();
        size_t sent = 0;
        const CURLcode s = curl_ws_send(c, wire.data(), wire.size(), &sent, 0, CURLWS_TEXT);
        return s == CURLE_OK;
    };

    nlohmann::json add_user{
        {"type", "conversation.item.create"},
        {"item", {
            {"type", "message"},
            {"role", "user"},
            {"content", nlohmann::json::array({
                nlohmann::json{{"type","input_text"}, {"text", parts.user}}
            })}
        }}
    };
    if (!send_event(add_user)) {
        res.success = false;
        res.error = "Realtime send failed: conversation.item.create";
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        return res;
    }

    nlohmann::json create{
        {"type", "response.create"},
        {"response", nlohmann::json{
            {"output_modalities", nlohmann::json::array({"text"})}
        }}
    };
    if (!parts.system.empty())
        create["response"]["instructions"] = parts.system;
    if (!send_event(create)) {
        res.success = false;
        res.error = "Realtime send failed: response.create";
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        return res;
    }

    char tmp[16384];
    std::string frame_buf;
    std::string out_text;
    bool saw_text_delta = false;
    bool done = false;
    while (!done) {
        size_t nrecvd = 0;
        const curl_ws_frame* meta = nullptr;
        rc = curl_ws_recv(c, tmp, sizeof(tmp), &nrecvd, &meta);
        if (rc == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (rc != CURLE_OK) {
            res.success = false;
            res.error = std::string("Realtime recv failed: ") + curl_easy_strerror(rc);
            break;
        }
        if (meta && (meta->flags & CURLWS_CLOSE)) {
            done = true;
            break;
        }

        if (nrecvd > 0) frame_buf.append(tmp, nrecvd);
        if (meta && meta->bytesleft == 0 && !frame_buf.empty()) {
            try {
                const auto j = nlohmann::json::parse(frame_buf, nullptr, false);
                if (j.is_object()) {
                    const std::string typ = j.value("type", std::string());
                    if (typ == "error") {
                        res.success = false;
                        res.error = "Realtime API error: " + j.dump();
                        done = true;
                    } else if (typ == "response.text.delta"
                               && j.contains("delta") && j["delta"].is_string()) {
                        saw_text_delta = true;
                        out_text += j["delta"].get<std::string>();
                    } else if (typ == "response.text.done"
                               && j.contains("text") && j["text"].is_string()) {
                        if (!saw_text_delta)
                            out_text += j["text"].get<std::string>();
                    } else if (typ == "response.done") {
                        if (!saw_text_delta && out_text.empty()
                                && j.contains("response") && j["response"].is_object()) {
                            const auto& r = j["response"];
                            if (r.contains("output") && r["output"].is_array()) {
                                for (const auto& item : r["output"]) {
                                    if (!item.is_object()) continue;
                                    if (item.value("type", std::string()) != "message") continue;
                                    if (!item.contains("content") || !item["content"].is_array()) continue;
                                    for (const auto& c : item["content"]) {
                                        if ((c.value("type", std::string()) == "output_text"
                                             || c.value("type", std::string()) == "text")
                                                && c.contains("text") && c["text"].is_string()) {
                                            out_text += c["text"].get<std::string>();
                                        }
                                    }
                                }
                            }
                        }
                        done = true;
                    } else {
                        if (typ == "response.output_text.delta"
                                && j.contains("delta") && j["delta"].is_string()) {
                            saw_text_delta = true;
                            out_text += j["delta"].get<std::string>();
                        } else if (typ == "response.output_text.done"
                                && j.contains("text") && j["text"].is_string()) {
                            if (!saw_text_delta)
                                out_text += j["text"].get<std::string>();
                        } else if (typ == "response.output_item.done"
                                   && j.contains("item") && j["item"].is_object()) {
                            // Ignore message-level final text when deltas were already received.
                            // This prevents duplicated assistant text in some realtime streams.
                            if (!saw_text_delta) {
                                const auto& item = j["item"];
                                if (item.value("type", std::string()) == "message"
                                        && item.contains("content") && item["content"].is_array()) {
                                    for (const auto& c : item["content"]) {
                                        if ((c.value("type", std::string()) == "output_text"
                                             || c.value("type", std::string()) == "text")
                                                && c.contains("text") && c["text"].is_string()) {
                                            out_text += c["text"].get<std::string>();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
            frame_buf.clear();
        }
    }

    if (!res.success && !res.error.empty()) {
        // keep error
    } else {
        res.success = true;
        res.text = out_text;
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t_start).count();
        logger::info("LLMClient[realtime]: done in " + std::to_string(elapsed_ms)
                     + " ms text_chars=" + std::to_string(out_text.size()));
    }

    size_t sent = 0;
    curl_ws_send(c, "", 0, &sent, 0, CURLWS_CLOSE);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return res;
}

} // namespace kbot
} // namespace polymech
