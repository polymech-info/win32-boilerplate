#include "path_tool_executor.hpp"

#include "agent_memory.hpp"
#include "logger/logger.h"
#include "logging/pm_log.hpp"

#include "core/cli_cancel.hpp"
#include "core/compress.hpp"
#include "core/find.hpp"
#include "core/glob_paths.hpp"
#include "core/meta.hpp"
#include "core/resize.hpp"
#include "core/search.hpp"
#include "core/transform.hpp"
#if defined(FEATURE_VIDEO) && FEATURE_VIDEO
#include "core/video.hpp"
#if defined(_WIN32)
#include <objbase.h>  // CoInitializeEx / CoUninitialize
#endif
#endif
#if defined(FEATURE_STT) && FEATURE_STT
#include "core/audio.hpp"
#include "audio/tts/elevenlabs_tts.hpp"
#include "audio/tts/proxy_tts.hpp"
#include <atomic>
#endif
#include "replicate_provider_models_cli.hpp"
#include "llm_image_tool_defaults.hpp"
#include "llm/llm_fs_guard.hpp"
#include "llm/llm_glob_filter.hpp"
#include "llm/tools/run/RunTool.hpp"
#include "llm/tools/computer_use/Tool_ComputerUse.hpp"
#include "llm/tool_executor.hpp"

#include "core/settings_store.hpp"
#include "core/settings_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace media::llm::path {

namespace fs = std::filesystem;

// thread_local: each agent run lives on its own thread (UI worker / scheduler),
// so per-thread storage gives complete isolation between concurrent agents.
static thread_local std::function<void(const nlohmann::json&)> g_image_transform_file_progress;
static thread_local std::unordered_set<std::string>            g_agent_tool_blocklist;
static thread_local std::string                                g_agent_path_base;
static thread_local bool                                       g_agent_godmode = false;

#if defined(FEATURE_STT) && FEATURE_STT
// Pointer to the AudioOutput currently blocked in play_sync, or nullptr.
// Written by do_speak on the calling thread; read by abort_active_speak from any thread.
static std::atomic<pm::audio::AudioOutput*> g_tts_out{nullptr};
#endif

POLYMECH_API void set_image_transform_file_progress_hook(
    std::function<void(const nlohmann::json& one_file_envelope)> f)
{
    g_image_transform_file_progress = std::move(f);
}

POLYMECH_API void clear_image_transform_file_progress_hook()
{
    g_image_transform_file_progress = {};
}

void set_agent_tool_blocklist(const std::vector<std::string>& disabled_names) {
    g_agent_tool_blocklist.clear();
    g_agent_tool_blocklist.reserve(disabled_names.size() * 2);
    for (const auto& d : disabled_names) {
        if (d.empty()) continue;
        std::string k = d;
        for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto b = k.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        const auto e = k.find_last_not_of(" \t\r\n");
        k = k.substr(b, e - b + 1);
        if (!k.empty()) g_agent_tool_blocklist.insert(std::move(k));
    }
}

void clear_agent_tool_blocklist() { g_agent_tool_blocklist.clear(); }

void set_agent_path_base(const std::string& raw) {
    g_agent_path_base.clear();
    if (raw.empty()) return;
    std::error_code ec;
    fs::path p(raw);
    if (!p.is_absolute()) p = fs::absolute(p, ec);
    if (ec) p = fs::path(raw);
    p = p.lexically_normal();
    g_agent_path_base = p.string();
    if (g_agent_path_base.empty() && !raw.empty()) g_agent_path_base = raw;
}

void clear_agent_path_base() { g_agent_path_base.clear(); }

void set_agent_godmode(bool on)  { g_agent_godmode = on; }
bool get_agent_godmode()         { return g_agent_godmode; }

namespace detail {

void str_tolower_in_place(std::string& s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// ── Small helpers ───────────────────────────────────────────────────────────

static std::string llm_path_fs_deny(const std::string& abs_str) {
    if (g_agent_godmode) return {};
    return media::llm::llm_fs_guard_deny_reason(fs::path(abs_str));
}

static std::string llm_path_write_deny(const std::string& abs_str) {
    if (g_agent_godmode) return {};
    return media::llm::llm_fs_guard_write_deny_reason(fs::path(abs_str));
}

const nlohmann::json& options_or_empty(const nlohmann::json& args) {
    static const nlohmann::json empty = nlohmann::json::object();
    if (args.contains("options") && args["options"].is_object()) return args["options"];
    return empty;
}

std::vector<std::string> get_string_array(const nlohmann::json& args, const char* key) {
    std::vector<std::string> out;
    if (!args.contains(key) || !args[key].is_array()) return out;
    for (const auto& v : args[key]) if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

std::optional<std::string> get_nonempty_string(const nlohmann::json& obj, const char* key) {
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_string())
        return std::nullopt;
    std::string s = obj[key].get<std::string>();
    if (s.empty())
        return std::nullopt;
    return s;
}

std::optional<std::string> get_output_path_argument(const nlohmann::json& args) {
    if (auto s = get_nonempty_string(args, "output_path"))
        return s;
    if (auto s = get_nonempty_string(args, "output"))
        return s;
    const nlohmann::json& opts = options_or_empty(args);
    if (auto s = get_nonempty_string(opts, "output_path"))
        return s;
    if (auto s = get_nonempty_string(opts, "output"))
        return s;
    return std::nullopt;
}

std::string resolve_tool_path_string(const std::string& raw);

static bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

static std::optional<fs::path> try_resolve_known_path_alias(const std::string& raw,
                                                             const fs::path&    cwd,
                                                             const fs::path&    base_eff,
                                                             const fs::path&    global_root)
{
    std::string s = raw;
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return std::nullopt;
    const auto e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
    if (s.empty())
        return std::nullopt;

    auto join_under = [](const fs::path& root, std::string tail) -> fs::path {
        while (!tail.empty() && (tail.front() == '/' || tail.front() == '\\'))
            tail.erase(tail.begin());
        if (tail.empty())
            return root.lexically_normal();
        return (root / fs::path(tail)).lexically_normal();
    };

    auto resolve_for = [&](const fs::path& root, std::string_view token) -> std::optional<fs::path> {
        if (s.size() == token.size() && starts_with_ci(s, token))
            return root.lexically_normal();
        if (starts_with_ci(s, token)) {
            const std::string tail = s.substr(token.size());
            return join_under(root, tail);
        }
        return std::nullopt;
    };
    
    for (const std::string_view t : {
             std::string_view("cwd:"),
             std::string_view("cwd/"),
             std::string_view("cwd\\"),
             std::string_view("$cwd"),
             std::string_view("${cwd}"),
             std::string_view("%cwd%"),
             std::string_view("%cd%"),
         }) {
        if (auto p = resolve_for(cwd, t))
            return p;
    }

    for (const std::string_view t : {
             std::string_view("global:"),
             std::string_view("global/"),
             std::string_view("global\\"),
             std::string_view("$global"),
             std::string_view("${global}"),
             std::string_view("%global%"),
         }) {
        if (auto p = resolve_for(global_root, t))
            return p;
    }

    return std::nullopt;
}

static bool ends_with_ci(const std::string& s, const char* suffix) {
    size_t sl = 0;
    while (suffix[sl])
        ++sl;
    if (s.size() < sl)
        return false;
    for (size_t i = 0; i < sl; ++i) {
        const unsigned char a = static_cast<unsigned char>(s[s.size() - sl + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

/// `coll.jpg` in the agent cwd / folder — no slash, still a resolvable local path for tools + Replicate.
static bool bare_media_filename_no_separators(const std::string& s) {
    if (s.empty())
        return false;
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
        return false;
    if (s.size() >= 2 && s[1] == ':')
        return false;
    const size_t dot = s.rfind('.');
    if (dot == std::string::npos || dot == 0)
        return false;
    static const char* suf[] = {".jpg",  ".jpeg", ".png",  ".webp", ".gif", ".bmp", ".tif", ".tiff",
                                ".heic", ".avif", ".jxl",  ".mp4",  ".webm", ".mov", ".m4v"};
    for (const char* e : suf) {
        if (ends_with_ci(s, e))
            return true;
    }
    return false;
}

/// Same idea as transform.cpp `string_might_be_local_file_path`: do not treat
/// short enum tokens (e.g. "16:9", "1080p") or prose prompts as relative paths.
static bool tool_string_looks_like_path_for_resolve(const std::string& s) {
    if (s.empty() || s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 || s.rfind("data:", 0) == 0)
        return false;
    if (s.size() >= 2 && s[1] == ':')
        return true;
    if (!s.empty() && (s[0] == '/' || s[0] == '\\'))
        return true;
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
        return true;
    return bare_media_filename_no_separators(s);
}

static void resolve_tool_paths_strings_in_json(nlohmann::json& j, const std::string* parent_obj_key = nullptr) {
    if (j.is_string()) {
        if (parent_obj_key != nullptr) {
            const std::string& k = *parent_obj_key;
            if (k == "prompt" || k == "negative_prompt")
                return;
        }
        std::string s = j.get<std::string>();
        if (s.empty() || s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 || s.rfind("data:", 0) == 0)
            return;
        if (!tool_string_looks_like_path_for_resolve(s))
            return;
        j = resolve_tool_path_string(s);
    } else if (j.is_array()) {
        for (auto& el : j)
            resolve_tool_paths_strings_in_json(el, nullptr);
    } else if (j.is_object()) {
        for (auto& kv : j.items()) {
            const std::string keyCopy = kv.key();
            resolve_tool_paths_strings_in_json(kv.value(), &keyCopy);
        }
    }
}

/// Reuse on-disk <stem>.json (preferred) or <stem>.md so the agent does not
/// re-call Gemini. We do not require *both* files: one valid JSON is enough
/// to populate `result`. See `options.regenerate` in image_meta.
bool try_image_meta_from_sidecar(const std::string&    in_abs,
                                 const media::MetaOptions& opt,
                                 const nlohmann::json&  tool_options,
                                 PerFileResult&           r)
{
    if (opt.dry_run) return false;
    if (opt.update_exif) return false;  // needs a fresh model payload to write EXIF
    if (tool_options.value("regenerate", false)) return false;

    if (!opt.out_md && !opt.out_json) return false;

    const fs::path    src  = in_abs;
    const fs::path    dir  = opt.out_dir.empty() ? src.parent_path() : fs::path(opt.out_dir);
    const std::string stem = src.stem().string();
    const fs::path    md_p   = dir / (stem + ".md");
    const fs::path    json_p = dir / (stem + ".json");

    auto nonempty = [](const fs::path& p) {
        std::error_code ec;
        if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return false;
        return fs::file_size(p, ec) > 0;
    };

    // 1) Prefer a valid <stem>.json when present (ignore out_json for *read* — the
    //    model may only set out_md: true in the tool call; out_json can be absent/false
    //    but a previous run still left a .json on disk).
    if (nonempty(json_p)) {
        std::ifstream      ifs(json_p, std::ios::binary);
        if (ifs) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            nlohmann::json parsed = nlohmann::json::parse(ss.str(), nullptr, false);
            if (!parsed.is_discarded()) {
                parsed["_from_sidecar_cache"] = true;
                r.result        = std::move(parsed);
                r.ok            = true;
                r.output_path   = json_p.string();
                return true;
            }
        }
    }

    // 2) Markdown only (no/consumed json) — e.g. model set out_json false, or .json missing/corrupt
    if (opt.out_md && nonempty(md_p)) {
        std::ifstream      ifs(md_p, std::ios::binary);
        if (!ifs) return false;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        r.result = nlohmann::json{
            {"markdown",          ss.str()},
            {"_from_sidecar_cache", true},
        };
        r.ok          = true;
        r.output_path = md_p.string();
        return true;
    }

    return false;
}

/**
 * Resolve a user/LLM-supplied path: relative paths become absolute against
 * `g_agent_path_base` when the agent set it (Explorer folder or `--folder`), else
 * the process CWD. `.\` / `..` collapse via lexically_normal; when the path
 * exists, weakly_canonical applies.
 *
 * When a folder base is pinned, the model may still pass a path relative to CWD
 * (e.g. `./tests/assets` while base is already that directory). If `(base / rel)`
 * does not exist but `(cwd / rel)` does, use the cwd resolution so paths are not
 * doubled.
 *
 * Glob characters `*` / `?` are preserved: we only absolutise + lexically normalise
 * the pattern string (weakly_canonical would refuse a non-existent `*.png` path).
 */
std::string resolve_tool_path_string(const std::string& raw) {
    if (raw.empty()) return raw;
    const bool has_glob =
        raw.find('*') != std::string::npos || raw.find('?') != std::string::npos;
    std::error_code ec_cwd;
    const fs::path cwd = fs::current_path(ec_cwd);
    if (ec_cwd) return raw;

    const fs::path base_eff = g_agent_path_base.empty() ? cwd : fs::path(g_agent_path_base);
    const fs::path global_root = media::settings::get_config_dir();
    if (const auto aliased = try_resolve_known_path_alias(raw, cwd, base_eff, global_root); aliased.has_value()) {
        std::error_code ec_alias;
        const fs::path p = aliased->lexically_normal();
        if (has_glob) return p.string();
        const fs::path c = fs::weakly_canonical(p, ec_alias);
        if (!ec_alias) return c.string();
        return p.string();
    }

    std::error_code ec;
    fs::path p_in(raw);
    if (p_in.is_absolute()) {
        fs::path p = p_in.lexically_normal();
        if (has_glob) return p.string();
        const fs::path c = fs::weakly_canonical(p, ec);
        if (!ec) return c.string();
        return p.string();
    }

    const fs::path rel = p_in.lexically_normal();
    fs::path p = (base_eff / rel).lexically_normal();

    if (!g_agent_path_base.empty()) {
        const fs::path p_cwd = (cwd / rel).lexically_normal();
        if (p_cwd != p) {
            std::error_code e_base, e_cwd;
            const bool ok_base = fs::exists(p, e_base);
            const bool ok_cwd = fs::exists(p_cwd, e_cwd);
            if (ok_cwd && !ok_base)
                p = p_cwd;
        }
    }

    if (has_glob) return p.string();
    const fs::path c = fs::weakly_canonical(p, ec);
    if (!ec) return c.string();
    return p.string();
}

// Build a sibling path: "<stem><suffix>.<ext>" inside out_dir (or beside source).
std::string make_sibling_path(const std::string& input,
                              const std::string& out_dir,
                              const std::string& suffix,
                              const std::string& new_ext_with_dot) {
    fs::path p(input);
    std::string stem = p.stem().string();
    if (!suffix.empty()) stem += suffix;
    const std::string ext = new_ext_with_dot.empty() ? p.extension().string() : new_ext_with_dot;
    const std::string filename = stem + ext;
    fs::path dir = out_dir.empty() ? p.parent_path() : fs::path(out_dir);
    if (!out_dir.empty()) {
        std::error_code ec;
        fs::create_directories(dir, ec); // best-effort; worker will surface I/O errors
    }
    return (dir / filename).string();
}

ExecuteResult build_envelope(const std::string& tool, std::vector<PerFileResult> per_file) {
    ExecuteResult r;
    r.ok = true;
    int succeeded = 0, failed = 0;
    nlohmann::json results = nlohmann::json::array();
    for (const auto& f : per_file) {
        nlohmann::json e;
        e["path"] = f.path;
        e["ok"]   = f.ok;
        if (!f.ok) {
            e["error"] = f.error;
            if (!f.result.is_null() && !f.result.empty())
                e["result"] = f.result;
            ++failed;
        } else {
            ++succeeded;
            if (!f.output_path.empty()) e["output_path"] = f.output_path;
            if (!f.result.is_null())    e["result"]      = f.result;
        }
        results.push_back(std::move(e));
    }
    r.envelope = nlohmann::json{
        {"ok",      true},
        {"tool",    tool},
        {"summary", {
            {"total",     (int)per_file.size()},
            {"succeeded", succeeded},
            {"failed",    failed},
        }},
        {"results", std::move(results)},
    };
    r.per_file = std::move(per_file);
    return r;
}

ExecuteResult fatal(const std::string& msg) {
    ExecuteResult r;
    r.ok = false;
    r.error = msg;
    r.envelope = nlohmann::json{{"ok", false}, {"error", msg}};
    return r;
}

// ── Per-tool dispatchers ───────────────────────────────────────────────────

ExecuteResult do_resize(const nlohmann::json& args) {
    auto paths = get_string_array(args, "paths");
    if (paths.empty()) return fatal("image_resize: 'paths' (string array) is required and non-empty");

    media::ResizeOptions opt;
    media::apply_resize_options_from_json(options_or_empty(args), opt);
    opt.cache_enabled = false; // chat-mode operates on user files directly

    const std::string out_dir = options_or_empty(args).value("out_dir", std::string{});

    std::vector<PerFileResult> out;
    out.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) {
            for (size_t j = i; j < paths.size(); ++j) {
                PerFileResult r2;
                r2.path  = resolve_tool_path_string(paths[j]);
                r2.ok    = false;
                r2.error = "interrupted (Ctrl+C)";
                out.push_back(std::move(r2));
            }
            return build_envelope("image_resize", std::move(out));
        }
        const std::string& in  = paths[i];
        const std::string in_abs = resolve_tool_path_string(in);
        PerFileResult r; r.path = in_abs;
        if (const std::string wIn = llm_path_fs_deny(in_abs); !wIn.empty()) {
            r.ok = false;
            r.error = wIn;
            out.push_back(std::move(r));
            continue;
        }
        std::string ext = opt.format.empty() ? fs::path(in_abs).extension().string()
                                             : "." + (opt.format == "jpeg" ? std::string("jpg") : opt.format);
        if (ext.empty()) ext = ".jpg";
        const std::string out_path = make_sibling_path(in_abs, out_dir, "_resized", ext);
        if (const std::string wOut = llm_path_fs_deny(out_path); !wOut.empty()) {
            r.ok = false;
            r.error = wOut;
            out.push_back(std::move(r));
            continue;
        }
        if (const std::string wW = llm_path_write_deny(out_path); !wW.empty()) {
            r.ok = false;
            r.error = wW;
            out.push_back(std::move(r));
            continue;
        }
        std::string err;
        if (media::resize_file(in_abs, out_path, opt, err)) {
            r.ok = true;
            r.output_path = out_path;
        } else {
            r.error = err.empty() ? "resize_file failed" : err;
        }
        out.push_back(std::move(r));
    }
    return build_envelope("image_resize", std::move(out));
}

ExecuteResult do_compress(const nlohmann::json& args) {
    auto paths = get_string_array(args, "paths");
    if (paths.empty()) return fatal("image_compress: 'paths' (string array) is required and non-empty");

    media::CompressOptions opt;
    media::apply_compress_options_from_json(options_or_empty(args), opt);
    const std::string out_dir = options_or_empty(args).value("out_dir", std::string{});

    std::vector<PerFileResult> out;
    out.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) {
            for (size_t j = i; j < paths.size(); ++j) {
                PerFileResult r2;
                r2.path  = resolve_tool_path_string(paths[j]);
                r2.ok    = false;
                r2.error = "interrupted (Ctrl+C)";
                out.push_back(std::move(r2));
            }
            return build_envelope("image_compress", std::move(out));
        }
        const std::string& in  = paths[i];
        const std::string in_abs = resolve_tool_path_string(in);
        PerFileResult r; r.path = in_abs;
        if (const std::string wIn = llm_path_fs_deny(in_abs); !wIn.empty()) {
            r.ok = false;
            r.error = wIn;
            out.push_back(std::move(r));
            continue;
        }
        const std::string out_path = media::compress_default_output(in_abs, out_dir, opt);
        if (const std::string wOut = llm_path_fs_deny(out_path); !wOut.empty()) {
            r.ok = false;
            r.error = wOut;
            out.push_back(std::move(r));
            continue;
        }
        if (const std::string wW = llm_path_write_deny(out_path); !wW.empty()) {
            r.ok = false;
            r.error = wW;
            out.push_back(std::move(r));
            continue;
        }
        const std::string err = media::compress_file(in_abs, out_path, opt);
        if (err.empty()) {
            r.ok = true;
            r.output_path = out_path;
        } else {
            r.error = err;
        }
        out.push_back(std::move(r));
    }
    return build_envelope("image_compress", std::move(out));
}

ExecuteResult do_transform(const nlohmann::json& args) {
    auto paths = get_string_array(args, "paths");
    if (paths.empty()) return fatal("image_transform: 'paths' (string array) is required and non-empty");

    const nlohmann::json& tool_options = options_or_empty(args);
    media::TransformOptions opt;
    media::apply_transform_options_from_json(tool_options, opt);
    opt.provider.clear();
    opt.model.clear();
    opt.api_key.clear();
    opt.base_url.clear();
    apply_chat_image_defaults_for_tool_options(opt.provider, opt.model, nlohmann::json::object());
    ensure_api_key_for_image_provider(opt.provider, opt.api_key);
    ensure_base_url_for_image_provider(opt.provider, opt.base_url);
    logger::info(std::string("image_transform: resolved provider=") + opt.provider + " model=" + opt.model
                 + " (see `llm info` image_tools_resolved; settings.json reloads on file mtime change)");
    if (opt.prompt.empty()) return fatal("image_transform: options.prompt is required");
    if (opt.provider.empty() || opt.model.empty()) {
        return fatal("image_transform: missing provider/model: set Chat -> Image provider/model in Settings, "
                     "or pass explicit tool options.provider/options.model");
    }

    const std::string out_dir = options_or_empty(args).value("out_dir", std::string{});
    const std::optional<std::string> explicit_output_path = get_output_path_argument(args);
    const std::vector<std::string> explicit_output_paths = get_string_array(args, "output_paths");
    if (explicit_output_path.has_value() && !explicit_output_paths.empty()) {
        return fatal("image_transform: pass either output_path or output_paths, not both");
    }
    if (explicit_output_path.has_value() && paths.size() != 1) {
        return fatal("image_transform: output_path is only valid with a single input; use output_paths for multiple inputs");
    }
    if (!explicit_output_paths.empty()) {
        if (explicit_output_paths.size() != paths.size()) {
            return fatal("image_transform: output_paths length must match paths length");
        }
        for (const auto& p : explicit_output_paths) {
            if (p.empty())
                return fatal("image_transform: output_paths entries must be non-empty strings");
        }
    }

    std::vector<PerFileResult> out;
    out.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) {
            for (size_t j = i; j < paths.size(); ++j) {
                PerFileResult r2;
                r2.path  = resolve_tool_path_string(paths[j]);
                r2.ok    = false;
                r2.error = "interrupted (Ctrl+C)";
                out.push_back(std::move(r2));
            }
            return build_envelope("image_transform", std::move(out));
        }
        const std::string& in  = paths[i];
        const std::string in_abs = resolve_tool_path_string(in);
        PerFileResult r; r.path = in_abs;
        if (const std::string wIn = llm_path_fs_deny(in_abs); !wIn.empty()) {
            r.ok = false;
            r.error = wIn;
            out.push_back(std::move(r));
            continue;
        }
        std::string out_path;
        if (explicit_output_path.has_value()) {
            out_path = resolve_tool_path_string(*explicit_output_path);
        } else if (!explicit_output_paths.empty()) {
            out_path = resolve_tool_path_string(explicit_output_paths[i]);
        } else {
            out_path = media::default_transform_output(in_abs, opt.prompt);
        }
        if (!explicit_output_path.has_value() && explicit_output_paths.empty() && !out_dir.empty()) {
            fs::path dir(out_dir);
            std::error_code ec; fs::create_directories(dir, ec);
            out_path = (dir / fs::path(out_path).filename()).string();
        }
        if (const std::string wOut = llm_path_fs_deny(out_path); !wOut.empty()) {
            r.ok = false;
            r.error = wOut;
            out.push_back(std::move(r));
            continue;
        }
        if (const std::string wW = llm_path_write_deny(out_path); !wW.empty()) {
            r.ok = false;
            r.error = wW;
            out.push_back(std::move(r));
            continue;
        }
        auto tr = media::transform_image(in_abs, out_path, opt);
        if (tr.ok) {
            r.ok = true;
            r.output_path = tr.output_path.empty() ? out_path : tr.output_path;
            if (!tr.ai_text.empty()) r.result = nlohmann::json{{"ai_text", tr.ai_text}};
        } else {
            r.error = tr.error;
        }
        if (g_image_transform_file_progress) {
            std::vector<PerFileResult> one;
            one.push_back(r);
            g_image_transform_file_progress(
                build_envelope("image_transform", std::move(one)).envelope);
        }
        out.push_back(std::move(r));
    }
    return build_envelope("image_transform", std::move(out));
}

ExecuteResult do_create_image(const nlohmann::json& args) {
    const nlohmann::json& tool_options = options_or_empty(args);
    media::TransformOptions opt;
    media::apply_transform_options_from_json(tool_options, opt);
    /* Provider / model / keys from Chat \u2192 Image settings, not LLM tool JSON (create_video uses Chat \u2192 Video). */
    opt.provider.clear();
    opt.model.clear();
    opt.api_key.clear();
    opt.base_url.clear();
    nlohmann::json defaults_src = tool_options;
    defaults_src.erase("provider");
    defaults_src.erase("model");
    defaults_src.erase("api_key");
    apply_chat_image_defaults_for_tool_options(opt.provider, opt.model, defaults_src);
    media::runtime_settings::merge_provider_credentials(
        opt.provider, false, opt.api_key, opt.base_url);
    ensure_api_key_for_image_provider(opt.provider, opt.api_key);
    ensure_base_url_for_image_provider(opt.provider, opt.base_url);
    if (opt.prompt.empty()) return fatal("image_create: options.prompt is required");
    if (opt.provider.empty() || opt.model.empty()) {
        return fatal("image_create: no provider/model configured: set Chat -> Image provider/model in Settings");
    }

    media::cli::yield_to_interrupt();
    if (media::cli::cancel_requested()) return fatal("image_create: interrupted (Ctrl+C)");

    std::string out_path;
    const std::optional<std::string> explicit_output_path = get_output_path_argument(args);
    if (explicit_output_path.has_value()) {
        out_path = resolve_tool_path_string(*explicit_output_path);
    }
    if (out_path.empty()) out_path = media::default_create_image_output(opt.prompt);

    const std::string out_dir = tool_options.value("out_dir", std::string{});
    if (!explicit_output_path.has_value() && !out_dir.empty()) {
        fs::path dir(out_dir);
        std::error_code ec; fs::create_directories(dir, ec);
        out_path = (dir / fs::path(out_path).filename()).string();
    }

    if (const std::string wOut = llm_path_fs_deny(out_path); !wOut.empty()) {
        PerFileResult r;
        r.path = out_path;
        r.ok = false;
        r.error = wOut;
        std::vector<PerFileResult> out;
        out.push_back(std::move(r));
        return build_envelope("image_create", std::move(out));
    }
    if (const std::string wW = llm_path_write_deny(out_path); !wW.empty()) {
        PerFileResult r;
        r.path = out_path;
        r.ok = false;
        r.error = wW;
        std::vector<PerFileResult> out;
        out.push_back(std::move(r));
        return build_envelope("image_create", std::move(out));
    }

    const std::string rpath = out_path;
    auto tr = media::create_image(out_path, opt);
    std::vector<PerFileResult> out;
    PerFileResult r;
    r.path = rpath;
    if (tr.ok) {
        r.ok = true;
        r.output_path = tr.output_path.empty() ? rpath : tr.output_path;
        if (!tr.ai_text.empty()) r.result = nlohmann::json{{"ai_text", tr.ai_text}};
    } else {
        r.error = tr.error;
    }
    out.push_back(std::move(r));
    return build_envelope("image_create", std::move(out));
}

ExecuteResult do_create_video(const nlohmann::json& args, const std::string& tool) {
    const std::string pfx = tool + ": ";

    media::TransformOptions opt;
    media::apply_transform_options_from_json(nlohmann::json::object(), opt);
    opt.provider.clear();
    opt.model.clear();
    opt.api_key.clear();
    opt.base_url.clear();
    nlohmann::json defaults_src = nlohmann::json::object();
    apply_chat_video_defaults_for_tool_options(opt.provider, opt.model, defaults_src);
    auto replicate_slug_ok = [](const std::string& m) -> bool {
        return !m.empty() && m.find('/') != std::string::npos;
    };
    if (!replicate_slug_ok(opt.model))
        opt.model.clear();
    media::runtime_settings::merge_provider_credentials(
        opt.provider, false, opt.api_key, opt.base_url);
    ensure_api_key_for_image_provider(opt.provider, opt.api_key);
    ensure_base_url_for_image_provider(opt.provider, opt.base_url);
    if (opt.provider.empty() || opt.model.empty()) {
        return fatal(pfx + "no provider/model configured: set Chat -> Video provider/model in Settings");
    }

    nlohmann::json flat;
    nlohmann::json required_keys;
    std::string    openapi_err;
    if (!media::replicate_cli::lookup_replicate_openapi_input_flat(opt.model, flat, required_keys, openapi_err))
        return fatal(pfx + "OpenAPI Input not available for model `" + opt.model + "`: " + openapi_err);

    nlohmann::json input = nlohmann::json::object();
    for (auto it = flat.begin(); it != flat.end(); ++it) {
        const std::string& k = it.key();
        if (!args.contains(k))
            continue;
        if (args[k].is_null())
            continue;
        input[k] = args[k];
    }

    for (const auto& r : required_keys) {
        if (!r.is_string())
            continue;
        const std::string rk = r.get<std::string>();
        if (!input.contains(rk))
            return fatal(pfx + "missing required OpenAPI Input field `" + rk + "`");
    }

    if (input.contains("prompt") && input["prompt"].is_string())
        opt.prompt = input["prompt"].get<std::string>();
    if (opt.prompt.empty())
        return fatal(pfx + "`prompt` must be a non-empty string in the tool arguments (Replicate Input)");

    resolve_tool_paths_strings_in_json(input);

    media::cli::yield_to_interrupt();
    if (media::cli::cancel_requested()) return fatal(pfx + "interrupted (Ctrl+C)");

    std::string out_path;
    if (args.contains("output") && args["output"].is_string()) {
        const std::string raw = args["output"].get<std::string>();
        if (!raw.empty()) out_path = resolve_tool_path_string(raw);
    }

    if (const std::string wOut = llm_path_fs_deny(out_path); !out_path.empty() && !wOut.empty()) {
        PerFileResult r;
        r.path = out_path;
        r.ok = false;
        r.error = wOut;
        std::vector<PerFileResult> one;
        one.push_back(std::move(r));
        return build_envelope(tool, std::move(one));
    }
    if (const std::string wW = llm_path_write_deny(out_path); !out_path.empty() && !wW.empty()) {
        PerFileResult r;
        r.path = out_path;
        r.ok = false;
        r.error = wW;
        std::vector<PerFileResult> one;
        one.push_back(std::move(r));
        return build_envelope(tool, std::move(one));
    }

    const std::string intended_path = media::replicate_video_intended_output_path(out_path, input, opt.prompt);
    nlohmann::json      meta          = nlohmann::json::object();
    meta["intended_output_path"]     = intended_path;
    meta["note"] =
        "If Replicate returns a different video container, the written file may use another suffix than a `.png` "
        "placeholder; on success `output_path` is authoritative.";

    const std::string          rpath = out_path;
    const media::TransformResult tr =
        media::replicate_video_replicate_input(std::move(input), out_path, opt, nullptr);
    std::vector<PerFileResult> out;
    PerFileResult              r;
    r.path = intended_path;
    if (tr.ok) {
        r.ok          = true;
        r.output_path = tr.output_path.empty() ? rpath : tr.output_path;
        r.path        = r.output_path;
        meta["output_path"] = r.output_path;
        if (!tr.ai_text.empty()) meta["ai_text"] = tr.ai_text;
        if (!tr.replicate_prediction_web_url.empty())
            meta["replicate_web_url"] = tr.replicate_prediction_web_url;
        r.result = std::move(meta);
    } else {
        r.error  = tr.error;
        if (!tr.replicate_prediction_web_url.empty())
            meta["replicate_web_url"] = tr.replicate_prediction_web_url;
        r.result = std::move(meta);
    }
    out.push_back(std::move(r));
    return build_envelope(tool, std::move(out));
}

ExecuteResult do_meta(const nlohmann::json& args) {
    auto paths = get_string_array(args, "paths");
    if (paths.empty()) return fatal("image_meta: 'paths' (string array) is required and non-empty");

    const nlohmann::json& tool_options = options_or_empty(args);
    media::MetaOptions     opt;
    media::apply_meta_options_from_json(tool_options, opt);
    opt.provider.clear();
    opt.model.clear();
    opt.api_key.clear();
    opt.base_url.clear();
    apply_chat_image_recognition_defaults_for_tool_options(opt.provider, opt.model, nlohmann::json::object());
    repair_meta_google_model_if_replicate_slug(opt.provider, opt.model);
    if (opt.provider.empty() || opt.model.empty()) {
        return fatal("image_meta: missing provider/model: set Chat -> Image recognition provider/model in Settings, "
                     "or pass explicit tool options.provider/options.model");
    }

    std::vector<PerFileResult> out;
    out.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) {
            for (size_t j = i; j < paths.size(); ++j) {
                PerFileResult r2;
                r2.path  = resolve_tool_path_string(paths[j]);
                r2.ok    = false;
                r2.error = "interrupted (Ctrl+C)";
                out.push_back(std::move(r2));
            }
            return build_envelope("image_meta", std::move(out));
        }
        const std::string&  in     = paths[i];
        const std::string   in_abs = resolve_tool_path_string(in);
        PerFileResult       r;
        r.path = in_abs;
        if (const std::string wIn = llm_path_fs_deny(in_abs); !wIn.empty()) {
            r.ok = false;
            r.error = wIn;
            out.push_back(std::move(r));
            continue;
        }

        if (try_image_meta_from_sidecar(in_abs, opt, tool_options, r)) {
            out.push_back(std::move(r));
            continue;
        }

        if (!opt.dry_run) {
            ensure_api_key_for_image_provider(opt.provider, opt.api_key);
            ensure_base_url_for_image_provider(opt.provider, opt.base_url);
        }
        auto mr = media::meta_extract(in_abs, opt);
        if (mr.ok) {
            r.ok = true;
            // The "primary" output is whichever sidecar(s) were written.
            if (!mr.json_path.empty())     r.output_path = mr.json_path;
            else if (!mr.md_path.empty())  r.output_path = mr.md_path;
            // Always surface the parsed JSON metadata in `result`.
            auto parsed = nlohmann::json::parse(mr.json_text, nullptr, /*allow_exceptions*/ false);
            if (!parsed.is_discarded()) {
                r.result = std::move(parsed);
            } else if (!mr.json_text.empty()) {
                r.result = nlohmann::json{{"meta_text", mr.json_text}};
            }
        } else {
            r.error = mr.error;
        }
        out.push_back(std::move(r));
    }
    return build_envelope("image_meta", std::move(out));
}

// ── image_understand — pure vision query (replaces image_meta as agent tool) ─
// Sends one or more images to the recognition model with a caller-supplied prompt.
// No sidecar writes, no caching — result is the model's verbatim text answer.
// Provider / model always come from Chat settings (image_recognition_provider /
// image_recognition_model); they are not agent-overridable.
ExecuteResult do_understand(const nlohmann::json& args) {
    auto paths = get_string_array(args, "paths");
    if (paths.empty())
        return fatal("image_understand: 'paths' (string array) is required and non-empty");

    const std::string prompt = args.contains("prompt") && args["prompt"].is_string()
                               ? args["prompt"].get<std::string>() : std::string{};
    if (prompt.empty())
        return fatal("image_understand: 'prompt' (string) is required");

    const nlohmann::json& tool_options = options_or_empty(args);

    // Provider always from Chat recognition settings — not agent-overridable
    media::MetaOptions opt;
    apply_chat_image_recognition_defaults_for_tool_options(opt.provider, opt.model,
                                                           nlohmann::json::object());
    repair_meta_google_model_if_replicate_slug(opt.provider, opt.model);
    if (opt.provider.empty() || opt.model.empty())
        return fatal("image_understand: no recognition provider configured — "
                     "set Chat \u2192 Image recognition provider/model in Settings");

    // Pure query: suppress all sidecar and EXIF side-effects.
    // Sidecar cache bypass is implicit — do_understand never calls
    // try_image_meta_from_sidecar, so meta_extract always runs.
    opt.out_md      = false;
    opt.out_json    = false;
    opt.update_exif = false;
    opt.prompt      = prompt; // agent-composed question replaces default cataloguer prompt

    // Resize options (default: resize to 512 px wide to reduce cost / latency)
    opt.resize_first = tool_options.value("resize_first", true);
    opt.resize_width = tool_options.value("resize_width", 512);

    if (!opt.dry_run) {
        ensure_api_key_for_image_provider(opt.provider, opt.api_key);
        ensure_base_url_for_image_provider(opt.provider, opt.base_url);
    }

    std::vector<PerFileResult> out;
    out.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) {
            for (size_t j = i; j < paths.size(); ++j) {
                PerFileResult r2;
                r2.path  = resolve_tool_path_string(paths[j]);
                r2.ok    = false;
                r2.error = "interrupted (Ctrl+C)";
                out.push_back(std::move(r2));
            }
            return build_envelope("image_understand", std::move(out));
        }
        const std::string in_abs = resolve_tool_path_string(paths[i]);
        PerFileResult r;
        r.path = in_abs;
        if (const std::string w = llm_path_fs_deny(in_abs); !w.empty()) {
            r.ok    = false;
            r.error = w;
            out.push_back(std::move(r));
            continue;
        }
        auto mr = media::meta_extract(in_abs, opt);
        r.ok = mr.ok;
        if (mr.ok) {
            // Return the model's response plus the execution provider/model so the
            // host (ChatWebPanel) can surface them in the chat entry metadata.
            nlohmann::json res = nlohmann::json::object();
            if (!mr.markdown.empty()) res["answer"] = mr.markdown;
            res["provider"] = opt.provider;  // e.g. "replicate"
            res["model"]    = opt.model;      // e.g. "g/google/gemini-2.5-flash-image"
            auto parsed = nlohmann::json::parse(mr.json_text, nullptr, /*allow_exceptions*/ false);
            if (!parsed.is_discarded()) res["result"] = std::move(parsed);
            else if (!mr.json_text.empty()) res["result_raw"] = mr.json_text;
            r.result = std::move(res);
        } else {
            r.error = mr.error;
        }
        out.push_back(std::move(r));
    }
    return build_envelope("image_understand", std::move(out));
}

// ── image_from_camera helpers ─────────────────────────────────────────────

// Return the first non-colliding path: tries `base`, then `stem_1.ext`, `stem_2.ext`, …
static std::string make_unique_camera_path(const std::string& base_path) {
    std::error_code ec;
    if (!fs::exists(fs::path(base_path), ec)) return base_path;
    const fs::path p(base_path);
    const std::string stem = p.stem().string();
    const std::string ext  = p.extension().string();
    const fs::path    dir  = p.parent_path();
    for (int n = 1; n < 10000; ++n) {
        fs::path candidate = dir / (stem + "_" + std::to_string(n) + ext);
        if (!fs::exists(candidate, ec)) return candidate.string();
    }
    return base_path; // give up after 10k; caller handles the overwrite
}

// ── image_from_camera — webcam still capture ──────────────────────────────
// action="list"    → enumerate devices + their unique (w×h) modes (sorted by area desc).
// action="capture" → grab one still frame and save to context folder or explicit path.
// Requires FEATURE_VIDEO (Win32: Media Foundation + WIC; Linux: V4L2 + libjpeg).
ExecuteResult do_camera(const nlohmann::json& args) {
#if !defined(FEATURE_VIDEO) || !FEATURE_VIDEO
    (void)args;
    return fatal("image_from_camera: webcam capture requires FEATURE_VIDEO "
                 "(not compiled into this build)");
#else
    // Media Foundation is a COM-based API. Ensure COM is initialized on this thread
    // before any MF or device enumeration call (the capture thread initialises its
    // own apartment; the calling tool-executor thread may not have done so).
#if defined(_WIN32)
    const HRESULT hr_com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComGuard {
        bool owned;
        ~ComGuard() { if (owned) ::CoUninitialize(); }
    } com_guard{ hr_com == S_OK || hr_com == S_FALSE };
    // RPC_E_CHANGED_MODE means COM is already initialised in a different mode — fine.
    if (FAILED(hr_com) && hr_com != RPC_E_CHANGED_MODE)
        return fatal("image_from_camera: CoInitializeEx failed");
#endif

    const std::string action = args.value("action", std::string("capture"));

    if (action == "list") {
        auto devices = pm::video::enumerate_capture_devices();
        nlohmann::json j_devs = nlohmann::json::array();
        for (const auto& d : devices) {
            auto raw_modes = pm::video::enumerate_device_modes(d.name);
            // Deduplicate by (width, height): keep highest FPS per resolution.
            std::vector<pm::video::DeviceMode> unique_modes;
            for (const auto& m : raw_modes) {
                auto it = std::find_if(unique_modes.begin(), unique_modes.end(),
                    [&](const pm::video::DeviceMode& u) {
                        return u.width == m.width && u.height == m.height;
                    });
                if (it == unique_modes.end()) {
                    unique_modes.push_back(m);
                } else {
                    double cur_fps = it->fps_den > 0 ? (double)it->fps_num / it->fps_den : 0.0;
                    double new_fps = m.fps_den  > 0 ? (double)m.fps_num  / m.fps_den  : 0.0;
                    if (new_fps > cur_fps) *it = m;
                }
            }
            // Sort descending by pixel area.
            std::sort(unique_modes.begin(), unique_modes.end(),
                [](const pm::video::DeviceMode& a, const pm::video::DeviceMode& b) {
                    return (a.width * a.height) > (b.width * b.height);
                });
            nlohmann::json j_modes = nlohmann::json::array();
            for (const auto& m : unique_modes) {
                int fps_int = m.fps_den > 0 ? (int)((double)m.fps_num / m.fps_den + 0.5) : 0;
                j_modes.push_back({{"width", m.width}, {"height", m.height}, {"fps", fps_int}});
            }
            j_devs.push_back({
                {"name",       d.name},
                {"is_default", d.is_default},
                {"modes",      j_modes},
            });
        }
        PerFileResult r;
        r.ok     = true;
        r.path   = "(camera list)";
        r.result = {
            {"devices",      j_devs},
            {"context_folder", g_agent_path_base.empty() ? std::string("(none)") : g_agent_path_base},
        };
        return build_envelope("image_from_camera", {std::move(r)});
    }

    // action == "capture" (default)
    const std::string device_hint = args.value("device", std::string{});
    const int req_w = args.value("width",  0);
    const int req_h = args.value("height", 0);

    // ── Resolve and collision-proof the output path ───────────────────────────
    std::string out_path;
    const bool explicit_path = args.contains("output_path")
                               && args["output_path"].is_string()
                               && !args["output_path"].get<std::string>().empty();
    if (explicit_path) {
        // Agent supplied a name — honour it but avoid clobbering an existing file.
        out_path = make_unique_camera_path(
                       resolve_tool_path_string(args["output_path"].get<std::string>()));
    } else {
        // Agent left it to us: generate a timestamped filename.
        auto now = std::chrono::system_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()) % 1000;
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm lt{};
#if defined(_WIN32)
        ::localtime_s(&lt, &tt);
#else
        ::localtime_r(&tt, &lt);
#endif
        char ts_buf[32]; char ms_buf[16];
        std::strftime(ts_buf, sizeof(ts_buf), "cam_%Y%m%d_%H%M%S", &lt);
        std::snprintf(ms_buf, sizeof(ms_buf), "_%03d.jpg", static_cast<int>(ms.count()));
        const std::string fname = std::string(ts_buf) + ms_buf;
        const fs::path base_dir = g_agent_path_base.empty()
            ? [&]() -> fs::path {
                  std::error_code ec_tmp;
                  fs::path tmp = fs::temp_directory_path(ec_tmp);
                  if (ec_tmp) tmp = fs::current_path();
                  return tmp;
              }()
            : fs::path(g_agent_path_base);
        out_path = make_unique_camera_path((base_dir / fname).string());
    }
    if (const std::string w = llm_path_write_deny(out_path); !w.empty())
        return fatal("image_from_camera: " + w);

    // ── Capture ──────────────────────────────────────────────────────────────
    // capture_still throws std::runtime_error on failure/timeout.
    auto frame = pm::video::capture_still(device_hint, req_w, req_h, /*timeout_ms=*/8000);

    // ISO-8601 timestamp at the moment the frame was received.
    auto cap_now = std::chrono::system_clock::now();
    auto cap_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       cap_now.time_since_epoch()) % 1000;
    std::time_t cap_tt = std::chrono::system_clock::to_time_t(cap_now);
    std::tm cap_lt{};
#if defined(_WIN32)
    ::localtime_s(&cap_lt, &cap_tt);
#else
    ::localtime_r(&cap_tt, &cap_lt);
#endif
    char cap_ts[32]; char cap_ms_buf[16];
    std::strftime(cap_ts, sizeof(cap_ts), "%Y-%m-%dT%H:%M:%S", &cap_lt);
    std::snprintf(cap_ms_buf, sizeof(cap_ms_buf), ".%03d", static_cast<int>(cap_ms.count()));
    const std::string captured_at = std::string(cap_ts) + cap_ms_buf;

    // Ensure destination directory exists before writing.
    std::error_code ec_mk;
    fs::create_directories(fs::path(out_path).parent_path(), ec_mk);

    std::string save_err;
    if (!pm::video::save_frame(frame, out_path, save_err))
        return fatal("image_from_camera: save failed: " + save_err);

    PerFileResult r;
    r.ok          = true;
    r.path        = out_path;
    r.output_path = out_path;
    r.result = {
        {"captured_at",    captured_at},
        {"device",         device_hint.empty() ? std::string("(default)") : device_hint},
        {"width",          frame.width},
        {"height",         frame.height},
        {"context_folder", g_agent_path_base.empty() ? std::string("(none)") : g_agent_path_base},
    };
    return build_envelope("image_from_camera", {std::move(r)});
#endif // FEATURE_VIDEO
}

// ── list_images — read-only enumeration ────────────────────────────────────
// Walks every path in `inputs[]` (file, folder, or glob) and collects entries
// whose extension is in the standard image set. Mirrors pmui::is_image_ext
// from src/file_extensions.hpp but lives in pm-media so it
// stays Win32-free.
namespace {

bool is_standard_image_ext_lower(const std::string& ext_lower) {
    return ext_lower == ".jpg"  || ext_lower == ".jpeg" ||
           ext_lower == ".png"  || ext_lower == ".webp" ||
           ext_lower == ".tif"  || ext_lower == ".tiff" ||
           ext_lower == ".bmp"  || ext_lower == ".gif"  ||
           ext_lower == ".avif" || ext_lower == ".heic";
}
bool is_raw_ext_lower(const std::string& ext_lower) {
    return ext_lower == ".arw" || ext_lower == ".cr2" || ext_lower == ".cr3" ||
           ext_lower == ".nef" || ext_lower == ".nrw" || ext_lower == ".dng" ||
           ext_lower == ".orf" || ext_lower == ".rw2" || ext_lower == ".raf" ||
           ext_lower == ".pef" || ext_lower == ".srw" || ext_lower == ".x3f" ||
           ext_lower == ".3fr" || ext_lower == ".mef" || ext_lower == ".mrw";
}
std::string ext_of_lower(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

void file_glob_append_one_file(const fs::path& p, bool skip_dev, nlohmann::json& files,
                               int& skipped_dev, int& skipped_policy, int& skipped_ext) {
    std::error_code ec2;
    fs::path canon = fs::weakly_canonical(p, ec2);
    if (ec2 || canon.empty()) canon = fs::absolute(p, ec2);
    if (skip_dev && media::llm::glob_path_has_excluded_component(canon)) {
        ++skipped_dev;
        return;
    }
    if (const std::string w = llm_path_fs_deny(canon.string()); !w.empty()) {
        ++skipped_policy;
        return;
    }
    const std::string ext = ext_of_lower(canon);
    if (media::llm::glob_file_extension_blocked_like_file_read(ext)) {
        ++skipped_ext;
        return;
    }
    std::error_code ecs;
    const auto sz = fs::file_size(canon, ecs);
    nlohmann::json e;
    e["path"] = canon.generic_string();
    e["ext"]  = ext;
    e["size"] = ecs ? 0 : static_cast<std::uint64_t>(sz);
    files.push_back(std::move(e));
}

void collect_files_file_glob(const fs::path& root, bool recursive, int max_results, bool skip_dev,
                             nlohmann::json& files, int& skipped_dev, int& skipped_policy, int& skipped_ext) {
    std::error_code ec;
    const auto opts = fs::directory_options::skip_permission_denied;
    if (!recursive) {
        for (const auto& ent : fs::directory_iterator(root, opts, ec)) {
            media::cli::yield_to_interrupt();
            if (media::cli::cancel_requested()) return;
            if (!ent.is_regular_file(ec)) continue;
            if (static_cast<int>(files.size()) >= max_results) return;
            file_glob_append_one_file(ent.path(), skip_dev, files, skipped_dev, skipped_policy, skipped_ext);
        }
        return;
    }
    for (fs::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) return;
        if (ec) {
            ec.clear();
            continue;
        }
        const fs::directory_entry& ent = *it;
        if (ent.is_directory(ec)) {
            if (skip_dev && media::llm::glob_exclude_default_path_component(ent.path().filename().string()))
                it.disable_recursion_pending();
            continue;
        }
        if (!ent.is_regular_file(ec)) continue;
        if (static_cast<int>(files.size()) >= max_results) return;
        file_glob_append_one_file(ent.path(), skip_dev, files, skipped_dev, skipped_policy, skipped_ext);
    }
}

void collect_images_from(const fs::path& root, bool recursive, bool include_raw,
                         std::vector<fs::path>& out, std::size_t cap) {
    if (cap > 0 && out.size() >= cap) return;
    std::error_code ec;
    auto check = [&](const fs::path& p) {
        if (cap > 0 && out.size() >= cap) return;
        if (!llm_path_fs_deny(p.string()).empty()) return;
        const std::string e = ext_of_lower(p);
        if (is_standard_image_ext_lower(e) || (include_raw && is_raw_ext_lower(e))) {
            out.push_back(p);
        }
    };
    if (fs::is_regular_file(root, ec)) { check(root); return; }
    if (!fs::is_directory(root, ec)) return;
    if (recursive) {
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;
            check(it->path());
            if (cap > 0 && out.size() >= cap) return;
        }
    } else {
        for (auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            check(entry.path());
            if (cap > 0 && out.size() >= cap) return;
        }
    }
}

} // namespace

ExecuteResult do_list_images(const nlohmann::json& args) {
    auto inputs = get_string_array(args, "inputs");
    if (inputs.empty()) return fatal("list_images: 'inputs' (string array) is required and non-empty");

    {
        std::string resolved_inputs;
        for (const auto& spec : inputs) {
            if (!resolved_inputs.empty()) resolved_inputs += "; ";
            resolved_inputs += fs::path(resolve_tool_path_string(spec)).generic_string();
        }
        pm::log::info_lazy("llm_path_tools", [resolved_inputs]() -> std::string {
            return std::string("list_images inputs_resolved=") + resolved_inputs;
        });
    }

    const auto& opts = options_or_empty(args);
    const bool        recursive    = opts.value("recursive",    true);
    const bool        include_raw  = opts.value("include_raw",  true);
    const std::size_t max_results  = static_cast<std::size_t>(std::max(0, opts.value("max_results", 0)));

    std::vector<fs::path> hits;
    int scanned_inputs = 0;
    for (const auto& spec : inputs) {
        media::cli::yield_to_interrupt();
        if (media::cli::cancel_requested()) {
            nlohmann::json files = nlohmann::json::array();
            for (const auto& p : hits) {
                std::error_code ec2;
                const auto sz = fs::file_size(p, ec2);
                nlohmann::json e;
                e["path"] = p.string();
                e["ext"]  = ext_of_lower(p);
                e["size"] = ec2 ? 0 : static_cast<std::uint64_t>(sz);
                files.push_back(std::move(e));
            }
            PerFileResult agg;
            agg.path = "<aggregate>";
            agg.ok   = true;
            agg.result = nlohmann::json{
                {"count",         (int)files.size()},
                {"files",         std::move(files)},
                {"scanned_inputs",scanned_inputs},
                {"recursive",     recursive},
                {"include_raw",   include_raw},
                {"max_results",   max_results},
                {"interrupted",   "Ctrl+C (partial list)"},
            };
            std::vector<PerFileResult> pout; pout.push_back(std::move(agg));
            return build_envelope("list_images", std::move(pout));
        }
        ++scanned_inputs;
        // Resolve relative `.\foo` etc. against cwd so tool results are stable.
        const std::string spec_r = resolve_tool_path_string(spec);
        // Honour glob expansion via media::expand_input_paths when the spec
        // contains glob tokens; otherwise treat it as a literal path / folder.
        std::error_code ec;
        if (spec_r.find('*') != std::string::npos || spec_r.find('?') != std::string::npos) {
            std::string err;
            auto expanded = media::expand_input_paths(spec_r, err);
            for (const auto& s : expanded) {
                const std::string s_abs = resolve_tool_path_string(s);
                collect_images_from(fs::path(s_abs), recursive, include_raw, hits, max_results);
                if (max_results > 0 && hits.size() >= max_results) break;
            }
        } else {
            collect_images_from(fs::path(spec_r), recursive, include_raw, hits, max_results);
        }
        if (max_results > 0 && hits.size() >= max_results) break;
    }

    // Sort + de-dupe (recursive_iterator can revisit on case-insensitive fs).
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());

    nlohmann::json files = nlohmann::json::array();
    for (const auto& p : hits) {
        std::error_code ec;
        const auto sz = fs::file_size(p, ec);
        nlohmann::json e;
        e["path"] = p.string();
        e["ext"]  = ext_of_lower(p);
        e["size"] = ec ? 0 : static_cast<std::uint64_t>(sz);
        files.push_back(std::move(e));
    }

    PerFileResult agg;
    agg.path = "<aggregate>";
    agg.ok = true;
    agg.result = nlohmann::json{
        {"count",         (int)files.size()},
        {"files",         std::move(files)},
        {"scanned_inputs",scanned_inputs},
        {"recursive",     recursive},
        {"include_raw",   include_raw},
        {"max_results",   max_results},
    };
    std::vector<PerFileResult> out; out.push_back(std::move(agg));
    return build_envelope("list_images", std::move(out));
}

// file_glob — see llm_glob_filter.hpp for shared prune / extension rules.
ExecuteResult do_file_glob(const nlohmann::json& args) {
    if (!args.contains("pattern") || !args["pattern"].is_string())
        return fatal("file_glob: 'pattern' (string) is required");
    std::string pattern = args["pattern"].get<std::string>();
    while (!pattern.empty() && std::isspace(static_cast<unsigned char>(pattern.front())))
        pattern.erase(0, 1);
    while (!pattern.empty() && std::isspace(static_cast<unsigned char>(pattern.back())))
        pattern.pop_back();
    if (pattern.empty()) return fatal("file_glob: pattern is empty");

    const nlohmann::json& opts = options_or_empty(args);
    int max_results = opts.value("max_results", 500);
    if (max_results < 1) max_results = 1;
    if (max_results > 5000) max_results = 5000;
    const bool skip_dev        = opts.value("skip_dev_folders", true);
    const bool recursive_dir   = opts.value("recursive", true);

    const std::string resolved = resolve_tool_path_string(pattern);
    pm::log::info_lazy("llm_path_tools", [&pattern, &resolved]() -> std::string {
        return std::string("file_glob pattern_resolved: ") + pattern + " -> "
            + fs::path(resolved).generic_string();
    });
    const bool has_glob = media::has_glob_tokens(resolved) || resolved.find("**") != std::string::npos;

    int          skipped_dev = 0, skipped_policy = 0, skipped_ext = 0;
    nlohmann::json files     = nlohmann::json::array();

    if (!has_glob) {
        std::error_code ec;
        fs::path p = fs::path(resolved).lexically_normal();
        fs::path abs = fs::absolute(p, ec);
        if (ec) return fatal("file_glob: could not resolve path");
        fs::path canon = fs::weakly_canonical(abs, ec);
        if (!ec && !canon.empty()) abs = std::move(canon);
        if (const std::string w = llm_path_fs_deny(abs.string()); !w.empty())
            return fatal(std::string("file_glob: ") + w);
        if (fs::is_directory(abs, ec)) {
            collect_files_file_glob(abs, recursive_dir, max_results, skip_dev, files, skipped_dev, skipped_policy,
                                    skipped_ext);
        } else if (fs::is_regular_file(abs, ec)) {
            if (max_results > 0)
                file_glob_append_one_file(abs, skip_dev, files, skipped_dev, skipped_policy, skipped_ext);
        } else {
            return fatal("file_glob: not a file or directory: " + resolved);
        }
    } else {
        std::string err;
        const std::vector<std::string> expanded = media::expand_input_paths(resolved, err);
        if (!err.empty()) return fatal("file_glob: " + err);
        for (const auto& s : expanded) {
            media::cli::yield_to_interrupt();
            if (media::cli::cancel_requested()) break;
            if (static_cast<int>(files.size()) >= max_results) break;
            fs::path p(s);
            std::error_code ec;
            if (!fs::is_regular_file(p, ec)) continue;
            file_glob_append_one_file(p, skip_dev, files, skipped_dev, skipped_policy, skipped_ext);
        }
    }

    PerFileResult agg;
    agg.path = "<aggregate>";
    agg.ok   = true;
    agg.result = nlohmann::json{
        {"count",               static_cast<int>(files.size())},
        {"files",               std::move(files)},
        {"pattern_resolved",    resolved},
        {"max_results",         max_results},
        {"recursive",           recursive_dir},
        {"skip_dev_folders",    skip_dev},
        {"skipped_dev_folders", skipped_dev},
        {"skipped_policy",      skipped_policy},
        {"skipped_extension",   skipped_ext},
    };
    std::vector<PerFileResult> out;
    out.push_back(std::move(agg));
    return build_envelope("file_glob", std::move(out));
}

ExecuteResult do_find(const nlohmann::json& args) {
    auto inputs = get_string_array(args, "inputs");
    if (inputs.empty()) return fatal("image_find: 'inputs' (string array) is required and non-empty");
    for (auto& in : inputs)
        in = resolve_tool_path_string(in);
    for (const auto& in : inputs) {
        if (in.find('*') != std::string::npos || in.find('?') != std::string::npos) continue;
        if (const std::string w = llm_path_fs_deny(in); !w.empty())
            return fatal(std::string("image_find: ") + w);
    }

    const nlohmann::json& tool_options = options_or_empty(args);
    media::FindOptions     opt;
    media::apply_find_options_from_json(tool_options, opt);
    if (!tool_options.contains("mode") || tool_options["mode"].is_null()
        || (tool_options["mode"].is_string() && tool_options["mode"].get<std::string>().empty()))
        opt.mode = media::FindMode::Llm;
    // LLM catalog exposes only a subset of `FindOptions`; full flags stay for `find` CLI / UI.
    if (opt.mode == media::FindMode::Llm) {
        // `image_find` does not use the LLM text judge — local substring/word match on filename
        // + sidecars + EXIF only. Missing sidecar text may trigger `image_meta` once (Gemini[meta]).
        // `reference_images` in options still uses multimodal find:judge. CLI `find` is separate.
        opt.find_semantic_judge = false;
        // Not in schema: when there is no usable sidecar, run meta_extract once to
        // materialise .md/.json (same as CLI find). When sidecars exist, they are read — no meta HTTP.
        opt.generate = true;
        opt.bypass_cache = false;
        opt.meta.resize_first = true;
        opt.meta.resize_width  = 512;
    }
    opt.meta.provider.clear();
    opt.meta.model.clear();
    opt.meta.api_key.clear();
    opt.meta.base_url.clear();
    apply_chat_image_recognition_defaults_for_tool_options(
        opt.meta.provider, opt.meta.model, nlohmann::json::object());
    const bool prefill_find_provider = opt.mode == media::FindMode::Llm && !opt.dry_run
        && (opt.find_semantic_judge || !opt.reference_images.empty() || opt.generate);
    if (prefill_find_provider) {
        ensure_api_key_for_image_provider(opt.meta.provider, opt.meta.api_key);
        ensure_base_url_for_image_provider(opt.meta.provider, opt.meta.base_url);
    }

    auto fr = media::find_images(inputs, opt);
    if (!fr.ok) return fatal(std::string("image_find: ") + fr.error);

    nlohmann::json matches = nlohmann::json::array();
    for (const auto& m : fr.matches) {
        if (!llm_path_fs_deny(m.path).empty()) continue;
        matches.push_back({{"path", m.path}, {"score", m.score},
                           {"source", m.source}, {"reason", m.reason}});
    }

    // image_find is a single aggregate result, not per-file. Surface as a
    // single PerFileResult (path = "<aggregate>") so the envelope shape stays
    // uniform across the catalog.
    PerFileResult agg;
    agg.path = "<aggregate>";
    agg.ok = true;
    agg.result = nlohmann::json{
        {"matches",    std::move(matches)},
        {"scanned",    fr.scanned},
        {"considered", fr.considered},
        {"generated",  fr.generated},
        {"cache_hits", fr.cache_hits},
    };
    std::vector<PerFileResult> out; out.push_back(std::move(agg));
    return build_envelope("image_find", std::move(out));
}

// ── file_search ─────────────────────────────────────────────────────────────

ExecuteResult do_file_search(const nlohmann::json& args) {
    if (!args.contains("pattern") || !args["pattern"].is_string() || args["pattern"].get<std::string>().empty())
        return fatal("file_search: 'pattern' (string) is required and non-empty");

    // ── Resolve search root ──────────────────────────────────────────────────
    std::string root_raw;
    if (args.contains("path") && args["path"].is_string())
        root_raw = args["path"].get<std::string>();
    const std::string root = root_raw.empty()
        ? (g_agent_path_base.empty() ? std::string(".") : g_agent_path_base)
        : resolve_tool_path_string(root_raw);

    // Permission check on a non-glob root.
    if (root.find('*') == std::string::npos && root.find('?') == std::string::npos) {
        if (const std::string w = llm_path_fs_deny(root); !w.empty())
            return fatal(std::string("file_search: ") + w);
    }

    // ── Build SearchOptions ──────────────────────────────────────────────────
    media::SearchOptions sopts;
    sopts.grep         = true;               // always content search
    sopts.recursive    = true;
    sopts.case_sensitive = false;            // default: case-insensitive (like -i)
    sopts.head_limit   = 250;               // default like GrepTool DEFAULT_HEAD_LIMIT
    if (g_agent_godmode) sopts.exclude_sensitive = false;

    // Apply all recognised keys from args (pattern, -i, -B, -A, -C, type, glob, output_mode, …).
    media::apply_search_options_from_json(args, sopts);

    // grep=false means filename-only search; that is supported but unusual for this tool.
    // LLM sends grep=false explicitly when it wants name search.
    if (args.contains("grep") && args["grep"].is_boolean() && !args["grep"].get<bool>())
        sopts.grep = false;

    // Handle `glob` parameter: split on whitespace / comma, preserve brace patterns.
    if (args.contains("glob") && args["glob"].is_string()) {
        const std::string g = args["glob"].get<std::string>();
        // Split on whitespace.
        std::istringstream iss(g);
        std::string tok;
        while (iss >> tok) {
            if (tok.find('{') != std::string::npos && tok.find('}') != std::string::npos) {
                sopts.include_globs.push_back(tok);
            } else {
                // Split on commas.
                std::string part;
                for (char ch : tok + ",") {
                    if (ch == ',') {
                        if (!part.empty()) { sopts.include_globs.push_back(part); part.clear(); }
                    } else {
                        part += ch;
                    }
                }
            }
        }
    }

    // VCS dirs are always excluded (same as GrepTool VCS_DIRECTORIES_TO_EXCLUDE).
    if (sopts.exclude_dirs.empty()) {
        sopts.exclude_dirs = media::default_search_exclude_dirs();
    }

    // ── Execute ──────────────────────────────────────────────────────────────
    media::cli::yield_to_interrupt();
    if (media::cli::cancel_requested()) {
        PerFileResult r; r.path = root; r.ok = false; r.error = "interrupted";
        std::vector<PerFileResult> one; one.push_back(std::move(r));
        return build_envelope("file_search", std::move(one));
    }

    const media::SearchResult sr = media::search_files({root}, sopts);

    if (!sr.ok) {
        return fatal("file_search: " + sr.error);
    }

    // ── Format aggregate result ──────────────────────────────────────────────
    nlohmann::json result = nlohmann::json::object();

    const std::string mode_str =
        (sr.output_mode == media::SearchOutputMode::FilesWithMatches) ? "files_with_matches" :
        (sr.output_mode == media::SearchOutputMode::Count)            ? "count" : "content";

    result["mode"]         = mode_str;
    result["total_matches"] = sr.total_matches;

    if (sr.output_mode == media::SearchOutputMode::FilesWithMatches) {
        nlohmann::json fn_arr = nlohmann::json::array();
        for (const auto& fp : sr.file_paths) fn_arr.push_back(fp);
        result["filenames"] = std::move(fn_arr);
        result["num_files"] = (int)sr.file_paths.size();
    } else if (sr.output_mode == media::SearchOutputMode::Count) {
        result["content"]   = sr.content;
        result["num_files"] = sr.stats.files_scanned;
        result["num_matches"] = sr.total_matches;
    } else {
        // Content mode.
        result["content"]   = sr.content.empty() ? std::string("No matches found") : sr.content;
        result["num_lines"] = sr.total_matches;
        result["num_files"] = sr.stats.files_scanned;
    }

    if (sr.applied_limit  > 0) result["applied_limit"]  = sr.applied_limit;
    if (sr.applied_offset > 0) result["applied_offset"] = sr.applied_offset;

    result["stats"] = nlohmann::json{
        {"files_visited", sr.stats.files_visited},
        {"files_scanned", sr.stats.files_scanned},
        {"files_skipped", sr.stats.files_skipped},
        {"files_binary",  sr.stats.files_binary},
    };

    PerFileResult agg;
    agg.path   = "<aggregate>";
    agg.ok     = true;
    agg.result = std::move(result);
    std::vector<PerFileResult> out;
    out.push_back(std::move(agg));
    return build_envelope("file_search", std::move(out));
}

// Max body size for write_file (agent / tool JSON).
static constexpr std::size_t k_write_file_max_bytes = 16 * 1024 * 1024;

ExecuteResult do_file_read_path(const nlohmann::json& args) {
    if (!args.contains("path") || !args["path"].is_string())
        return fatal("file_read: 'path' (string) is required");
    const std::string raw = args["path"].get<std::string>();
    const std::string abs = resolve_tool_path_string(raw);
    pm::log::info_lazy("llm_path_tools", [&raw, &abs]() -> std::string {
        return std::string("file_read path_resolved: ") + raw + " -> " + fs::path(abs).generic_string();
    });
    const media::llm::ExecuteResult br = media::llm::execute("file_read", nlohmann::json{{"path", abs}});
    if (!br.ok || !br.envelope.value("ok", false)) {
        std::string err;
        if (br.envelope.contains("error") && br.envelope["error"].is_string())
            err = br.envelope["error"].get<std::string>();
        else if (!br.error.empty())
            err = br.error;
        else
            err = "failed";
        return fatal("file_read: " + err);
    }
    PerFileResult r;
    r.path = abs;
    r.ok   = true;
    if (br.envelope.contains("result") && br.envelope["result"].is_object())
        r.result = br.envelope["result"];
    std::vector<PerFileResult> one;
    one.push_back(std::move(r));
    return build_envelope("file_read", std::move(one));
}

ExecuteResult do_write_file(const nlohmann::json& args) {
    if (!args.contains("path") || !args["path"].is_string())
        return fatal("write_file: 'path' (string) is required");
    if (!args.contains("content") || !args["content"].is_string())
        return fatal("write_file: 'content' (string) is required");
    const std::string   path    = args["path"].get<std::string>();
    const std::string&  content = args["content"].get_ref<const std::string&>();
    if (content.size() > k_write_file_max_bytes)
        return fatal("write_file: content exceeds 16 MiB limit");

    const nlohmann::json& o     = options_or_empty(args);
    const bool            append = o.value("append", false);
    const std::string     out_abs = resolve_tool_path_string(path);
    pm::log::info_lazy("llm_path_tools", [&path, &out_abs]() -> std::string {
        return std::string("write_file path_resolved: ") + path + " -> " + fs::path(out_abs).generic_string();
    });
    if (const std::string w = llm_path_fs_deny(out_abs); !w.empty())
        return fatal(std::string("write_file: ") + w);
    if (const std::string wW = llm_path_write_deny(out_abs); !wW.empty())
        return fatal(std::string("write_file: ") + wW);

    media::cli::yield_to_interrupt();
    if (media::cli::cancel_requested()) {
        PerFileResult r;
        r.path  = out_abs;
        r.ok    = false;
        r.error = "interrupted (Ctrl+C)";
        std::vector<PerFileResult> one;
        one.push_back(std::move(r));
        return build_envelope("write_file", std::move(one));
    }

    std::error_code ec;
    const fs::path  p      = out_abs;
    const fs::path  parent = p.parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);

    const std::ios::openmode mode =
        (append ? (std::ios::app) : (std::ios::trunc)) | std::ios::binary;
    std::ofstream ofs(p, std::ios::out | mode);
    if (!ofs) return fatal("write_file: cannot open for writing: " + out_abs);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!ofs.good()) return fatal("write_file: write failed: " + out_abs);

    PerFileResult r;
    r.path         = out_abs;
    r.ok           = true;
    r.output_path  = out_abs;
    r.result       = nlohmann::json{
        {"bytes_written", static_cast<std::uint64_t>(content.size())},
        {"append",        append},
    };
    std::vector<PerFileResult> out;
    out.push_back(std::move(r));
    return build_envelope("write_file", std::move(out));
}

// ── Scheduler / memory tool handlers ────────────────────────────────────────

static ExecuteResult simple_ok(nlohmann::json payload) {
    ExecuteResult r;
    r.ok       = true;
    r.envelope = std::move(payload);
    r.envelope["ok"] = true;
    return r;
}

static ExecuteResult simple_error(const std::string& msg) {
    ExecuteResult r;
    r.ok       = false;
    r.error    = msg;
    r.envelope = nlohmann::json{{"ok", false}, {"error", msg}};
    return r;
}

// ── speak — TTS → default speaker ─────────────────────────────────────────
// Routes through chat Audio settings (App Settings → Chat → Voice & Audio).
// Supported providers: "pixlwiz" (LiteLLM proxy) and "elevenlabs" (direct).
// No implicit fallbacks — if tts_provider is empty or the key is missing the
// agent gets a clear error pointing to Settings.

static ExecuteResult do_speak(const nlohmann::json& args) {
#if !(defined(FEATURE_STT) && FEATURE_STT)
    (void)args;
    return fatal("speak: audio playback requires FEATURE_STT (not compiled in this build)");
#else
    const std::string text = args.value("text", std::string{});
    if (text.empty())
        return fatal("speak: 'text' is required and must not be empty");

    // ── Resolve provider & credentials from chat settings ──────────────────
    media::runtime_settings::ChatProviderSettings chat;
    {
        std::string err;
        if (!media::runtime_settings::load_chat_provider(chat, err))
            return fatal("speak: could not load chat settings: " + err);
    }
    const std::string& provider  = chat.tts_provider;
    const std::string& model     = chat.tts_model;
    const std::string& voice_id  = chat.tts_voice_id;
    if (provider.empty())
        return fatal("speak: no TTS provider configured — set one in App Settings → Chat → Voice & Audio");

    std::string api_key, base_url;
    media::runtime_settings::merge_provider_credentials(provider, false, api_key, base_url);
    if (api_key.empty())
        return fatal("speak: no API key for provider '" + provider
                     + "' — configure it in App Settings → API Providers");

    try {
        logger::info("[speak] provider=" + provider + " synthesising "
                     + std::to_string(text.size()) + " chars");

        std::vector<uint8_t> audio;

        if (provider == "pixlwiz") {
            // LiteLLM proxy → /v1/audio/speech
            const std::string eff_model = model.empty() ? "pixlwiz-speech" : model;
            audio = pm::tts::proxy_tts_synthesize(text, base_url, api_key,
                                                  eff_model, /*voice=*/{}, "mp3");
        } else {
            // ElevenLabs direct API
            pm::tts::ElevenLabsTTSConfig cfg;
            cfg.api_key = api_key;
            if (!model.empty())    cfg.model_id = model;
            if (!voice_id.empty()) cfg.voice_id  = voice_id;
            audio = pm::tts::elevenlabs_tts_synthesize(text, cfg);
        }

        logger::info("[speak] playing " + std::to_string(audio.size()) + " bytes");
        pm::audio::AudioOutput out;
        g_tts_out.store(&out, std::memory_order_release);
        try {
            out.play_sync(audio.data(), audio.size());
        } catch (...) {
            g_tts_out.store(nullptr, std::memory_order_release);
            throw;
        }
        g_tts_out.store(nullptr, std::memory_order_release);
        logger::info("[speak] done");

        ExecuteResult r;
        r.ok       = true;
        r.envelope = nlohmann::json{
            {"ok",       true},
            {"provider", provider},
            {"model",    model},
            {"chars",    static_cast<int>(text.size())},
            {"bytes",    static_cast<int>(audio.size())},
        };
        return r;
    } catch (const std::exception& ex) {
        return fatal(std::string("speak: ") + ex.what());
    }
#endif
}

static ExecuteResult do_schedule_at(const nlohmann::json& args) {
    if (!args.contains("title") || !args["title"].is_string())
        return simple_error("schedule_at: 'title' (string) is required");
    if (!args.contains("prompt") || !args["prompt"].is_string())
        return simple_error("schedule_at: 'prompt' (string) is required");
    if (!args.contains("run_at") || !args["run_at"].is_string())
        return simple_error("schedule_at: 'run_at' (ISO 8601 string) is required");

    const auto tp = media::llm::agent::parse_iso8601(args["run_at"].get<std::string>());
    if (!tp) return simple_error("schedule_at: could not parse 'run_at' as ISO 8601 datetime");

    media::llm::agent::ScheduledTask task;
    task.title          = args["title"].get<std::string>();
    task.prompt         = args["prompt"].get<std::string>();
    task.schedule.kind  = media::llm::agent::ScheduleKind::At;
    task.schedule.run_at = tp;
    task.next_run_at    = tp;
    if (args.contains("initial_state") && args["initial_state"].is_object())
        task.memory_state = args["initial_state"];
    if (args.contains("folder_hint") && args["folder_hint"].is_string())
        task.folder_hint  = args["folder_hint"].get<std::string>();

    const std::string id = media::llm::agent::task_store_create(std::move(task));
    return simple_ok(nlohmann::json{{"task_id", id}});
}

static ExecuteResult do_schedule_in(const nlohmann::json& args) {
    if (!args.contains("title") || !args["title"].is_string())
        return simple_error("schedule_in: 'title' (string) is required");
    if (!args.contains("prompt") || !args["prompt"].is_string())
        return simple_error("schedule_in: 'prompt' (string) is required");
    if (!args.contains("delay_seconds") || !args["delay_seconds"].is_number())
        return simple_error("schedule_in: 'delay_seconds' (integer) is required");

    const int secs = args["delay_seconds"].get<int>();
    if (secs < 1) return simple_error("schedule_in: delay_seconds must be >= 1");

    media::llm::agent::ScheduledTask task;
    task.title         = args["title"].get<std::string>();
    task.prompt        = args["prompt"].get<std::string>();
    task.schedule.kind  = media::llm::agent::ScheduleKind::In;
    task.schedule.delay = std::chrono::seconds(secs);
    if (args.contains("initial_state") && args["initial_state"].is_object())
        task.memory_state = args["initial_state"];
    if (args.contains("folder_hint") && args["folder_hint"].is_string())
        task.folder_hint  = args["folder_hint"].get<std::string>();

    const std::string id = media::llm::agent::task_store_create(std::move(task));
    return simple_ok(nlohmann::json{{"task_id", id}});
}

static ExecuteResult do_schedule_every(const nlohmann::json& args) {
    if (!args.contains("title") || !args["title"].is_string())
        return simple_error("schedule_every: 'title' (string) is required");
    if (!args.contains("prompt") || !args["prompt"].is_string())
        return simple_error("schedule_every: 'prompt' (string) is required");
    if (!args.contains("interval_seconds") || !args["interval_seconds"].is_number())
        return simple_error("schedule_every: 'interval_seconds' (integer) is required");

    const int secs = args["interval_seconds"].get<int>();
    if (secs < 5) return simple_error("schedule_every: interval_seconds must be >= 5");

    media::llm::agent::ScheduledTask task;
    task.title              = args["title"].get<std::string>();
    task.prompt             = args["prompt"].get<std::string>();
    task.schedule.kind      = media::llm::agent::ScheduleKind::Every;
    task.schedule.interval  = std::chrono::seconds(secs);

    if (args.contains("start_at") && args["start_at"].is_string()) {
        if (const auto tp = media::llm::agent::parse_iso8601(args["start_at"].get<std::string>()))
            task.schedule.start_at = tp;
    }
    if (args.contains("max_runs") && args["max_runs"].is_number_integer())
        task.schedule.max_runs = args["max_runs"].get<int>();
    if (args.contains("initial_state") && args["initial_state"].is_object())
        task.memory_state      = args["initial_state"];
    if (args.contains("folder_hint") && args["folder_hint"].is_string())
        task.folder_hint       = args["folder_hint"].get<std::string>();

    const std::string id = media::llm::agent::task_store_create(std::move(task));
    return simple_ok(nlohmann::json{{"task_id", id}});
}

static ExecuteResult do_schedule_cancel(const nlohmann::json& args) {
    if (!args.contains("task_id") || !args["task_id"].is_string())
        return simple_error("schedule_cancel: 'task_id' (string) is required");
    const std::string id = args["task_id"].get<std::string>();
    if (!media::llm::agent::task_store_cancel(id))
        return simple_error("schedule_cancel: task not found: " + id);
    return simple_ok(nlohmann::json{{"cancelled", id}});
}

static ExecuteResult do_schedule_list(const nlohmann::json& /*args*/) {
    const auto tasks = media::llm::agent::task_store_list();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tasks)
        arr.push_back(media::llm::agent::task_to_summary_json(t));
    ExecuteResult r;
    r.ok       = true;
    r.envelope = nlohmann::json{{"ok", true}, {"tasks", std::move(arr)}};
    return r;
}

static ExecuteResult do_memory_read(const nlohmann::json& args) {
    std::string id;
    if (args.contains("task_id") && args["task_id"].is_string())
        id = args["task_id"].get<std::string>();
    if (id.empty())
        id = media::llm::agent::get_current_task_id();
    if (id.empty())
        return simple_error("memory_read: no task_id provided and no current scheduled task");

    const auto task = media::llm::agent::task_store_get(id);
    if (!task)
        return simple_error("memory_read: context not found: " + id);

    const bool empty = task->memory_state.empty()
                    || (task->memory_state.is_object() && task->memory_state.empty());
    ExecuteResult r;
    r.ok = true;
    r.envelope = nlohmann::json{
        {"ok",      true},
        {"state",   task->memory_state},
        {"message", empty ? "No facts stored yet. Use memory_write to save some."
                          : "Memory loaded successfully."},
    };
    return r;
}

static ExecuteResult do_memory_write(const nlohmann::json& args) {
    if (!args.contains("state") || !args["state"].is_object())
        return simple_error("memory_write: 'state' (object) is required");

    const std::string id = media::llm::agent::get_current_task_id();
    if (id.empty())
        return simple_error("memory_write: no active session or scheduled task context");

    if (!media::llm::agent::task_store_write_memory(id, args["state"]))
        return simple_error("memory_write: context not found: " + id);

    ExecuteResult r;
    r.ok = true;
    // Return the stored state so the model can confirm what was saved.
    r.envelope = nlohmann::json{
        {"ok",      true},
        {"saved",   args["state"]},
        {"message", "Memory saved. It will be injected into your system prompt on the next turn."},
    };
    return r;
}

static ExecuteResult do_memory_append_event(const nlohmann::json& args) {
    if (!args.contains("event") || !args["event"].is_object())
        return simple_error("memory_append_event: 'event' (object) is required");

    const std::string id = media::llm::agent::get_current_task_id();
    if (id.empty())
        return simple_error("memory_append_event: no active session or scheduled task context");

    nlohmann::json ev = args["event"];
    if (!ev.contains("time")) ev["time"] = media::llm::agent::now_iso8601();
    media::llm::agent::task_store_append_event(id, ev);
    return simple_ok(nlohmann::json{{"task_id", id}});
}

/** [info] cwd + pinned path base + redacted args for every path-tool dispatch. */
static void log_path_tool_invoke_context(const std::string& tool, const nlohmann::json& args) {
    constexpr const char k_ch[] = "llm_path_tools";
    std::error_code ec;
    const fs::path cwd_p = fs::current_path(ec);
    const std::string cwd_s = ec ? std::string("(unavailable)") : cwd_p.generic_string();
    const std::string base_s = g_agent_path_base.empty()
        ? std::string("(unset)")
        : fs::path(g_agent_path_base).generic_string();

    std::string slim;
    try {
        if (!args.is_object()) {
            slim = args.dump();
        } else if (tool == "file_glob") {
            nlohmann::json j;
            if (args.contains("pattern")) j["pattern"] = args["pattern"];
            if (args.contains("options")) j["options"] = args["options"];
            slim = j.dump();
        } else if (tool == "list_images") {
            nlohmann::json j;
            if (args.contains("inputs")) j["inputs"] = args["inputs"];
            if (args.contains("options")) j["options"] = args["options"];
            slim = j.dump();
        } else if (tool == "file_read") {
            nlohmann::json j;
            if (args.contains("path")) j["path"] = args["path"];
            if (args.contains("options")) j["options"] = args["options"];
            slim = j.dump();
        } else if (tool == "write_file") {
            nlohmann::json j;
            if (args.contains("path")) j["path"] = args["path"];
            if (args.contains("content") && args["content"].is_string())
                j["content_bytes"] = args["content"].get<std::string>().size();
            if (args.contains("options")) j["options"] = args["options"];
            slim = j.dump();
        } else if (tool == "image_find") {
            nlohmann::json j;
            if (args.contains("inputs")) j["inputs"] = args["inputs"];
            if (args.contains("options")) j["options"] = args["options"];
            slim = j.dump();
        } else if (tool == "file_search") {
            nlohmann::json j;
            if (args.contains("pattern")) j["pattern"] = args["pattern"];
            if (args.contains("path"))    j["path"]    = args["path"];
            if (args.contains("glob"))    j["glob"]    = args["glob"];
            if (args.contains("type"))    j["type"]    = args["type"];
            if (args.contains("output_mode")) j["output_mode"] = args["output_mode"];
            slim = j.dump();
        } else {
            nlohmann::json j = nlohmann::json::object();
            for (auto it = args.begin(); it != args.end(); ++it) {
                const std::string& k = it.key();
                const auto&      v = it.value();
                if (k == "image" && v.is_object()) {
                    nlohmann::json im = nlohmann::json::object();
                    if (v.contains("mime")) im["mime"] = v["mime"];
                    if (v.contains("b64") && v["b64"].is_string())
                        im["b64_bytes"] = v["b64"].get<std::string>().size();
                    j["image"] = std::move(im);
                } else if (k == "references" && v.is_array()) {
                    nlohmann::json arr = nlohmann::json::array();
                    for (const auto& el : v) {
                        if (!el.is_object()) continue;
                        nlohmann::json one;
                        if (el.contains("mime")) one["mime"] = el["mime"];
                        if (el.contains("b64") && el["b64"].is_string())
                            one["b64_bytes"] = el["b64"].get<std::string>().size();
                        arr.push_back(std::move(one));
                    }
                    j["references"] = std::move(arr);
                } else if (k == "paths" || k == "inputs" || k == "options" || k == "path")
                    j[k] = v;
            }
            slim = j.empty() ? std::string("{...}") : j.dump();
        }
    } catch (...) {
        slim = "<args summary error>";
    }

    pm::log::info_lazy(k_ch, [&tool, cwd_s, base_s, slim]() -> std::string {
        return std::string("invoke ctx tool=") + tool + " cwd=" + cwd_s + " path_base=" + base_s + " " + slim;
    });
}

ExecuteResult execute_impl(const std::string& name, const nlohmann::json& arguments) {
    constexpr const char k_ch[] = "llm_path_tools";

    auto log_done = [k_ch, &name](ExecuteResult r) -> ExecuteResult {
        if (r.ok)
            pm::log::debug_lazy(k_ch, [&] { return std::string("tool=") + name + " ok"; });
        else
            pm::log::warn_lazy(k_ch, [&] { return std::string("tool=") + name + " failed: " + r.error; });
        return r;
    };

    try {
        if (!g_agent_tool_blocklist.empty()) {
            std::string nlow = name;
            str_tolower_in_place(nlow);
            if (g_agent_tool_blocklist.count(nlow)) {
                pm::log::warn_lazy(k_ch, [&] { return std::string("tool blocked name=") + name; });
                return fatal("tool is disabled: " + name);
            }
        }
        log_path_tool_invoke_context(name, arguments);
        if (name == "list_images")     return log_done(do_list_images(arguments));
        if (name == "file_glob")       return log_done(do_file_glob(arguments));
        if (name == "file_read")       return log_done(do_file_read_path(arguments));
        if (name == "file_search")     return log_done(do_file_search(arguments));
        if (name == "image_resize")    return log_done(do_resize(arguments));
        if (name == "image_compress")  return log_done(do_compress(arguments));
        if (name == "image_transform") return log_done(do_transform(arguments));
        if (name == "image_create")    return log_done(do_create_image(arguments));
        if (name == "create_video") return log_done(do_create_video(arguments, name));
        if (name == "image_understand")   return log_done(do_understand(arguments));
        if (name == "image_from_camera")  return log_done(do_camera(arguments));
        if (name == "speak")               return log_done(do_speak(arguments));
        // image_meta and image_find removed from agent tool catalog;
        // do_meta / do_find kept for internal use (sidecar generation, CLI).
        // Return a clear error if the agent somehow still calls them.
        if (name == "image_meta")
            return fatal("image_meta is no longer available; use image_understand with a prompt instead");
        if (name == "image_find")
            return fatal("image_find is no longer available as an agent tool");
        if (name == "write_file")      return log_done(do_write_file(arguments));
        // Scheduler tools
        if (name == "schedule_at")     return log_done(do_schedule_at(arguments));
        if (name == "schedule_in")     return log_done(do_schedule_in(arguments));
        if (name == "schedule_every")  return log_done(do_schedule_every(arguments));
        if (name == "schedule_cancel") return log_done(do_schedule_cancel(arguments));
        if (name == "schedule_list")   return log_done(do_schedule_list(arguments));
        // Memory tools
        if (name == "memory_read")          return log_done(do_memory_read(arguments));
        if (name == "memory_write")         return log_done(do_memory_write(arguments));
        if (name == "memory_append_event")  return log_done(do_memory_append_event(arguments));
        // Shell execution
        if (name == "run")                 return log_done(media::llm::run::execute(arguments, g_agent_path_base));
        // Computer use / app inspection
        if (media::llm::computer_use::is_computer_use_tool(name))
            return log_done(media::llm::computer_use::execute(name, arguments, g_agent_path_base));
        pm::log::warn_lazy(k_ch, [&] { return std::string("unknown tool name=") + name; });
        return fatal("unknown tool: " + name);
    } catch (const std::exception& e) {
        pm::log::error_lazy(k_ch, [&] { return std::string("tool=") + name + " exception: " + e.what(); });
        return fatal(std::string("path tool '") + name + "' exception: " + e.what());
    } catch (...) {
        pm::log::error_lazy(k_ch, [&] { return std::string("tool=") + name + " exception: unknown"; });
        return fatal("path tool '" + name + "' exception: unknown");
    }
}

} // namespace detail

ExecuteResult execute(const std::string& name, const nlohmann::json& arguments) {
    return detail::execute_impl(name, arguments);
}

void abort_active_speak() {
#if defined(FEATURE_STT) && FEATURE_STT
    auto* p = g_tts_out.load(std::memory_order_acquire);
    if (p) p->stop();
#endif
}

void abort_active_run() {
    media::llm::run::abort_active_run();
}

std::string envelope_text(const ExecuteResult& r) {
    return r.envelope.dump();
}

} // namespace media::llm::path
