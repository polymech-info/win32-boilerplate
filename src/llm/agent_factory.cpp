#include "agent_factory.hpp"
#include "agent_internal.hpp"  // detail::emit, detail::merge_llm_usage_from_provider_meta

#include "path_tool_executor.hpp"
#include "llm/tools/run/RunTool.hpp"
#include "agent_memory.hpp"
#include "core/cli_cancel.hpp"
#include "logger/logger.h"

#include "kbot.h"
#include "llm_client.h"

#include <chrono>
#include <mutex>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace media::llm::agent {
namespace {

#if defined(_WIN32)
std::wstring agent_getenv_w(const wchar_t* name)
{
    const DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(needed, L'\0');
    const DWORD n = ::GetEnvironmentVariableW(name, value.data(), needed);
    if (n == 0) return {};
    value.resize(n);
    return value;
}

bool agent_path_contains_part(const std::wstring& path, const std::wstring& part)
{
    if (path.empty() || part.empty()) return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(L';', start);
        std::wstring item = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        std::wstring cmp = part;
        while (!item.empty() && (item.back() == L'\\' || item.back() == L'/')) item.pop_back();
        while (!cmp.empty() && (cmp.back() == L'\\' || cmp.back() == L'/')) cmp.pop_back();
        if (!item.empty() && ::CompareStringOrdinal(item.c_str(), -1, cmp.c_str(), -1, TRUE) == CSTR_EQUAL)
            return true;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return false;
}

std::wstring agent_exe_adjacent_bin_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    const size_t slash = buf.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    std::wstring bin = buf.substr(0, slash + 1) + L"bin";
    const DWORD attrs = ::GetFileAttributesW(bin.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return bin;
}

void ensure_agent_process_path_has_exe_bin()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const std::wstring bin = agent_exe_adjacent_bin_dir();
        if (bin.empty()) return;
        std::wstring path = agent_getenv_w(L"Path");
        if (path.empty()) path = agent_getenv_w(L"PATH");
        if (agent_path_contains_part(path, bin)) return;
        path = path.empty() ? bin : bin + L";" + path;
        ::SetEnvironmentVariableW(L"Path", path.c_str());
    });
}
#else
void ensure_agent_process_path_has_exe_bin() {}
#endif

} // namespace

// ── The loop engine ───────────────────────────────────────────────────────────

Result Agent::run(const Turn&             turn,
                  const ProviderSettings& provider,
                  EventCallback           on_event) const
{
    ensure_agent_process_path_has_exe_bin();

    Result res;
    res.transcript                        = nlohmann::json::object();
    res.transcript["messages"]            = nlohmann::json::array();
    res.llm_usage_aggregate               = nlohmann::json::object();
    res.llm_usage_aggregate["prompt_tokens"]     = 0;
    res.llm_usage_aggregate["completion_tokens"] = 0;
    res.llm_usage_aggregate["total_tokens"]      = 0;
    res.llm_usage_aggregate["llm_rounds"]        = nlohmann::json::array();

    // ── Phase 1: let features prepare the run ─────────────────────────────────
    Turn working = turn;            // mutable working copy
    PreparedRun prepared;
    prepared.cb = on_event;

    for (const auto& f : opts_.features)
        f->on_prepare(working, provider, prepared);

    // Fallback: if no feature provided a system prompt, build the default one.
    if (prepared.system_prompt.empty())
        prepared.system_prompt = build_system_prompt(working);

    // ── Phase 2: initial message history ──────────────────────────────────────
    std::vector<polymech::kbot::ChatMessage> messages;
    messages.push_back({"system", prepared.system_prompt, {}, nlohmann::json()});
    messages.push_back({"user",   working.user_prompt,     {}, nlohmann::json()});

    for (const auto& m : messages)
        res.transcript["messages"].push_back({{"role", m.role}, {"content", m.content}});

    if (!detail::emit(prepared.cb,
                      Event{Event::TurnStarted, working.user_prompt, "",
                            nlohmann::json{{"selection", working.selection},
                                           {"folder",    working.folder_hint}}})) {
        res.cancelled = true;
        // Phase 3 (on_finish) still runs even on early exit.
        for (const auto& f : opts_.features) f->on_finish(working, res);
        return res;
    }

    // ── Phase 3: configure kbot ───────────────────────────────────────────────
    polymech::kbot::KBotOptions opts;
    opts.router         = provider.router;
    opts.base_url       = provider.base_url;
    opts.api_key        = provider.api_key;
    opts.model          = provider.model;
    opts.llm_timeout_ms = provider.timeout_ms;
    polymech::kbot::LLMClient client(opts);

    logger::debug(std::string("agent: api_mode=")
                  + (provider.api_mode == LlmApiMode::Responses ? "responses"
                     : provider.api_mode == LlmApiMode::Realtime ? "realtime"
                     : "completion")
                  + " streaming="
                  + (provider.streaming_mode == LlmStreamingMode::On ? "on"
                     : provider.streaming_mode == LlmStreamingMode::Off ? "off"
                     : "auto"));

    nlohmann::json tool_choice;  // null → let the model decide ("auto")

    // ── Phase 4: LLM + tool loop ──────────────────────────────────────────────
    const int max_iter = std::max(1, provider.max_iterations);
    std::string prev_response_id;

    for (int iter = 0; iter < max_iter; ++iter) {
        if (media::cli::cancel_requested()) {
            res.cancelled           = true;
            res.error               = "interrupted (Ctrl+C)";
            res.transcript["error"] = res.error;
            (void)detail::emit(prepared.cb, Event{Event::Error, res.error, "", nlohmann::json()});
            for (const auto& f : opts_.features) f->on_finish(working, res);
            return res;
        }
        ++res.iterations;

        // LLM call —————————————————————————————————————————————————————————
        const auto t_llm = std::chrono::steady_clock::now();
        polymech::kbot::LLMResponse resp;
        if (provider.api_mode == LlmApiMode::Responses) {
            const bool stream_on = (provider.streaming_mode == LlmStreamingMode::On);
            if (stream_on) {
                resp = client.execute_responses_stream(
                    messages, prepared.tools, tool_choice, prev_response_id,
                    [&](const std::string& delta) -> bool {
                        if (!delta.empty())
                            return detail::emit(prepared.cb, Event{Event::TextDelta, delta, "", nlohmann::json()});
                        return true;
                    });
                // Provider/proxy stream compatibility fallback:
                // if streaming returns no text/tool/reasoning/error, run one non-stream call
                // to avoid blank assistant outputs under --streaming on.
                if (resp.success && resp.text.empty() && resp.tool_calls.empty()
                        && resp.reasoning_text.empty() && resp.error.empty()) {
                    logger::warn("agent responses streaming: empty stream payload; "
                                 "falling back to non-stream /responses for this round");
                    resp = client.execute_responses(messages, prepared.tools, tool_choice, prev_response_id);
                }
            } else {
                if (provider.streaming_mode == LlmStreamingMode::Auto) {
                    // Current default remains non-streaming for compatibility.
                }
                resp = client.execute_responses(messages, prepared.tools, tool_choice, prev_response_id);
            }
        } else if (provider.api_mode == LlmApiMode::Realtime) {
            if (provider.max_iterations > 0) {
                logger::warn("agent realtime PoC: tools are disabled in realtime mode; forcing text-only turn");
            }
            resp = client.execute_realtime_text(messages);
        } else {
            if (provider.streaming_mode == LlmStreamingMode::On)
                logger::warn("agent completion mode: --streaming on is not implemented yet; using non-streaming call");
            resp = client.execute_chat_messages(messages, prepared.tools, tool_choice);
        }
        const auto llm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t_llm).count();

        if (!resp.success) {
            res.error               = "LLM error: " + resp.error;
            res.transcript["error"] = res.error;
            (void)detail::emit(prepared.cb, Event{Event::Error, res.error, "", nlohmann::json()});
            for (const auto& f : opts_.features) f->on_finish(working, res);
            return res;
        }

        if (!resp.response_id.empty()) prev_response_id = resp.response_id;

        detail::merge_llm_usage_from_provider_meta(
            res.llm_usage_aggregate, resp.provider_meta_json, provider.router);

        // Stamp duration onto the last llm_rounds[] entry.
        if (res.llm_usage_aggregate["llm_rounds"].is_array()
                && !res.llm_usage_aggregate["llm_rounds"].empty())
            res.llm_usage_aggregate["llm_rounds"].back()["duration_ms"] = llm_ms;

        // Emit per-round telemetry —————————————————————————————————————————
        {
            nlohmann::json rp;
            rp["round"]       = iter + 1;
            rp["duration_ms"] = llm_ms;
            rp["stateful"]    = (provider.api_mode == LlmApiMode::Responses
                                 && iter > 0 && !resp.response_id.empty());
            if (!resp.response_id.empty()) rp["response_id"] = resp.response_id;
            if (!resp.provider_meta_json.empty()) {
                try {
                    auto meta = nlohmann::json::parse(resp.provider_meta_json);
                    if (meta.contains("usage") && meta["usage"].is_object()) {
                        const auto& u = meta["usage"];
                        nlohmann::json tok;
                        if (u.contains("input_tokens"))               tok["input"]     = u["input_tokens"];
                        else if (u.contains("prompt_tokens"))         tok["input"]     = u["prompt_tokens"];
                        if (u.contains("output_tokens"))              tok["output"]    = u["output_tokens"];
                        else if (u.contains("completion_tokens"))     tok["output"]    = u["completion_tokens"];
                        if (u.contains("output_tokens_details")
                                && u["output_tokens_details"].contains("reasoning_tokens"))
                            tok["reasoning"] = u["output_tokens_details"]["reasoning_tokens"];
                        if (!tok.empty()) rp["tokens"] = std::move(tok);
                    }
                    if (meta.contains("model") && meta["model"].is_string())
                        rp["model"] = meta["model"];
                } catch (...) {}
            }
            (void)detail::emit(prepared.cb, Event{Event::LlmRound, "", "", std::move(rp)});
        }

        // Final text — no more tool calls ——————————————————————————————————
        if (resp.tool_calls.empty()) {
            if (!resp.reasoning_text.empty())
                (void)detail::emit(prepared.cb,
                                   Event{Event::Thinking, resp.reasoning_text, "", nlohmann::json()});
            res.final_text = resp.text;
            messages.push_back({"assistant", resp.text, {}, nlohmann::json()});
            res.transcript["messages"].push_back({{"role", "assistant"}, {"content", resp.text}});
            if (!resp.reasoning_text.empty())
                res.transcript["messages"].back()["reasoning"] = resp.reasoning_text;
            (void)detail::emit(prepared.cb,
                               Event{Event::AssistantText, resp.text, "", nlohmann::json()});
            (void)detail::emit(prepared.cb,
                               Event{Event::Done,          resp.text, "", nlohmann::json()});
            res.ok = true;
            for (const auto& f : opts_.features) f->on_finish(working, res);
            return res;
        }

        // Serialise the assistant's tool-call request ——————————————————————
        // OpenAI spec: function.arguments MUST be a JSON string on the wire.
        nlohmann::json tc_array = nlohmann::json::array();
        for (const auto& tc : resp.tool_calls) {
            nlohmann::json entry{
                {"id",   tc.id},
                {"type", "function"},
                {"function", {{"name",      tc.name},
                              {"arguments", tc.arguments.is_string()
                                                ? tc.arguments.get<std::string>()
                                                : tc.arguments.dump()}}},
            };
            if (!tc.item_id.empty()) entry["item_id"] = tc.item_id;
            tc_array.push_back(std::move(entry));
        }
        polymech::kbot::ChatMessage asst_msg{"assistant", "", "", tc_array};
        if (!resp.responses_output_items.empty())
            asst_msg.responses_output_items = resp.responses_output_items;
        messages.push_back(std::move(asst_msg));
        res.transcript["messages"].push_back({{"role",       "assistant"},
                                              {"content",    nullptr},
                                              {"tool_calls", tc_array}});

        // Dispatch tool calls ——————————————————————————————————————————————
        const std::string interrupt_msg = "interrupted (Ctrl+C)";
        for (std::size_t tci = 0; tci < resp.tool_calls.size(); ++tci) {
            if (media::cli::cancel_requested()) {
                for (std::size_t tj = tci; tj < resp.tool_calls.size(); ++tj) {
                    const auto& tcr = resp.tool_calls[tj];
                    nlohmann::json err_env{{"ok", false}, {"error", interrupt_msg}};
                    (void)detail::emit(prepared.cb,
                                       Event{Event::ToolResult, "", tcr.name,
                                             nlohmann::json{{"id", tcr.id}, {"envelope", err_env}}});
                    messages.push_back({"tool", err_env.dump(), tcr.id, nlohmann::json()});
                    res.transcript["messages"].push_back(
                        {{"role", "tool"}, {"tool_call_id", tcr.id}, {"content", err_env.dump()}});
                }
                res.cancelled           = true;
                res.error               = interrupt_msg;
                res.transcript["error"] = res.error;
                (void)detail::emit(prepared.cb,
                                   Event{Event::Error, res.error, "", nlohmann::json()});
                for (const auto& f : opts_.features) f->on_finish(working, res);
                return res;
            }

            const auto& tc = resp.tool_calls[tci];

            // Guard: if the model's arguments are null/non-object (e.g. output
            // truncated by max_tokens), skip the call with a clear error so the
            // model knows it must retry with a shorter response.
            if (!tc.arguments.is_object()) {
                nlohmann::json err_env{
                    {"ok", false},
                    {"error", "Tool call for '" + tc.name + "' had no arguments "
                              "(output was likely truncated by max_tokens). "
                              "Retry with a shorter response or split into smaller steps."}};
                (void)detail::emit(prepared.cb,
                                   Event{Event::ToolResult, "", tc.name,
                                         nlohmann::json{{"id", tc.id}, {"envelope", err_env}}});
                messages.push_back({"tool", err_env.dump(), tc.id, nlohmann::json()});
                res.transcript["messages"].push_back(
                    {{"role", "tool"}, {"tool_call_id", tc.id}, {"content", err_env.dump()}});
                continue;
            }

            // Merge UI-level overrides into args before execution.
            nlohmann::json args = tc.arguments;
            auto merge_ui_overrides = [&args](const nlohmann::json& ov) {
                if (!ov.is_object()) return;
                for (auto it = ov.begin(); it != ov.end(); ++it) {
                    const std::string& k = it.key();
                    const auto&        v = it.value();
                    if (v.is_null()) continue;
                    if (v.is_string()) {
                        const std::string sv = v.get<std::string>();
                        if (sv.empty())      continue;
                        if (sv == "true")  { args[k] = true;  continue; }
                        if (sv == "false") { args[k] = false; continue; }
                        bool all_digit = true;
                        for (unsigned char c : sv)
                            if (!std::isdigit(c)) { all_digit = false; break; }
                        if (all_digit) {
                            try { args[k] = std::stoll(sv); } catch (...) { args[k] = v; }
                            continue;
                        }
                        args[k] = v;
                    } else {
                        args[k] = v;
                    }
                }
            };
            if (tc.name == "create_video")
                merge_ui_overrides(working.create_video_ui_overrides);

            (void)detail::emit(prepared.cb,
                               Event{Event::ToolCall, "", tc.name,
                                     nlohmann::json{{"id", tc.id}, {"arguments", args}}});

            // Wire per-file progress for image_transform.
            const bool image_transform_progress =
                (tc.name == "image_transform" || tc.name == "transform") && prepared.cb;
            struct ClearTransformHook {
                bool armed = false;
                ~ClearTransformHook() {
                    if (armed) path::clear_image_transform_file_progress_hook();
                }
            } clear_hook;
            if (image_transform_progress) {
                clear_hook.armed = true;
                path::set_image_transform_file_progress_hook(
                    [&](const nlohmann::json& one_file_env) {
                        (void)detail::emit(prepared.cb,
                                           Event{Event::ToolFileProgress, "", tc.name,
                                                 nlohmann::json{{"id",       tc.id},
                                                                {"envelope", one_file_env}}});
                    });
            }

            // Wire streaming output hook for run tool.
            const bool run_streaming = (tc.name == "run") && prepared.cb;
            struct ClearRunHook {
                bool armed = false;
                ~ClearRunHook() { if (armed) run::clear_output_chunk_hook(); }
            } clear_run_hook;
            if (run_streaming) {
                clear_run_hook.armed = true;
                // Emit a "start" event so the UI can show the command header immediately.
                {
                    std::string cmd_text;
                    try { cmd_text = args.value("command", std::string{}); } catch (...) {}
                    (void)detail::emit(prepared.cb,
                                       Event{Event::ToolFileProgress, "", tc.name,
                                             nlohmann::json{{"id",      tc.id},
                                                            {"event",   "start"},
                                                            {"command", cmd_text}}});
                }
                run::set_output_chunk_hook(
                    [&](const std::string& stream, const std::string& chunk) {
                        (void)detail::emit(prepared.cb,
                                           Event{Event::ToolFileProgress, "", tc.name,
                                                 nlohmann::json{{"id",     tc.id},
                                                                {"stream", stream},
                                                                {"chunk",  chunk}}});
                    });
            }

            std::string    envelope_str;
            nlohmann::json tool_result_envelope;
            const auto     t_tool = std::chrono::steady_clock::now();

            if (prepared.mcp_bridge
                    && prepared.mcp_bridge->is_mcp_openai_function(tc.name)) {
                std::string mcp_err;
                if (!prepared.mcp_bridge->call_tool(tc.name, args, envelope_str, mcp_err)) {
                    tool_result_envelope = nlohmann::json{
                        {"ok", false},
                        {"error", mcp_err.empty() ? "MCP tool dispatch failed" : mcp_err}};
                    envelope_str = tool_result_envelope.dump();
                } else {
                    try { tool_result_envelope = nlohmann::json::parse(envelope_str); }
                    catch (...) { tool_result_envelope = nlohmann::json::object(); }
                }
            } else {
                path::ExecuteResult er = path::execute(tc.name, args);
                envelope_str           = path::envelope_text(er);
                tool_result_envelope   = std::move(er.envelope);
            }

            const auto tool_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t_tool).count();

            (void)detail::emit(prepared.cb,
                               Event{Event::ToolResult, "", tc.name,
                                     nlohmann::json{{"id",          tc.id},
                                                    {"envelope",    tool_result_envelope},
                                                    {"duration_ms", tool_ms}}});

            if (run_streaming) {
                bool run_ok = false;
                try { run_ok = tool_result_envelope.value("ok", false); } catch (...) {}
                (void)detail::emit(prepared.cb,
                                   Event{Event::ToolFileProgress, "", tc.name,
                                         nlohmann::json{{"id",          tc.id},
                                                        {"event",       "done"},
                                                        {"exit_code",   tool_result_envelope.value("exit_code", -1)},
                                                        {"duration_ms", tool_ms},
                                                        {"ok",          run_ok}}});
            }

            messages.push_back({"tool", envelope_str, tc.id, nlohmann::json()});
            res.transcript["messages"].push_back({{"role",         "tool"},
                                                  {"tool_call_id", tc.id},
                                                  {"content",      tool_result_envelope}});
        }
    }

    // Exhausted max_iterations.
    res.error = "agent: hit max_iterations (" + std::to_string(max_iter)
                + ") without a final response \u2014 model kept calling tools";
    res.transcript["error"] = res.error;
    (void)detail::emit(prepared.cb, Event{Event::Error, res.error, "", nlohmann::json()});
    for (const auto& f : opts_.features) f->on_finish(working, res);
    return res;
}

// ── AgentFactory::default_agent ───────────────────────────────────────────────

namespace AgentFactory {

Agent default_agent() {
    AgentOptions opts;
    opts.features.push_back(feature_path_scope());    // 1. pin base path + blocklist (RAII)
#if FEATURE_HOME_LLM_TOOLS
    opts.features.push_back(feature_tools());         // 2. build path-catalog tools JSON
#if FEATURE_MCP_CLIENT
    opts.features.push_back(feature_mcp());           // 3. merge mcp.json tools + create bridge
#endif
#endif
    opts.features.push_back(feature_memory());        // 4. thread-local task-ID scope
#if FEATURE_HOME_LLM_SKILLS
    opts.features.push_back(feature_skills());        // 5. discover skills and append summary to system extra
#endif
    opts.features.push_back(feature_system_prompt()); // 6. assemble system prompt (sees final Turn + tools)
    return Agent(std::move(opts));
}

} // namespace AgentFactory

} // namespace media::llm::agent
