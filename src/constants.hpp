// Branding: generated `build/include/pm_branding_config.hpp` (configure from `src/pm_branding_config.hpp.in` + cmake/Branding.cmake).
// Human-facing: PolyMech, PM-Image. Technical slugs: pm-image, pm-image-execute (edit Branding.cmake to rebrand).
// Win11 sparse / Explorer: `branding/AppxManifest.xml.in` → `dist/sparse/AppxManifest.xml` (placeholders from CMake; see docs/win11.md).
#pragma once

#include "pm_branding_config.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

#ifndef FEATURE_AGENT_COMPUTER_USE
#  define FEATURE_AGENT_COMPUTER_USE 1
#endif

namespace pm {

/// Convert a `std::filesystem::path` to a UTF-8 `std::string`.
/// In C++17 `path::u8string()` returned `std::string`; in C++20 it returns
/// `std::u8string` (`char8_t`-based).  This helper bridges both standards
/// by reinterpreting the bytes — safe because `char8_t` has the same
/// representation as `unsigned char` / `char` on all platforms.
inline std::string path_u8_str(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

/**
 * Pixlwiz API host when `SERVER_URL` / `VITE_SERVER_IMAGE_API_URL` / `CLIENT_URL` are unset
 * (e.g. Share from the Win32 UI without cwd `.env`). No trailing slash.
 * Clear to `""` to require env only.
 */
inline constexpr const char k_pixlwiz_service_server_base_default_u8[] = "https://pixlwiz.com";

/**
 * Local pm-pics / image API (dev). No trailing slash.
 * Use from env / tooling when pointing at a machine-local stack (see docs/zitadel.md).
 */
inline constexpr const char k_pixlwiz_service_server_base_default_u8_local[] = "http://localhost:4555";

/**
 * Default ZITADEL issuer (OpenID “issuer” URL) when `ZITADEL_ISSUER`, `VITE_ZITADEL_AUTHORITY`, and CLI `--issuer`
 * are all unset — e.g. `pm-image login` spawned from the app with no `.env` next to the exe. Same tenant as
 * production Pixlwiz ([auth.polymech.info](https://auth.polymech.info/)); no trailing slash.
 * Clear to `""` to require env / flags only (`pm_image_cmd_login` skips this fallback if empty).
 */
inline constexpr const char k_pixlwiz_zitadel_authority[] = "https://auth.polymech.info";

/**
 * Default OIDC public client id when `ZITADEL_OIDC_CLIENT_ID`, `VITE_ZITADEL_CLIENT_ID`, and `--client-id` are unset.
 * Must match a ZITADEL application that allows the loopback redirect URI used by `pm-image login` (see docs/zitadel.md).
 * Clear to `""` to require env / flags only.
 */
inline constexpr const char k_pixlwiz_zitadel_client_id[] = "367440527605432321";

} // namespace pm

// ── UI timing constants ───────────────────────────────────────────────────────
// Centralised here so all timer intervals are tunable from one header.
// See `docs/selection.md` §"Centralize timing constants" for rationale.
namespace pm::ui {

/// One-shot delay after `OnInitialUpdate` before `OnDeferredPostLayoutInit`
/// runs (session replay, startup flush, Shell rebuild, file-tree browse).
inline constexpr int k_post_layout_init_delay_ms       = 150;  // kPostLayoutInitTimerId

/// Deferred `IExplorerBrowser` navigation (`ScheduleFileTreeBrowse`).
/// Must be > 0 so navigation posts after any pending Shell rebuild.
inline constexpr int k_file_tree_nav_delay_ms           = 120;  // kFileTreeNavTimerId

/// `IFolderView2::GetSelection` poll interval in `CExplorerBrowserView`.
inline constexpr int k_explorer_selection_poll_ms       = 250;  // TIMER_SELECTION

/// System stats (CPU / RAM / FPS) status-bar refresh interval.
inline constexpr int k_stats_poll_ms                    = 5000; // kStatsTimerId

/// Monotonic correlation-ID generator for deferred-operation logging.
/// Thread-safe; IDs are unique within one process lifetime.
inline uint32_t next_correlation_id() noexcept {
    static uint32_t s_id = 0;
    return ++s_id;
}

} // namespace pm::ui

// ── LLM defaults ─────────────────────────────────────────────────────────────
namespace pm::llm {

/// Default HTTP endpoint mode for all LLM agent paths (CLI, UI, scheduler).
/// "responses" → POST /responses (OpenAI Responses API — OpenRouter, LiteLLM, Pixlwiz proxy).
/// "completion" → POST /chat/completions (classic OpenAI-compatible path).
inline constexpr const char k_default_api_mode[] = "responses";

// ── Session memory (chat-panel persistent memory across turns) ────────────────
/// When true, each completed turn is automatically appended to the session
/// memory ring buffer so the LLM can see past conversation context.
inline constexpr bool k_auto_memory_enabled = true;
/// Maximum number of past turns kept in the session event ring buffer.
/// Older entries are evicted when this limit is reached.
inline constexpr size_t k_session_memory_max_turns = 40;
/// Maximum UTF-8 characters from the user prompt stored per auto-memory entry.
inline constexpr size_t k_session_memory_user_chars = 1800;
/// Maximum UTF-8 characters from the assistant reply stored per auto-memory entry.
inline constexpr size_t k_session_memory_asst_chars = 5400;
/// Maximum total UTF-8 characters injected into the system prompt for past turns.
inline constexpr size_t k_session_memory_inject_chars = 36000;

// ── Agent tool flags ──────────────────────────────────────────────────────────
//
// Each enumerator maps 1-to-1 to a named tool in media::llm::path::tool_catalog().
// The order of bit positions is stable — new tools are appended at the end.
//
// Usage:
//   AgentTools flags = k_agent_tools_all;              // all on
//   flags &= ~static_cast<AgentTools>(AgentTool::Speak); // disable speak
//   auto tools = media::llm::path::tool_catalog_openai_for_flags(flags);
//
enum class AgentTool : uint64_t {
    None              = 0,
    // ── File / folder tools ──────────────────────────
    ListImages        = 1ULL <<  0,  // list_images
    FileGlob          = 1ULL <<  1,  // file_glob
    FileRead          = 1ULL <<  2,  // file_read
    FileSearch        = 1ULL <<  3,  // file_search
    // ── Image processing ────────────────────────────
    ImageResize       = 1ULL <<  4,  // image_resize
    ImageTransform    = 1ULL <<  5,  // image_transform
    ImageCreate       = 1ULL <<  6,  // image_create
    CreateVideo       = 1ULL <<  7,  // create_video
    ImageUnderstand   = 1ULL <<  8,  // image_understand
    ImageFromCamera   = 1ULL <<  9,  // image_from_camera
    // ── Utility tools ───────────────────────────────
    WriteFile         = 1ULL << 10,  // write_file
    Speak             = 1ULL << 11,  // speak
    // ── Scheduler tools ─────────────────────────────
    ScheduleAt        = 1ULL << 12,  // schedule_at
    ScheduleIn        = 1ULL << 13,  // schedule_in
    ScheduleEvery     = 1ULL << 14,  // schedule_every
    ScheduleCancel    = 1ULL << 15,  // schedule_cancel
    ScheduleList      = 1ULL << 16,  // schedule_list
    // ── Memory tools ────────────────────────────────
    MemoryRead        = 1ULL << 17,  // memory_read
    MemoryWrite       = 1ULL << 18,  // memory_write
    MemoryAppendEvent = 1ULL << 19,  // memory_append_event
    // ── Shell execution ─────────────────────────────
    Run               = 1ULL << 20,  // run
    // ── Computer use / app inspection ───────────────
    AppInspectDump    = 1ULL << 21,  // app_inspect_dump
    AppInspectFind    = 1ULL << 22,  // app_inspect_find
    AppScreenshot     = 1ULL << 23,  // app_screenshot
    AppClick          = 1ULL << 24,  // app_click
    AppOpen           = 1ULL << 25,  // app_open
    AppType           = 1ULL << 26,  // app_type
    AppHotkey         = 1ULL << 27,  // app_hotkey
    AppClose          = 1ULL << 28,  // app_close
    AppDrag           = 1ULL << 29,  // app_drag
    AppBatch          = 1ULL << 30,  // app_batch
};

/// Bitmask of zero or more AgentTool flags.
using AgentTools = uint64_t;

constexpr AgentTools operator|(AgentTool a, AgentTool b) noexcept {
    return static_cast<AgentTools>(a) | static_cast<AgentTools>(b);
}
constexpr AgentTools operator|(AgentTools a, AgentTool b) noexcept {
    return a | static_cast<AgentTools>(b);
}
constexpr AgentTools operator&(AgentTools a, AgentTool b) noexcept {
    return a & static_cast<AgentTools>(b);
}
constexpr bool agent_tool_enabled(AgentTools flags, AgentTool t) noexcept {
    return (flags & static_cast<AgentTools>(t)) != 0;
}

/// All tools enabled — use as the default preset for new agent configurations.
inline constexpr AgentTools k_agent_tools_all =
    AgentTool::ListImages     | AgentTool::FileGlob        | AgentTool::FileRead       |
    AgentTool::FileSearch     | AgentTool::ImageResize     | AgentTool::ImageTransform |
    AgentTool::ImageCreate    | AgentTool::CreateVideo     | AgentTool::ImageUnderstand|
    AgentTool::ImageFromCamera| AgentTool::WriteFile       | AgentTool::Speak          |
    AgentTool::ScheduleAt     | AgentTool::ScheduleIn      | AgentTool::ScheduleEvery  |
    AgentTool::ScheduleCancel | AgentTool::ScheduleList    |
    AgentTool::MemoryRead     | AgentTool::MemoryWrite     | AgentTool::MemoryAppendEvent |
    AgentTool::Run            | AgentTool::AppInspectDump  | AgentTool::AppInspectFind |
    AgentTool::AppScreenshot  | AgentTool::AppClick        | AgentTool::AppOpen        |
    AgentTool::AppType        | AgentTool::AppHotkey       | AgentTool::AppClose       |
    AgentTool::AppDrag        | AgentTool::AppBatch;

/// No tools — useful as a starting point when building a minimal allow-list.
inline constexpr AgentTools k_agent_tools_none = static_cast<AgentTools>(AgentTool::None);

} // namespace pm::llm

// ── CLI command flags ────────────────────────────────────────────────────────
// Bitmask that controls which subcommands pm_image_register_cli() wires up.
// Flip individual bits to compile-time disable verbs (e.g. for minimal builds).
namespace pm::cli {

enum class Cmd : uint64_t {
    None              = 0,
    Resize            = 1ULL <<  0,
    Compress          = 1ULL <<  1,
    Transform         = 1ULL <<  2,
    Create            = 1ULL <<  3,
    Meta              = 1ULL <<  4,
    Find              = 1ULL <<  5,
    Search            = 1ULL <<  6,
    Duplicates        = 1ULL <<  7,
    Serve             = 1ULL <<  8,
    Ipc               = 1ULL <<  9,
    Settings          = 1ULL << 10,
    ProviderModels    = 1ULL << 11,
    Llm               = 1ULL << 12,
    RegisterExplorer  = 1ULL << 13,
    App               = 1ULL << 14,
    Service           = 1ULL << 15,
    Batch             = 1ULL << 16,
    Login             = 1ULL << 17,
    Audio             = 1ULL << 18,
    Video             = 1ULL << 19,
    Xblox             = 1ULL << 20,
    Test              = 1ULL << 21,
    License           = 1ULL << 22,
    Status            = 1ULL << 23,
    Commands          = 1ULL << 24,
    Daemon            = 1ULL << 25,
    RegisterStartMenu = 1ULL << 26,
    Installer         = 1ULL << 27,
    Assistant         = 1ULL << 28,
    Info              = 1ULL << 29,
};

using CmdFlags = uint64_t;

constexpr CmdFlags operator|(Cmd a, Cmd b) noexcept {
    return static_cast<CmdFlags>(a) | static_cast<CmdFlags>(b);
}
constexpr CmdFlags operator|(CmdFlags a, Cmd b) noexcept {
    return a | static_cast<CmdFlags>(b);
}
constexpr bool cmd_enabled(CmdFlags flags, Cmd c) noexcept {
    return (flags & static_cast<CmdFlags>(c)) != 0;
}

inline constexpr CmdFlags k_cmds_all =
    Cmd::Resize      | Cmd::Compress     | Cmd::Transform    | Cmd::Create       |
    Cmd::Meta        | Cmd::Find         | Cmd::Search       | Cmd::Duplicates   |
    Cmd::Serve       | Cmd::Ipc          | Cmd::Settings     | Cmd::ProviderModels |
    Cmd::Llm         | Cmd::RegisterExplorer | Cmd::App      | Cmd::Service      |
    Cmd::Batch       | Cmd::Login        | Cmd::Audio        | Cmd::Video        |
    Cmd::Xblox       | Cmd::Test         | Cmd::License      | Cmd::Status       |
    Cmd::Commands    | Cmd::Daemon       | Cmd::RegisterStartMenu | Cmd::Installer |
    Cmd::Assistant    | Cmd::Info;

inline constexpr CmdFlags k_cmds_none = static_cast<CmdFlags>(Cmd::None);

// ── FEATURE_COMMAND_* default definitions ────────────────────────────────────
// If CMake doesn't define these, default to enabled (backward compatible)
#ifndef FEATURE_COMMAND_RESIZE
#define FEATURE_COMMAND_RESIZE 1
#endif
#ifndef FEATURE_COMMAND_COMPRESS
#define FEATURE_COMMAND_COMPRESS 1
#endif
#ifndef FEATURE_COMMAND_TRANSFORM
#define FEATURE_COMMAND_TRANSFORM 1
#endif
#ifndef FEATURE_COMMAND_CREATE
#define FEATURE_COMMAND_CREATE 1
#endif
#ifndef FEATURE_COMMAND_META
#define FEATURE_COMMAND_META 1
#endif
#ifndef FEATURE_COMMAND_FIND
#define FEATURE_COMMAND_FIND 1
#endif
#ifndef FEATURE_COMMAND_SEARCH
#define FEATURE_COMMAND_SEARCH 1
#endif
#ifndef FEATURE_COMMAND_DUPLICATES
#define FEATURE_COMMAND_DUPLICATES 1
#endif
#ifndef FEATURE_COMMAND_PROVIDER_MODELS
#define FEATURE_COMMAND_PROVIDER_MODELS 1
#endif
#ifndef FEATURE_COMMAND_LLM
#define FEATURE_COMMAND_LLM 1
#endif
#ifndef FEATURE_REGISTER_EXPLORER
#define FEATURE_REGISTER_EXPLORER 1
#endif
#ifndef FEATURE_COMMAND_APP
#define FEATURE_COMMAND_APP 1
#endif
#ifndef FEATURE_COMMAND_SERVICE
#define FEATURE_COMMAND_SERVICE 1
#endif
#ifndef FEATURE_COMMAND_BATCH
#define FEATURE_COMMAND_BATCH 1
#endif
#ifndef FEATURE_COMMAND_SETTINGS
#define FEATURE_COMMAND_SETTINGS 1
#endif
#ifndef FEATURE_COMMAND_TEST
#define FEATURE_COMMAND_TEST 1
#endif
#ifndef FEATURE_COMMAND_LICENSE
#define FEATURE_COMMAND_LICENSE 1
#endif
#ifndef FEATURE_COMMAND_STATUS
#define FEATURE_COMMAND_STATUS 1
#endif
#ifndef FEATURE_COMMAND_COMMANDS
#define FEATURE_COMMAND_COMMANDS 1
#endif
#ifndef FEATURE_COMMAND_INFO
#define FEATURE_COMMAND_INFO 1
#endif
#ifndef FEATURE_COMMAND_REPLAY
#define FEATURE_COMMAND_REPLAY 1
#endif
#ifndef FEATURE_COMMAND_SERVE
#define FEATURE_COMMAND_SERVE 1
#endif
#ifndef FEATURE_COMMAND_IPC
#define FEATURE_COMMAND_IPC 1
#endif
#ifndef FEATURE_COMMAND_AUDIO
#define FEATURE_COMMAND_AUDIO 1
#endif
#ifndef FEATURE_COMMAND_VIDEO
#define FEATURE_COMMAND_VIDEO 1
#endif
#ifndef FEATURE_COMMAND_XBLOX
#define FEATURE_COMMAND_XBLOX 1
#endif
#ifndef FEATURE_ASSISTANT
#  if defined(_WIN32)
#    define FEATURE_ASSISTANT 1
#  else
#    define FEATURE_ASSISTANT 0
#  endif
#endif
#ifndef FEATURE_COMMAND_SESSION_PERSISTENCE
#define FEATURE_COMMAND_SESSION_PERSISTENCE 1
#endif
#ifndef FEATURE_COMMAND_LOG_VIEW
#define FEATURE_COMMAND_LOG_VIEW 1
#endif
#ifndef FEATURE_COMMAND_QUEUE_VIEW
#define FEATURE_COMMAND_QUEUE_VIEW 1
#endif
#ifndef FEATURE_COMMAND_UI_COMMAND_CONTROL
#define FEATURE_COMMAND_UI_COMMAND_CONTROL 1
#endif
#ifndef FEATURE_DAEMON
#define FEATURE_DAEMON 1
#endif
#ifndef FEATURE_EXPLORER_INTEGRATION
#define FEATURE_EXPLORER_INTEGRATION 1
#endif

// ── Base FEATURE_* default definitions ────────────────────────────────────────
// These control availability of underlying functionality (not just command enable)
#ifndef FEATURE_XBLOX
#define FEATURE_XBLOX 1
#endif
#ifndef FEATURE_TRIAL_CHECK
#define FEATURE_TRIAL_CHECK 0
#endif
#ifndef FEATURE_LICENSE_FILE
#define FEATURE_LICENSE_FILE 0
#endif
#ifndef FEATURE_SERVE
#define FEATURE_SERVE 0
#endif
#ifndef FEATURE_IPC
#define FEATURE_IPC 0
#endif
#ifndef FEATURE_PIXLWIZ_AUTH
#define FEATURE_PIXLWIZ_AUTH 0
#endif
#ifndef FEATURE_STT
#define FEATURE_STT 0
#endif
#ifndef FEATURE_VIDEO
#define FEATURE_VIDEO 0
#endif

// ── Compile-time command availability helpers ────────────────────────────────
// Maps Cmd enum to FEATURE_COMMAND_* macros for compile-time checking
constexpr bool cmd_available_at_compile_time(Cmd c) noexcept {
    switch (c) {
        case Cmd::Resize:           return FEATURE_COMMAND_RESIZE;
        case Cmd::Compress:         return FEATURE_COMMAND_COMPRESS;
        case Cmd::Transform:        return FEATURE_COMMAND_TRANSFORM;
        case Cmd::Create:           return FEATURE_COMMAND_CREATE;
        case Cmd::Meta:             return FEATURE_COMMAND_META;
        case Cmd::Find:             return FEATURE_COMMAND_FIND;
        case Cmd::Search:           return FEATURE_COMMAND_SEARCH;
        case Cmd::Duplicates:       return FEATURE_COMMAND_DUPLICATES;
        case Cmd::Serve:            return FEATURE_SERVE && FEATURE_COMMAND_SERVE;
        case Cmd::Ipc:              return FEATURE_IPC && FEATURE_COMMAND_IPC;
        case Cmd::Settings:         return FEATURE_COMMAND_SETTINGS;
        case Cmd::ProviderModels:   return FEATURE_COMMAND_PROVIDER_MODELS;
        case Cmd::Llm:              return FEATURE_COMMAND_LLM;
        case Cmd::RegisterExplorer: return FEATURE_REGISTER_EXPLORER;
        case Cmd::RegisterStartMenu:return FEATURE_REGISTER_EXPLORER;
        case Cmd::Installer:        return FEATURE_REGISTER_EXPLORER;
        case Cmd::App:              return FEATURE_COMMAND_APP;
        case Cmd::Service:          return FEATURE_COMMAND_SERVICE;
        case Cmd::Batch:            return FEATURE_COMMAND_BATCH;
        case Cmd::Login:            return FEATURE_PIXLWIZ_AUTH;
        case Cmd::Audio:            return FEATURE_STT && FEATURE_COMMAND_AUDIO;
        case Cmd::Video:            return FEATURE_VIDEO && FEATURE_COMMAND_VIDEO;
        case Cmd::Xblox:            return FEATURE_XBLOX && FEATURE_COMMAND_XBLOX;
        case Cmd::Test:             return FEATURE_COMMAND_TEST;
        case Cmd::License:          return FEATURE_COMMAND_LICENSE;
        case Cmd::Status:           return FEATURE_COMMAND_STATUS;
        case Cmd::Commands:         return FEATURE_COMMAND_COMMANDS;
        case Cmd::Daemon:           return FEATURE_DAEMON;
        case Cmd::Assistant:        return FEATURE_ASSISTANT;
        case Cmd::Info:             return FEATURE_COMMAND_INFO;
        default:                    return true;
    }
}

} // namespace pm::cli
