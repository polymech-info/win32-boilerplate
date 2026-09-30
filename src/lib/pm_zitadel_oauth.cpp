#include "lib/pm_zitadel_oauth.hpp"
#include "core/settings_runtime.hpp"
#include "core/settings_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

std::filesystem::path pm_zitadel_oauth_json_path(std::string& err)
{
    err.clear();
    try {
        return media::settings::get_config_dir() / "zitadel-oauth.json";
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
}

bool pm_zitadel_oauth_read_access_token(std::string& token_out, std::string& err)
{
    token_out.clear();
    err.clear();
    std::string perr;
    const std::filesystem::path p = pm_zitadel_oauth_json_path(perr);
    if (p.empty()) {
        err = perr.empty() ? "zitadel-oauth.json path unavailable" : perr;
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        err = "zitadel-oauth.json not found at " + p.string() + " (run `pm-image login` first)";
        return false;
    }
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        err = "cannot read " + p.string();
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    try {
        const nlohmann::json j = nlohmann::json::parse(oss.str());
        if (!j.contains("access_token") || !j["access_token"].is_string()) {
            err = "zitadel-oauth.json missing access_token string";
            return false;
        }
        token_out = j["access_token"].get<std::string>();
        if (token_out.empty()) {
            err = "access_token is empty in zitadel-oauth.json";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse zitadel-oauth.json: ") + e.what();
        return false;
    }
}

bool pm_zitadel_oauth_clear(std::string& err)
{
    err.clear();
    std::string perr;
    const std::filesystem::path p = pm_zitadel_oauth_json_path(perr);
    if (p.empty()) {
        err = perr.empty() ? "zitadel-oauth.json path unavailable" : perr;
        return false;
    }
    std::error_code ec;
    // Missing file = already signed out (do not treat `is_regular_file` "not found" as failure).
    if (!std::filesystem::exists(p, ec)) {
        if (ec) {
            err = ec.message();
            return false;
        }
        return true;
    }
    if (!std::filesystem::is_regular_file(p, ec)) {
        err = "zitadel-oauth.json path is not a regular file: " + p.string();
        return false;
    }
    (void)std::filesystem::remove(p, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    // Clear the mirrored key in settings.json (Option A logout counterpart).
    {
        std::string serr;
        media::runtime_settings::update_pixlwiz_api_key("", serr);
        // Non-fatal: the key will be stale but merge_provider_credentials won't
        // find a valid zitadel-oauth.json anymore, so LLM calls will fail cleanly.
    }
    return true;
}

bool pm_zitadel_oauth_read_summary_json(nlohmann::json& out, std::string& err)
{
    out                = nlohmann::json::object();
    out["logged_in"]   = false;
    out["oauth_file_present"] = false;
    out["has_refresh_token"]  = false;
    err.clear();

    std::string perr;
    const std::filesystem::path p = pm_zitadel_oauth_json_path(perr);
    if (p.empty()) {
        err = perr.empty() ? "zitadel-oauth.json path unavailable" : perr;
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return true;
    }
    out["oauth_file_present"] = true;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        err = "cannot read " + p.string();
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(oss.str());
    } catch (const std::exception& e) {
        err = std::string("parse zitadel-oauth.json: ") + e.what();
        return false;
    }

    bool has_at = j.contains("access_token") && j["access_token"].is_string()
                  && !j["access_token"].get<std::string>().empty();
    out["logged_in"] = has_at;

    if (j.contains("refresh_token") && j["refresh_token"].is_string()
        && !j["refresh_token"].get<std::string>().empty())
        out["has_refresh_token"] = true;

    if (j.contains("app_user_id") && j["app_user_id"].is_string())
        out["app_user_id"] = j["app_user_id"].get<std::string>();
    if (j.contains("zitadel_sub") && j["zitadel_sub"].is_string())
        out["zitadel_sub"] = j["zitadel_sub"].get<std::string>();
    if (j.contains("roles") && j["roles"].is_array())
        out["roles"] = j["roles"];

    if (j.contains("obtained_at_unix") && j["obtained_at_unix"].is_number_integer()) {
        const std::int64_t obt = j["obtained_at_unix"].get<std::int64_t>();
        out["obtained_at_unix"] = obt;
        if (j.contains("expires_in") && j["expires_in"].is_number_integer()) {
            const std::int64_t expSec = j["expires_in"].get<std::int64_t>();
            out["expires_in_sec"] = expSec;
            const std::time_t now = std::time(nullptr);
            if (now != static_cast<std::time_t>(-1) && expSec > 0)
                out["access_token_expired_est"] = (obt + expSec < static_cast<std::int64_t>(now));
        }
    }
    return true;
}

bool pm_zitadel_oauth_read_app_user_id(std::string& uuid_out, std::string& err)
{
    uuid_out.clear();
    err.clear();
    std::string perr;
    const std::filesystem::path p = pm_zitadel_oauth_json_path(perr);
    if (p.empty()) {
        err = perr.empty() ? "zitadel-oauth.json path unavailable" : perr;
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        err = "zitadel-oauth.json not found at " + p.string();
        return false;
    }
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        err = "cannot read " + p.string();
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    try {
        const nlohmann::json j = nlohmann::json::parse(oss.str());
        if (!j.contains("app_user_id") || !j["app_user_id"].is_string()) {
            err = "zitadel-oauth.json missing app_user_id (re-run login with SERVER_URL set)";
            return false;
        }
        uuid_out = j["app_user_id"].get<std::string>();
        if (uuid_out.empty()) {
            err = "app_user_id is empty in zitadel-oauth.json";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse zitadel-oauth.json: ") + e.what();
        return false;
    }
}
