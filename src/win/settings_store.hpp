#pragma once

#include "LayoutStore.hpp"
#include "core/settings_portable.hpp"

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace media::settings {

using WindowLayout = media::layout::WindowLayout;
using WorkbenchWindowDefaults = media::layout::WorkbenchWindowDefaults;

/** %APPDATA%\PolyMech\pm-image (created if missing). */
std::filesystem::path get_config_dir();

/** Override the process profile/config root (settings, caches, MCP, skills, web state). */
void set_config_dir_override(const std::filesystem::path& dir);

/**
 * Encrypted settings file: get_config_dir() / "settings.json" (binary PME1 blob or legacy UTF-8 JSON).
 * On first load, when this profile file is missing and no read override is active, Windows imports
 * `settings.json` from the directory containing pm-image.exe if present.
 */
std::filesystem::path get_settings_json_path();

/**
 * User-authored ribbon/app command file: get_config_dir() / "commands.json".
 */
std::filesystem::path get_command_json_path();

/**
 * Global shortcut daemon config: get_config_dir() / "daemon.json".
 */
std::filesystem::path get_daemon_json_path();

/**
 * Path @ref load_settings_utf8 reads from: CLI @c --settings override when set, else @ref get_settings_json_path.
 * Saves always use @ref get_settings_json_path.
 */
std::filesystem::path get_settings_effective_read_path();

/** True after @ref set_settings_read_path_override (e.g. @c pm-image --settings=…). */
bool has_settings_read_path_override();

/**
 * Use @p absolute_path as the only source for @ref load_settings_utf8 in this process (read-through).
 * Does not copy into the profile store. Cleared on process exit.
 */
void set_settings_read_path_override(const std::filesystem::path& absolute_path);

/** Remove a prior @ref set_settings_read_path_override (tests / rare tooling). */
void clear_settings_read_path_override();

/**
 * Append one UTF-8 line to `get_config_dir() / "pm-image-iexecute.log"` (same file as the IExecute DLL).
 * Prefixes wall time and `[pm-image]` so Explorer shell and the main EXE can be correlated.
 */
void append_explorer_shell_correlation_log_utf8(const std::string& line);

/**
 * Load UTF-8 JSON string from @ref get_settings_effective_read_path (PME1 or plaintext JSON).
 * If no override is active and the profile file is missing, first imports a `settings.json` next to
 * pm-image.exe into the profile store. Supports:
 * - libsodium secretbox file (magic PME1 + nonce + ciphertext)
 * - legacy plaintext JSON (trimmed first char '{')
 */
bool load_settings_utf8(std::string& out_json, std::string& err_out);

/**
 * Load UTF-8 from @ref get_command_json_path. Missing file is success with an empty string.
 */
bool load_command_json_utf8(std::string& out_json, std::string& err_out);

/**
 * Save UTF-8 to @ref get_command_json_path as a plaintext user-authored command config.
 */
bool save_command_json_utf8(const std::string& utf8_json, std::string& err_out);

/// Optional: on cache *miss* only, with a short reason (see `SettingsLoadLabelScope`).
void set_settings_load_tracing(void (*fn)(const char* reason));
void clear_settings_load_tracing();

/// RAII: next nested `load_settings_utf8` cache miss is attributed to @p reason in the trace.
class SettingsLoadLabelScope {
public:
    explicit SettingsLoadLabelScope(const char* reason);
    ~SettingsLoadLabelScope();
    SettingsLoadLabelScope(const SettingsLoadLabelScope&)            = delete;
    SettingsLoadLabelScope& operator=(const SettingsLoadLabelScope&) = delete;

private:
    bool pushed_ = false;
};

/**
 * Save UTF-8 JSON (always written as encrypted PME1 blob; 32-byte key in .settings-key.dat protected via DPAPI).
 */
bool save_settings_utf8(const std::string& utf8_json, std::string& err_out);

/**
 * Export all settings to a file. If @p encrypted is true, writes the same PME1 binary format as settings.json
 * (portable only to machines that can read the same DPAPI-protected key file — typically same Windows user profile).
 * If false, writes UTF-8 JSON (contains API keys — handle carefully).
 * Merges missing @c workbench.main.chrome boolean keys (@c show_menu, @c show_status_bar, @c show_ribbon_strip) and
 * missing @c workbench.main.window_defaults @c width / @c height (1280×860) so the export documents preset sizing;
 * does not change the on-disk app settings store.
 */
bool export_settings_file(const std::filesystem::path& path, bool encrypted, std::string& err_out);

/**
 * Import settings from a file (PME1 blob or plaintext UTF-8 JSON) and persist to the live settings store.
 */
bool import_settings_file(const std::filesystem::path& path, std::string& err_out);

// ---------------------------------------------------------------------------
// Extensible settings interface (API keys, custom JSON, history, …)
// ---------------------------------------------------------------------------
// The root document is `settings.json` (encrypted PME1).  Top-level keys
// already used by this app include: `providers`, `window`,
// `appearance`, `chat`, `explorer_presets`, `chat_web`.  For app-specific or UI state that does not warrant a
// dedicated struct, use the reserved subtrees in @ref SettingsSubtreeKeys and
// @c load_subtree / @c save_subtree (or @c merge_subtree_object for partial
// updates to an object).  @c load_history_data / @c load_custom_data are
// thin aliases with stable key names.  @ref SettingsApiKeys groups API key
// read/write for discoverability; it forwards to the functions below.
// ---------------------------------------------------------------------------

/** String literals for `settings.json` top-level keys reserved for extensions. */
struct SettingsSubtreeKeys {
    static constexpr const char* custom  = "custom";
    static constexpr const char* history = "history";
    /**
     * Windows Explorer context-menu presets (transform / resize / meta / future ops).
     * Authoritative copy lives only in the encrypted store — `register_explorer` and any
     * `pm-image … --preset <id>` helper read via @ref load_explorer_presets.
     */
    static constexpr const char* explorer_presets = "explorer_presets";
    /** WebView2 chat UI: prompt history, quick actions, panel toggles, optional `sessions` (see save_chat_web). */
    static constexpr const char* chat_web = "chat_web";
};

/**
 * Load `settings.json["chat_web"]` (object or null). Empty / missing returns true with @p out = {}.
 * May include `sessions` (array of full session objects) written by the Win32 WebView host.
 */
bool load_chat_web(nlohmann::json& out, std::string& err);

/**
 * Replace `settings.json["chat_web"]` with @p doc; other top-level keys preserved.
 * The WebView host merges `sessions` when the UI posts `chatWebState` without that key.
 */
bool save_chat_web(const nlohmann::json& doc, std::string& err);

/**
 * One user-defined shell / Explorer action. `op` selects the dispatcher branch
 * (e.g. `transform`, `resize`, `meta`); `args` holds op-specific options (see product docs).
 */
struct ExplorerPreset {
    std::string     id;    ///< Stable id for registry / CLI (ASCII recommended).
    std::string     label; ///< Shown in Explorer (MUIVerb).
    std::string     op;    ///< Operation id (e.g. transform, resize, meta).
    nlohmann::json  args = nlohmann::json::object();
};

/**
 * Load `settings.json[explorer_presets]`. Document shape:
 *   `{ "version": 1, "items": [ { "id", "label", "op", "args"?: {…} } ] }`
 * Missing or invalid document yields an empty @p out and success (first run).
 */
bool load_explorer_presets(std::vector<ExplorerPreset>& out, std::string& err);

/**
 * Replace the `explorer_presets` subtree; all other `settings.json` keys preserved.
 */
bool save_explorer_presets(const std::vector<ExplorerPreset>& items, std::string& err);

/**
 * Read one top-level value from `settings.json`.
 * If @p key is missing, @p out is JSON `null` (and the function still returns true).
 */
bool load_subtree(const std::string& key, nlohmann::json& out, std::string& err);

/**
 * Replace the top-level @p key and persist.  All other top-level keys are kept.
 */
bool save_subtree(const std::string& key, const nlohmann::json& value, std::string& err);

/**
 * Shallow-merge @p fields into the JSON object at @p key.
 * If the existing value is not an object, it is replaced by an object built from @p fields.
 * @p fields must be a JSON object.
 */
bool merge_subtree_object(const std::string& key, const nlohmann::json& fields, std::string& err);

bool load_custom_data(nlohmann::json& out, std::string& err);
bool save_custom_data(const nlohmann::json& value, std::string& err);
bool load_history_data(nlohmann::json& out, std::string& err);
bool save_history_data(const nlohmann::json& value, std::string& err);

// ---------------------------------------------------------------------------
// Provider key management (stored under "providers" key in settings.json)
// ---------------------------------------------------------------------------

/** Per-provider configuration stored in settings.json. */
using ProviderEntry = media::settings_types::ProviderEntry;
using ProviderMap = media::settings_types::ProviderMap;

/**
 * Known provider names and their default base URLs.
 * Order matches the UI display order.
 */
struct ProviderDefaults {
    const char* name;
    const char* display_name;
    const char* base_url;
    std::vector<const char*> models;
};

/** Static list of supported providers. */
const ProviderDefaults* known_providers(int* count_out);

/**
 * Load provider entries from settings.json.
 * Missing providers are returned with empty api_key and provider defaults for base_url.
 */
bool load_providers(ProviderMap& out, std::string& err);

/**
 * Save provider entries to settings.json.
 * Non-provider keys in the JSON (e.g. prompt_presets) are preserved.
 * Legacy active_provider key is removed on save (no longer used per design).
 */
bool save_providers(const ProviderMap& providers, std::string& err);

/**
 * Convenience: load the active provider's API key.
 * Returns empty string on failure or if no key is configured.
 */
std::string get_active_api_key(std::string& provider_name_out);

/**
 * Active image provider entry (API key, base_url, default_model) from the same store as @c get_active_api_key.
 * @return false if settings could not be loaded.
 */
bool get_active_image_provider(ProviderEntry& out, std::string& provider_name_out);

/**
 * Google / Gemini API key for image transform, meta, and find (LLM mode).
 * Stored under settings.json["providers"]["google"]["api_key"] (same as AI Provider Settings).
 * Used when IMAGE_TRANSFORM_GOOGLE_API_KEY is unset (e.g. Explorer-launched UI where cwd has no .env).
 */
std::string get_google_provider_api_key();

// ---------------------------------------------------------------------------
// Window placement persistence (settings.json)
// ---------------------------------------------------------------------------
// Dock topology (parent, style, size, floating rect) is stored as
// `workbench.main.win32_dock` (legacy: top-level `win32_dock` still read, then
// removed on next save; migration from HKCU\…\Dock Settings unchanged).
// Window placement + panel flags live in `workbench.main.window` (legacy:
// top-level `window` still read, then removed on save).
// ---------------------------------------------------------------------------

/**
 * True when the stored preference is to skip synchronous `LoadDockContainers` inside
 * `LoadDockLayout` and apply it from the main frame after first show instead.
 * Respects `WindowLayout::defer_dock_containers` (see `load_window_layout`). Default is true
 * before any load; not used in non-UI / non-pm-image builds.
 */
bool defer_dock_container_load();

/**
 * Load window layout from settings.json.
 * @param workbench_slot  Key under `workbench` (e.g. from the active workbench’s @c workbenchSettingsId()).
 *                        Legacy top-level `window` is read only when @p workbench_slot is `main`.
 * On first run / missing data, returns default-initialised struct (err stays empty).
 * `has_placement` is set only when `normal_rect` exists and has a sane non-degenerate size (partial
 * `window` objects no longer imply placement).
 */
bool load_window_layout(WindowLayout& out, std::string& err, const char* workbench_slot = "main");

/**
 * Persist window layout to settings.json, preserving all other keys.
 * Writes `workbench.<workbench_slot>.window` and, when @p workbench_slot is `main`, drops legacy top-level `window`.
 */
bool save_window_layout(const WindowLayout& layout, std::string& err, const char* workbench_slot = "main");

/** Sets `filetree_show_shell_frames` for that workbench’s `window` object. */
bool set_filetree_show_shell_frames(bool show, std::string& err, const char* workbench_slot = "main");

/** Sets `filetree_filter_mask` for that workbench's `window` object (e.g. "*.jpg;*.png"). */
bool set_filetree_filter_mask(const std::string& mask, std::string& err, const char* workbench_slot = "main");

/**
 * Optional frame “chrome” under `workbench.<slot>.chrome` (ReBar main menu, Win32++ status bar,
 * own-ribbon strip). Omitted keys default to true. Overrides Win32++ registry frame defaults
 * (see CMainFrame::Create after LoadRegistrySettings).
 */
struct WorkbenchChromeSettings {
    bool show_main_menu   = true;
    bool show_status_bar  = true;
    /// Own-ribbon builds only (`FEATURE_USE_OWN_RIBBON`); ignored when the Windows UIRibbon build is used.
    bool show_ribbon_strip = true;
};

/**
 * Fills @p out from @c workbench[workbench_slot].chrome. Missing @c workbench, slot, or @c chrome
 * uses slot defaults: @c main → all @c true; @c chat → menu/status off with own-ribbon on; @c viewer → all @c true.
 */
bool load_workbench_chrome(WorkbenchChromeSettings& out, std::string& err, const char* workbench_slot = "main");

/**
 * Reads @c settings.json @c ["ui"]["workbench"]. @c "main" (default), @c "chat", or @c "viewer". Unknown / missing / invalid → @c "main".
 * If @ref set_ui_workbench_id_cli_override was set (e.g. @c --ui-preset=chat), that value is returned once and takes precedence.
 */
bool load_settings_ui_workbench_id(std::string& out, std::string& err);

/**
 * Same resolution as @ref load_settings_ui_workbench_id (CLI override if set, else JSON) but never consumes the
 * CLI one-shot. Use before @c CMainFrame construction (e.g. splash monitor) so @ref load_settings_ui_workbench_id
 * in the frame ctor still sees the override.
 */
bool peek_settings_ui_workbench_id(std::string& out, std::string& err);

/** One-shot: next @ref load_settings_ui_workbench_id returns @p id (@c "main", @c "chat", or @c "viewer") and clears. Pass @c nullptr or @c "" to clear without consuming. */
void set_ui_workbench_id_cli_override(const char* id);

/**
 * CLI --app: override viewer kind for the next file opened in viewer workbench.
 * Used to force a specific viewer app (e.g., @c "agent-flow") regardless of file extension.
 * Pass @c nullptr or @c "" to clear.
 */
void set_ui_viewer_app_cli_override(const char* app);

/**
 * Peek the current viewer app CLI override without consuming it.
 * Returns @c true if an override is set (output in @p out).
 */
bool peek_ui_viewer_app_cli_override(std::string& out) noexcept;

/**
 * When @c true (e.g. @c pm-image --ui-reset): for this process only, ignore persisted workbench UI in
 * settings.json — @ref load_window_layout, @ref load_win32_dock_doc, @ref load_workbench_chrome, and file-backed
 * @c ui.workbench (unless @c --ui-preset is set) use built-in defaults for @c main, @c chat, and @c viewer. Other keys (providers, paths, …) still load from disk.
 */
void set_ui_reset_session(bool active) noexcept;
bool ui_reset_session() noexcept;

/**
 * Per-workbench preset for the main window outer size (pixels) when the @c workbench @c window subtree
 * is **absent** (first run / no saved placement). Used by @ref load_window_layout; centered on the primary
 * work area, clamped to the monitor. JSON path: @c workbench.<id>.window_defaults.
 * If @c width / @c height are missing, zero, or invalid, the apply step is skipped.
 * Typical values: 1280×860.
 */
/**
 * Reads @c workbench[workbench_slot].window_defaults from settings.json.
 * Missing object or non-numeric @c width / @c height leaves @p out with zeros (and returns true, empty @p err).
 */
bool load_workbench_window_defaults(WorkbenchWindowDefaults& out, std::string& err, const char* workbench_slot = "main");

// ---------------------------------------------------------------------------
// Appearance (theme + UI font size) — stored under "appearance" in settings.json
// ---------------------------------------------------------------------------

enum class Theme {
    System = 0,   // follow Windows AppsUseLightTheme registry
    Light  = 1,
    Dark   = 2,
};

/**
 * Visual appearance preferences.  Persisted under settings.json["appearance"]
 * alongside provider keys / window layout / prompt presets.
 */
struct AppearanceSettings {
    Theme theme              = Theme::System;
    /**
     * Extra points added on top of the system message font (Segoe UI 9pt by
     * default).  Default `+2` makes the UI noticeably more comfortable on
     * high-DPI laptops without being huge.  Range is clamped to [0, 8].
     */
    int   font_size_extra_pt = 2;
    /**
     * Display language for Win32 resources (ribbon / fallback menu). One of:
     * `en`, `es`, `de`, `it`, `fr`. Applied via `SetThreadUILanguage` at startup;
     * changing it typically requires a restart to refresh cached ribbon UI.
     */
    std::string display_language = "en";
};

/// Load appearance settings (returns defaults on first run).
bool load_appearance(AppearanceSettings& out, std::string& err);

/// Persist appearance settings, preserving every other key in settings.json.
bool save_appearance(const AppearanceSettings& a, std::string& err);

// ---------------------------------------------------------------------------
// Chat provider — `settings.json["chat"]` (in-app agent)
// ---------------------------------------------------------------------------
// Three different concerns share this object; do not conflate them:
//   1) Text (LLM) chat — `router` / `model` (and `timeout_ms`, `max_iterations`) for
//      OpenAI-compatible chat completions (kbot::LLMClient). **API keys and base URLs are not
//      persisted in `settings.json["chat"]`** — they live under `settings.json["providers"][…]`
//      (API Provider Settings). At runtime, the app merges credentials for the selected router
//      from that map. `save_chat_provider` strips legacy `chat.api_key` / `chat.base_url`.
//   2) Image *creation* tools — `image_provider` / `image_model` default which
//      backend (e.g. google, replicate) `image_create`-style tool calls use.
//   2b) Image *recognition* / vision (`image_recognition_provider` / `image_recognition_model`):
//      separate defaults for models used when describing or analyzing images (e.g. vision
//      APIs). Same provider id space as (2). Edited in ChatProviderDlg; persisted here.
//   2c) Video generation (`video_provider` / `video_model`) — defaults for the `create_video`
//      path tool. Same provider id space as (2).
//   3) Replicate (image) — when a row’s provider is "replicate", ChatProviderDlg shows
//      collection + refresh + model (ReplicateSelectorController).
//
// Persisted under settings.json["chat"] beside (but not overlapping) `providers`.
using ChatProviderSettings = media::settings_types::ChatProviderSettings;

/// Load chat-provider settings (returns defaults on first run).
bool load_chat_provider(ChatProviderSettings& out, std::string& err);

/// Persist chat-provider settings, preserving every other key in settings.json.
bool save_chat_provider(const ChatProviderSettings& s, std::string& err);

/**
 * Grouped access to image `providers` and in-app `chat` settings (LLM API keys
 * are under `providers`, not the `chat` object).
 * Thin wrappers for discoverability; see @c load_providers, etc.
 */
struct SettingsApiKeys {
    static std::string image_google() { return get_google_provider_api_key(); }
    static bool image_providers_load(ProviderMap& out, std::string& err) { return media::settings::load_providers(out, err); }
    static bool image_providers_save(const ProviderMap& m, std::string& err) { return media::settings::save_providers(m, err); }
    static bool chat_load(ChatProviderSettings& out, std::string& err) { return media::settings::load_chat_provider(out, err); }
    static bool chat_save(const ChatProviderSettings& s, std::string& err) { return media::settings::save_chat_provider(s, err); }
};

// ---------------------------------------------------------------------------
// Batch sessions — pause / resume / cancel state
// ---------------------------------------------------------------------------
// Stored in a separate sessions.json next to settings.json so large session
// payloads (thousands of paths) never inflate the main settings load/save cycle.
//
// Format: plain UTF-8 JSON array under the top-level "sessions" key.
//   { "sessions": [ { session_id, op, options, items:[...], created_at, updated_at } ] }
//
// FEATURE_ENCRYPT_SESSIONS (cmake -DFEATURE_ENCRYPT_SESSIONS=ON):
//   When defined, load/save use the same PME1 libsodium+DPAPI pipeline as
//   settings.json.  Off by default — opt-in when ready.
// ---------------------------------------------------------------------------

/// Path: <get_config_dir()> / "sessions.json"
std::filesystem::path get_sessions_json_path();

struct SessionItem {
    std::string path;
    std::string sha256;
    std::string status;  ///< "pending" | "done" | "error"
    std::string error;   ///< Non-empty when status == "error"
};

struct PersistedSession {
    std::string              session_id;
    std::string              op;        ///< "resize"|"compress"|"meta"|"transform"|"find"
    nlohmann::json           options;   ///< Full op-opts blob for apply_*_from_json
    std::vector<SessionItem> items;
    std::string              created_at;
    std::string              updated_at;
};

/// Load all sessions from sessions.json.  Returns empty vector when the file
/// does not exist or is empty.  Errors are reported via @p err.
bool load_sessions(std::vector<PersistedSession>& out, std::string& err);

/// Upsert one session (matched by session_id).
/// Creates sessions.json when absent.  Preserves all other sessions.
bool save_session(const PersistedSession& s, std::string& err);

/// Remove the session with the given id.  No-op when not found.
bool delete_session(const std::string& session_id, std::string& err);

/// Convenience: load_sessions, optionally filter by op name (empty = all).
std::vector<PersistedSession> list_sessions(const std::string& op_filter = "");

} // namespace media::settings
