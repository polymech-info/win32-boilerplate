// C API for Swift / AppKit: JSON in → JSON out; same options as `apply_*_from_json` in src/core.
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Frees a buffer returned by the functions below (use `free` on macOS). */
void pm_polymech_free(void* ptr);

/** macOS: one-time spdlog stderr + Desktop `pm-image.log` (or `PM_IMAGE_LOG_DIR`); no-op on other platforms. Call from app launch before native image APIs. */
void pm_polymech_init_process_logging(void);

/**
 * Register a callback that receives every formatted spdlog line (UTF-8, no trailing newline).
 * Thread-safe. The callback is invoked from whichever thread spdlog fires on — callers must
 * dispatch to the main thread if they update UI. Pass NULL to unregister.
 * Installs the internal spdlog UI sink on first call (idempotent).
 */
typedef void (*pm_log_callback_t)(const char* line_utf8, void* ctx);
void pm_polymech_set_log_callback(pm_log_callback_t callback, void* ctx);

/**
 * @return 0 on success (`out_json` is set, UTF-8). Non-zero: `err_out` message; `out_json` may be null.
 *
 * @param json_in UTF-8. Operation-specific:
 *  resize:    { "input":"path", "output":"path", "options": { ... } }  — @see apply_resize_options_from_json
 *  meta:      { "input":"path", "options": { ... } }  — @see apply_meta_options_from_json
 *  compress:  { "input":"path", "output":"path", "options": { ... } }  — @see apply_compress_options_from_json
 *  find:      { "inputs": ["path"…], "options": { ... } }  — @see apply_find_options_from_json
 *  transform: { "input":"path", "output":"path|null", "options": { ... } }  — @see apply_transform_options_from_json
 */
int32_t pm_polymech_resize(const char* json_in, char** out_json, char** err_out);
int32_t pm_polymech_meta(const char* json_in, char** out_json, char** err_out);
int32_t pm_polymech_compress(const char* json_in, char** out_json, char** err_out);
int32_t pm_polymech_find(const char* json_in, char** out_json, char** err_out);
int32_t pm_polymech_transform(const char* json_in, char** out_json, char** err_out);
int32_t pm_polymech_chat_turn(const char* json_in, char** out_json, char** err_out);
/** @see `media::replicate_cli` — `op`: `collections` | `models` | `resolve_collection`. */
int32_t pm_polymech_replicate(const char* json_in, char** out_json, char** err_out);

/** OpenRouter text catalog — `op`: `models` (same fields as Win32 `OpenRouterModelInfo` / chat-web `openrouter` RPC). */
int32_t pm_polymech_openrouter(const char* json_in, char** out_json, char** err_out);
/**
 * Pixlwiz service: POST /api/posts + upload images (same as `pm-image service posts create`).
 * JSON: { "images": ["path"…], "title"?: string, "description"?: string,
 *         "visibility"?: "public"|"listed"|"private", "server_url"?: string }.
 * Out: { "ok": true, "post_id", "picture_ids", "view_url" } or ok:false + error.
 */
int32_t pm_polymech_service_create_post(const char* json_in, char** out_json, char** err_out);

/**
 * Probe mcp.json (same as `pm-image llm info --json`): connect to each configured MCP server,
 * run `initialize` + `tools/list`, and return a slim catalog for the chat web sidebar.
 * JSON in: {} (empty object; no fields required)
 * JSON out: {
 *   "servers": [ { "name", "transport"?, "handshake_ok", "tools_list_ok",
 *                  "skipped"?, "skip_reason"?, "tools": [{"name","description"}] } ],
 *   "mcp_json_path": string,
 *   "exists": bool
 * }
 * On exception: { "servers": [], "exists": false, "probe_error": string }
 * Parity with Win32 ChatWebPanel `chat_web_start_mcp_catalog_probe`.
 */
int32_t pm_polymech_mcp_probe(const char* json_in, char** out_json, char** err_out);

#ifdef __cplusplus
}
#endif
