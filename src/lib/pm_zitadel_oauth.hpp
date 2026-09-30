#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

/** `%APPDATA%/.../zitadel-oauth.json` (Windows); beside portable `settings.json` on macOS / Linux / Unix. */
std::filesystem::path pm_zitadel_oauth_json_path(std::string& err);

/** Read `access_token` from that file (JSON written by `pm-image login`). */
bool pm_zitadel_oauth_read_access_token(std::string& token_out, std::string& err);

/**
 * Read `app_user_id` (resolved app UUID from GET /api/me/identity, same as pm-pics `fetchUserIdentity().id`).
 * Returns false if missing or not a non-empty string (e.g. login ran without SERVER_URL).
 */
bool pm_zitadel_oauth_read_app_user_id(std::string& uuid_out, std::string& err);

/**
 * Sanitized fields for UI / WebView2: logged_in, oauth_file_present, app_user_id, zitadel_sub,
 * has_refresh_token, expires_in_sec, obtained_at_unix, access_token_expired_est (when timestamps
 * allow). Never includes access_token or refresh_token.
 * On success @p out is filled; @p err is only set for path resolution / read / parse failures.
 */
bool pm_zitadel_oauth_read_summary_json(nlohmann::json& out, std::string& err);

/** Delete `zitadel-oauth.json` if it exists (sign out). Returns true if absent or removed. */
bool pm_zitadel_oauth_clear(std::string& err);
