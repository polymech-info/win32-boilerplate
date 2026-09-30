// Polymech image ops — C JSON bridge for CodeEdit / AppKit. Uses the same C++ entry points as the CLI.
#include "pm_polymech_bridge.h"

#include "constants.hpp"
#include "logger/logger.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "compress.hpp"
#include "find.hpp"
#include "meta.hpp"
#include "resize.hpp"
#include "transform.hpp"
#include "llm/agent.hpp"
#include "llm/mcp_probe.hpp"
#include "core/settings_runtime.hpp"
#include "replicate_provider_models_cli.hpp"
#include "core/openrouter_provider_models_cli.hpp"

#include "lib/pm_service_posts.hpp"
#include "lib/pm_service_upload.hpp"
#include "lib/pm_zitadel_oauth.hpp"

using nlohmann::json;
namespace m = media;

// ── Swift UI log sink ────────────────────────────────────────────────────────

namespace {

std::atomic<pm_log_callback_t> g_log_cb{ nullptr };
std::atomic<void*>             g_log_ctx{ nullptr };

template<typename Mutex>
class swift_ui_sink final : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        auto cb = g_log_cb.load(std::memory_order_relaxed);
        if (!cb) return;
        spdlog::memory_buf_t buf;
        this->formatter_->format(msg, buf);
        std::string text = fmt::to_string(buf);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty())
            cb(text.c_str(), g_log_ctx.load(std::memory_order_relaxed));
    }
    void flush_() override {}
};

} // namespace

extern "C" void pm_polymech_set_log_callback(pm_log_callback_t callback, void* ctx)
{
    g_log_cb.store(callback, std::memory_order_relaxed);
    g_log_ctx.store(ctx,      std::memory_order_relaxed);

    // Install the spdlog sink once.
    static std::once_flag s_installed;
    std::call_once(s_installed, [] {
        try {
            if (auto logger = spdlog::default_logger()) {
                auto sink = std::make_shared<swift_ui_sink<std::mutex>>();
                logger->sinks().push_back(std::move(sink));
            }
        } catch (...) {}
    });
}

// ────────────────────────────────────────────────────────────────────────────

static char* dup_s(const std::string& s)
{
  if (s.empty()) {
    return static_cast<char*>(::calloc(1, 1));
  }
  return static_cast<char*>(::strdup(s.c_str()));
}

void pm_polymech_free(void* p)
{
  if (p != nullptr) {
    std::free(p);
  }
}

extern "C" void pm_polymech_init_process_logging(void)
{
#if defined(__APPLE__)
  logger::macos_bootstrap_file_logging_utf8(pm::brand::k_app_id_u8, "info");
#endif
}

static int set_err(const std::string& e, char** err_out, char** out_json)
{
  (void) out_json;
  if (err_out != nullptr) {
    *err_out = e.empty() ? nullptr : dup_s(e);
  }
  if (out_json != nullptr) {
    *out_json = nullptr;
  }
  return 1;
}

static int ok(const json& j, char** out_json, char** err_out)
{
  (void) err_out;
  if (out_json == nullptr) {
    return 0;
  }
  const std::string  d  = j.dump();
  *out_json  = dup_s(d);
  return 0;
}

// Fills prov from the app profile via media::runtime_settings.
// Explicit fields already set on prov (from JSON) take precedence and are not overwritten.
// After profile merge, fills api_key / base_url from providers[router] only.
// Returns false with a clear message when router or model remain unresolved.
static bool fill_chat_provider_from_runtime(media::llm::agent::ProviderSettings& prov, std::string& err)
{
  media::runtime_settings::ChatProviderSettings cs;
  if (media::runtime_settings::load_chat_provider(cs, err)) {
    if (prov.router.empty()) prov.router = cs.router;
    // Do not apply the saved base URL or model when the turn targets a different router.
    if (prov.base_url.empty()) {
      if (prov.router.empty() || cs.router.empty() || prov.router == cs.router)
        prov.base_url = cs.base_url;
    }
    if (prov.model.empty()) {
      if (prov.router.empty() || cs.router.empty() || prov.router == cs.router)
        prov.model = cs.model;
    }
    if (prov.api_key.empty()) prov.api_key = cs.api_key;
    if (prov.timeout_ms <= 0 && cs.timeout_ms > 0) prov.timeout_ms = cs.timeout_ms;
    if (prov.max_iterations <= 0 && cs.max_iterations > 0) prov.max_iterations = cs.max_iterations;
  }
  if (prov.timeout_ms <= 0) prov.timeout_ms = 60'000;
  if (prov.max_iterations <= 0) prov.max_iterations = 8;

  // Fill api_key / base_url from providers[router] only — no active/first fallback.
  if (!prov.router.empty())
    media::runtime_settings::merge_provider_credentials(prov.router, false, prov.api_key, prov.base_url);

  if (prov.router.empty()) {
    err = "chat.router is not configured; set chat.router in settings";
    return false;
  }
  if (prov.model.empty()) {
    err = "chat.model is not configured; set chat.model in settings";
    return false;
  }
  return true;
}

// ── resize ─────────────────────────────────────────────
int32_t pm_polymech_resize(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) {
    return set_err("out_json null", err_out, out_json);
  }
  *out_json = nullptr;
  if (err_out != nullptr) {
    *err_out = nullptr;
  }
  if (json_in == nullptr) {
    return set_err("json_in null", err_out, out_json);
  }
  try {
    const json  in  = json::parse(json_in);
    if (!in.is_object()) {
      return set_err("json must be object", err_out, out_json);
    }
    if (!in.contains("input") || !in["input"].is_string()) {
      return set_err("input (string) required", err_out, out_json);
    }
    if (!in.contains("output") || !in["output"].is_string()) {
      return set_err("output (string) required", err_out, out_json);
    }
    const std::string  ip  = in["input"].get<std::string>();
    const std::string  op  = in["output"].get<std::string>();
    m::ResizeOptions  opt{};
    if (in.contains("options") && in["options"].is_object()) {
      m::apply_resize_options_from_json(in["options"], opt);
    }
    std::string  e;
    if (!m::resize_file(ip, op, opt, e)) {
      return set_err(e, err_out, out_json);
    }
    return ok({{"ok", true}, {"input", ip}, {"output", op}}, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── meta ───────────────────────────────────────────────
int32_t pm_polymech_meta(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) {
    return set_err("out_json null", err_out, out_json);
  }
  *out_json = nullptr;
  if (err_out != nullptr) {
    *err_out = nullptr;
  }
  if (json_in == nullptr) {
    return set_err("json_in null", err_out, out_json);
  }
  try {
    const json  in  = json::parse(json_in);
    if (!in.contains("input") || !in["input"].is_string()) {
      return set_err("input (string) required", err_out, out_json);
    }
    const std::string  path  = in["input"].get<std::string>();
    m::MetaOptions  o{};
    if (in.contains("options") && in["options"].is_object()) {
      m::apply_meta_options_from_json(in["options"], o);
    }
    m::MetaResult  r  = m::meta_extract(path, o, nullptr);
    if (!r.ok) {
      return set_err(r.error.empty() ? "meta failed" : r.error, err_out, out_json);
    }
    json  j
        = {{"ok", true},
           {"markdown", r.markdown},
           {"json_text", r.json_text},
           {"md_path", r.md_path},
           {"json_path", r.json_path},
           {"exif_updated", r.exif_updated},
           {"bytes_sent", r.bytes_sent},
           {"resized_w", r.resized_w},
           {"resized_h", r.resized_h},
           {"orig_w", r.orig_w},
           {"orig_h", r.orig_h}};
    return ok(j, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── compress ───────────────────────────────────────────
int32_t pm_polymech_compress(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) {
    return set_err("out_json null", err_out, out_json);
  }
  *out_json = nullptr;
  if (err_out != nullptr) {
    *err_out = nullptr;
  }
  if (json_in == nullptr) {
    return set_err("json_in null", err_out, out_json);
  }
  try {
    const json  in  = json::parse(json_in);
    if (!in.contains("input") || !in["input"].is_string()) {
      return set_err("input (string) required", err_out, out_json);
    }
    if (!in.contains("output") || !in["output"].is_string()) {
      return set_err("output (string) required", err_out, out_json);
    }
    const std::string  ip  = in["input"].get<std::string>();
    const std::string  op  = in["output"].get<std::string>();
    m::CompressOptions  c{};
    if (in.contains("options") && in["options"].is_object()) {
      m::apply_compress_options_from_json(in["options"], c);
    }
    const std::string  e  = m::compress_file(ip, op, c);
    if (!e.empty()) {
      return set_err(e, err_out, out_json);
    }
    return ok({{"ok", true}, {"input", ip}, {"output", op}}, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── find ───────────────────────────────────────────────
int32_t pm_polymech_find(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) {
    return set_err("out_json null", err_out, out_json);
  }
  *out_json = nullptr;
  if (err_out != nullptr) {
    *err_out = nullptr;
  }
  if (json_in == nullptr) {
    return set_err("json_in null", err_out, out_json);
  }
  try {
    const json  in  = json::parse(json_in);
    if (!in.contains("inputs") || !in["inputs"].is_array()) {
      return set_err("inputs (string array) required", err_out, out_json);
    }
    std::vector<std::string>  ins;
    for (const auto&  el  : in["inputs"]) {
      if (el.is_string()) {
        ins.push_back(el.get<std::string>());
      }
    }
    m::FindOptions  o{};
    if (in.contains("options") && in["options"].is_object()) {
      m::apply_find_options_from_json(in["options"], o);
    }
    m::FindResult  r  = m::find_images(ins, o, nullptr, nullptr, nullptr);
    if (!r.ok) {
      return set_err(r.error.empty() ? "find failed" : r.error, err_out, out_json);
    }
    json  marr  = json::array();
    for (const auto&  mm  : r.matches) {
      marr.push_back({{"path", mm.path},
                      {"score", mm.score},
                      {"source", mm.source},
                      {"reason", mm.reason}});
    }
    json  j
        = {{"ok", true},
           {"matches", marr},
           {"scanned", r.scanned},
           {"considered", r.considered},
           {"generated", r.generated},
           {"cache_hits", r.cache_hits}};
    return ok(j, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── transform ───────────────────────────────────────
int32_t pm_polymech_transform(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) {
    return set_err("out_json null", err_out, out_json);
  }
  *out_json = nullptr;
  if (err_out != nullptr) {
    *err_out = nullptr;
  }
  if (json_in == nullptr) {
    return set_err("json_in null", err_out, out_json);
  }
  try {
    const json  in  = json::parse(json_in);
    if (!in.contains("input") || !in["input"].is_string()) {
      return set_err("input (string) required", err_out, out_json);
    }
    const std::string  path  = in["input"].get<std::string>();
    std::string        outP;
    if (!in.contains("output") || in["output"].is_null()) {
      outP.clear();
    } else if (in["output"].is_string()) {
      outP  = in["output"].get<std::string>();
    } else {
      return set_err("output: omit, null, or string path", err_out, out_json);
    }
    m::TransformOptions  t{};
    if (in.contains("options") && in["options"].is_object()) {
      m::apply_transform_options_from_json(in["options"], t);
    }
    m::TransformResult  r
        = m::transform_image(path, outP, t, nullptr, nullptr, nullptr);
    if (!r.ok) {
      return set_err(r.error.empty() ? "transform failed" : r.error, err_out, out_json);
    }
    json  j  = {{"ok", true}, {"output_path", r.output_path}, {"ai_text", r.ai_text}};
    if (!r.image_data.empty()) {
      j["image_bytes"] = r.image_data.size();
    }
    return ok(j, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── chat turn (native llm::agent) ───────────────────────────────────────
int32_t pm_polymech_chat_turn(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) return set_err("out_json null", err_out, out_json);
  *out_json = nullptr;
  if (err_out != nullptr) *err_out = nullptr;
  if (json_in == nullptr) return set_err("json_in null", err_out, out_json);
  try {
    const json in = json::parse(json_in);
    if (!in.is_object()) return set_err("json must be object", err_out, out_json);
    if (!in.contains("prompt") || !in["prompt"].is_string()) {
      return set_err("prompt (string) required", err_out, out_json);
    }

    media::llm::agent::Turn turn;
    turn.user_prompt = in["prompt"].get<std::string>();
    if (in.contains("selection") && in["selection"].is_array()) {
      for (const auto& el : in["selection"]) if (el.is_string()) turn.selection.push_back(el.get<std::string>());
    }
    if (in.contains("folder") && in["folder"].is_string()) {
      turn.folder_hint = in["folder"].get<std::string>();
    }
    if (in.contains("system_extra") && in["system_extra"].is_string()) {
      turn.system_extra = in["system_extra"].get<std::string>();
    }
    if (in.contains("disable_tools") && in["disable_tools"].is_array()) {
      for (const auto& el : in["disable_tools"]) if (el.is_string()) turn.disabled_path_tools.push_back(el.get<std::string>());
    }
    if (in.contains("mcp_tools_enabled") && in["mcp_tools_enabled"].is_boolean()) {
      turn.mcp_tools_enabled = in["mcp_tools_enabled"].get<bool>();
    }
    if (in.contains("disabled_mcp_servers") && in["disabled_mcp_servers"].is_array()) {
      for (const auto& el : in["disabled_mcp_servers"]) if (el.is_string()) turn.disabled_mcp_servers.push_back(el.get<std::string>());
    }

    media::llm::agent::ProviderSettings prov;
    if (in.contains("router") && in["router"].is_string()) prov.router = in["router"].get<std::string>();
    if (in.contains("base_url") && in["base_url"].is_string()) prov.base_url = in["base_url"].get<std::string>();
    if (in.contains("api_key") && in["api_key"].is_string()) prov.api_key = in["api_key"].get<std::string>();
    if (in.contains("model") && in["model"].is_string()) prov.model = in["model"].get<std::string>();
    if (in.contains("timeout_ms") && in["timeout_ms"].is_number_integer()) prov.timeout_ms = in["timeout_ms"].get<int>();
    if (in.contains("max_iterations") && in["max_iterations"].is_number_integer()) prov.max_iterations = in["max_iterations"].get<int>();
    std::string fill_err;
    if (!fill_chat_provider_from_runtime(prov, fill_err))
      return set_err(fill_err, err_out, out_json);

    json events = json::array();
    media::llm::agent::EventCallback cb = [&](const media::llm::agent::Event& e) {
      json row;
      switch (e.kind) {
        case media::llm::agent::Event::TurnStarted: row["kind"] = "turn_started"; break;
        case media::llm::agent::Event::ToolCall: row["kind"] = "tool_call"; break;
        case media::llm::agent::Event::ToolResult: row["kind"] = "tool_result"; break;
        case media::llm::agent::Event::ToolFileProgress: row["kind"] = "tool_file_progress"; break;
        case media::llm::agent::Event::AssistantText: row["kind"] = "assistant_text"; break;
        case media::llm::agent::Event::Error: row["kind"] = "error"; break;
        case media::llm::agent::Event::Done: row["kind"] = "done"; break;
      }
      if (!e.text.empty()) row["text"] = e.text;
      if (!e.tool_name.empty()) row["tool"] = e.tool_name;
      if (!e.payload.is_null() && !e.payload.empty()) row["payload"] = e.payload;
      events.push_back(std::move(row));
      return true;
    };

    media::llm::agent::Result r = media::llm::agent::run_turn(turn, prov, cb);
    json out{
      {"ok", r.ok},
      {"error", r.error},
      {"final_text", r.final_text},
      {"cancelled", r.cancelled},
      {"iterations", r.iterations},
      {"events", std::move(events)},
      {"transcript", r.transcript}
    };
    return ok(out, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── Replicate collections / models (shared with CLI; parity with Win32 `ReplicateSelectorController::reload`) ──
int32_t pm_polymech_replicate(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) return set_err("out_json null", err_out, out_json);
  *out_json = nullptr;
  if (err_out != nullptr) *err_out = nullptr;
  if (json_in == nullptr) return set_err("json_in null", err_out, out_json);
  try {
    const json in = json::parse(json_in);
    if (!in.is_object()) return set_err("json must be object", err_out, out_json);
    const std::string op = in.value("op", std::string{});

    if (op == "collections") {
      const bool force = in.value("force_refresh", false);
      std::vector<media::replicate_cli::CollectionInfo> cols;
      std::string                                       rerr;
      if (!media::replicate_cli::fetch_collections(
              in.value("api_key", std::string{}), in.value("base_url", std::string{}), cols, rerr, force)) {
        return set_err(rerr.empty() ? "fetch_collections failed" : rerr, err_out, out_json);
      }
      json arr = json::array();
      for (const auto& c : cols) {
        arr.push_back(
            {{"name", c.name}, {"slug", c.slug}, {"description", c.description}});
      }
      return ok({{"ok", true}, {"collections", std::move(arr)}}, out_json, err_out);
    }
    if (op == "models") {
      const std::string coll  = in.value("collection", std::string{"official"});
      const bool        force = in.value("force_refresh", false);
      std::vector<media::replicate_cli::ModelInfo> models;
      std::string                                rerr;
      if (!media::replicate_cli::fetch_collection_models(
              in.value("api_key", std::string{}), in.value("base_url", std::string{}), coll, models, rerr, force)) {
        return set_err(rerr.empty() ? "fetch_collection_models failed" : rerr, err_out, out_json);
      }
      media::replicate_cli::sort_models_alpha(models);
      json arr = json::array();
      for (const auto& m : models) {
        arr.push_back({{"slug", m.slug},
                       {"description", m.description},
                       {"url", m.url},
                       {"visibility", m.visibility},
                       {"is_official", m.is_official}});
      }
      return ok({{"ok", true}, {"models", std::move(arr)}}, out_json, err_out);
    }
    if (op == "resolve_collection") {
      std::string out_slug;
      if (!media::replicate_cli::resolve_collection_for_model_cached(in.value("model_slug", std::string{}), out_slug)
          || out_slug.empty()) {
        return ok({{"ok", true}, {"collection", nullptr}}, out_json, err_out);
      }
      return ok({{"ok", true}, {"collection", out_slug}}, out_json, err_out);
    }
    if (op == "openapi_input_flat") {
      const std::string slug = in.value("model_slug", std::string{});
      if (slug.empty())
        return set_err("openapi_input_flat: missing model_slug", err_out, out_json);
      json                flat = json::object();
      json                req  = json::array();
      std::string         rerr;
      if (!media::replicate_cli::lookup_replicate_openapi_input_flat(slug, flat, req, rerr))
        return set_err(rerr.empty() ? "openapi_input_flat failed" : rerr, err_out, out_json);
      return ok({{"ok", true},
                 {"model_slug", slug},
                 {"required", std::move(req)},
                 {"properties", std::move(flat)}},
                out_json, err_out);
    }
    return set_err(
        "replicate: unknown op (use collections, models, resolve_collection, openapi_input_flat)", err_out, out_json);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── OpenRouter `/v1/models` catalog (parity with Win32 `OpenRouterSelectorController::reload`) ──
int32_t pm_polymech_openrouter(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) return set_err("out_json null", err_out, out_json);
  *out_json = nullptr;
  if (err_out != nullptr) *err_out = nullptr;
  if (json_in == nullptr) return set_err("json_in null", err_out, out_json);
  try {
    const json in = json::parse(json_in);
    if (!in.is_object()) return set_err("json must be object", err_out, out_json);
    const std::string op = in.value("op", std::string{});
    if (op != "models")
      return set_err("openrouter: unknown op (use models)", err_out, out_json);
    const bool force = in.value("force_refresh", false);
    std::vector<media::openrouter_cli::OpenRouterCatalogModelRow> rows;
    std::string                                                  rerr;
    if (!media::openrouter_cli::list_openrouter_catalog_models(
            in.value("api_key", std::string{}), in.value("base_url", std::string{}), rows, rerr, force)) {
      return set_err(rerr.empty() ? "openrouter catalog failed" : rerr, err_out, out_json);
    }
    json arr = json::array();
    for (const auto& m : rows) {
      arr.push_back({{"id", m.id},
                     {"name", m.name},
                     {"description", m.description},
                     {"open_url", m.open_url},
                     {"detail_head", m.detail_head}});
    }
    return ok({{"ok", true}, {"models", std::move(arr)}}, out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── Pixlwiz service: create post + pictures (parity with `pm-image service posts create`) ──
int32_t pm_polymech_service_create_post(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr)
    return set_err("out_json null", err_out, out_json);
  *out_json = nullptr;
  if (err_out != nullptr)
    *err_out = nullptr;
  if (json_in == nullptr)
    return set_err("json_in null", err_out, out_json);
  try {
    const json in = json::parse(json_in);
    if (!in.is_object())
      return set_err("json must be object", err_out, out_json);
    if (!in.contains("images") || !in["images"].is_array() || in["images"].empty())
      return set_err("images (non-empty array of path strings) required", err_out, out_json);

    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
    for (const auto& el : in["images"]) {
      if (!el.is_string())
        return set_err("each images[] entry must be a string path", err_out, out_json);
      std::error_code ec;
      const fs::path abs = fs::absolute(fs::path(el.get<std::string>()), ec);
      if (ec || abs.empty())
        return set_err("bad image path: " + el.get<std::string>(), err_out, out_json);
      paths.push_back(abs);
    }

    const std::string server_override = in.value("server_url", std::string{});
    std::string       base              = pm_resolve_service_server_base(server_override);
    if (base.empty() && pm::k_pixlwiz_service_server_base_default_u8[0] != '\0')
      base = pm_trim_trailing_slash(std::string(pm::k_pixlwiz_service_server_base_default_u8));
    if (base.empty())
      return set_err(
          "Set SERVER_URL / VITE_SERVER_IMAGE_API_URL or pass server_url in JSON", err_out, out_json);

    std::string bearer;
    std::string aerr;
    if (!pm_zitadel_oauth_read_access_token(bearer, aerr))
      return set_err(aerr.empty() ? "zitadel-oauth.json unreadable" : aerr, err_out, out_json);

    const std::string title       = in.value("title", std::string{});
    const std::string desc      = in.value("description", std::string{});
    const std::string vis       = in.value("visibility", std::string{"public"});
    const auto        log_line  = PmServiceUploadLogLine{};
    const auto        progress  = PmServicePostProgressLine{};
    const auto        r         = pm_service_create_post_with_pictures(base, bearer, paths, title, desc, vis,
                                                                         log_line, progress);
    if (!r.ok)
      return set_err(r.err.empty() ? "service create post failed" : r.err, err_out, out_json);

    std::string base_ns = base;
    while (!base_ns.empty() && (base_ns.back() == '/' || base_ns.back() == '\\'))
      base_ns.pop_back();
    std::string view_url = base_ns + "/post/" + r.post_id + "?view=compact";
    if (!r.picture_ids.empty() && !r.picture_ids[0].empty())
      view_url += "&pic=" + r.picture_ids[0];

    json pics = json::array();
    for (const auto& id : r.picture_ids)
      pics.push_back(id);
    return ok(json{{"ok", true},
                   {"post_id", r.post_id},
                   {"picture_ids", std::move(pics)},
                   {"view_url", std::move(view_url)}},
                out_json, err_out);
  } catch (const std::exception& ex) {
    return set_err(ex.what(), err_out, out_json);
  }
}

// ── MCP catalog probe (parity with Win32 chat_web_start_mcp_catalog_probe) ──
int32_t pm_polymech_mcp_probe(const char* json_in, char** out_json, char** err_out)
{
  if (out_json == nullptr) return set_err("out_json null", err_out, out_json);
  *out_json = nullptr;
  if (err_out != nullptr) *err_out = nullptr;
  try {
    json slim = json::object();
    const json probe = media::llm::mcp::probe_mcp_config({});
    json servers = json::array();
    if (probe.contains("servers") && probe["servers"].is_array()) {
      for (const auto& s : probe["servers"]) {
        json row;
        row["name"]          = s.value("name", std::string{});
        if (s.contains("transport")) row["transport"] = s["transport"];
        row["handshake_ok"]  = s.value("handshake_ok",  false);
        row["tools_list_ok"] = s.value("tools_list_ok", false);
        if (s.value("skipped", false)) {
          row["skipped"]     = true;
          row["skip_reason"] = s.value("skip_reason", std::string{});
        }
        row["tools"] = (s.contains("tools") && s["tools"].is_array())
                           ? s["tools"]
                           : json::array();
        servers.push_back(std::move(row));
      }
    }
    slim["servers"]       = std::move(servers);
    slim["mcp_json_path"] = probe.value("mcp_json_path", std::string{});
    slim["exists"]        = probe.value("exists", false);
    return ok(slim, out_json, err_out);
  } catch (const std::exception& ex) {
    json slim;
    slim["servers"]     = json::array();
    slim["exists"]      = false;
    slim["probe_error"] = std::string(ex.what());
    return ok(slim, out_json, err_out);
  }
}
