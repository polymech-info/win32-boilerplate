#include "pm_image_cmd_status.hpp"
#include "pm_image_cli_state.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
#include "core/pixlwiz_user_budget.hpp"
#include "core/settings_runtime.hpp"
#include "lib/pm_zitadel_oauth.hpp"
#endif
#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE) && FEATURE_LICENSE_FILE
#include "win/license_file.hpp"
#endif
#if defined(_WIN32) && defined(FEATURE_TRIAL_CHECK) && FEATURE_TRIAL_CHECK
#include "win/trial_protection.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>

/// Truncate an ISO-8601 timestamp to "YYYY-MM-DD HH:MM" for compact display.
static std::string fmt_iso_short(const std::string& iso) {
    // "2026-05-11T18:23:34.123456Z" -> "2026-05-11 18:23"
    if (iso.size() < 16) return iso;
    std::string s = iso.substr(0, 16);
    if (s[10] == 'T') s[10] = ' ';
    return s;
}

/// Pad or truncate a string to exactly @p w chars (left-aligned).
static std::string col(std::string s, std::size_t w) {
    if (s.size() > w) s.resize(w);
    while (s.size() < w) s += ' ';
    return s;
}

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
static nlohmann::json pixlwiz_budget_json(
    const media::pixlwiz_cli::PixlWizBudgetInfo& info) {
    nlohmann::json j;
    j["enabled"]         = true;
    j["logged_in"]       = true;
    j["user_id"]         = info.user_id;
    j["user_email"]      = info.user_email;
    j["spend"]           = info.spend;
    j["max_budget"]      = (info.max_budget < 0) ? nlohmann::json(nullptr)
                                                  : nlohmann::json(info.max_budget);
    j["budget_duration"] = info.budget_duration;
    j["budget_reset_at"] = info.budget_reset_at;
    j["models"]          = info.models;
    return j;
}

static void print_pixlwiz_budget(
    const media::pixlwiz_cli::PixlWizBudgetInfo& info) {
    std::cout << "Pixlwiz account status\n";
    if (!info.user_email.empty())
        std::cout << "  User:         " << info.user_email << "\n";

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4) << "$" << info.spend;
    if (info.max_budget >= 0.0) {
        ss << " / $" << std::fixed << std::setprecision(2) << info.max_budget;
        if (info.max_budget > 0.0)
            ss << "  (" << std::fixed << std::setprecision(1)
               << (info.spend / info.max_budget * 100.0) << "%)";
    } else {
        ss << " (unlimited)";
    }
    std::cout << "  Spend:        " << ss.str() << "\n";

    if (!info.budget_duration.empty() || !info.budget_reset_at.empty()) {
        std::cout << "  Budget reset: ";
        if (!info.budget_duration.empty())
            std::cout << info.budget_duration;
        if (!info.budget_reset_at.empty())
            std::cout << " (" << info.budget_reset_at << ")";
        std::cout << "\n";
    }

    if (!info.models.empty()) {
        std::cout << "  Models:       ";
        for (std::size_t i = 0; i < info.models.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << info.models[i];
        }
        std::cout << "\n";
    }
}

static int append_pixlwiz_status_json(PmImageCliState& st, nlohmann::json& root) {
    std::string access_token, token_err;
    if (!pm_zitadel_oauth_read_access_token(access_token, token_err) || access_token.empty()) {
        root["pixlwiz"] = {
            {"enabled", true},
            {"logged_in", false},
            {"error", "not logged in (run `pm-image login` first)"}
        };
        return 0;
    }

    std::string api_key_unused, base_url;
    media::runtime_settings::merge_provider_credentials("pixlwiz", false, api_key_unused, base_url);

    media::pixlwiz_cli::PixlWizBudgetInfo info;
    std::string err;
    if (!media::pixlwiz_cli::pixlwiz_get_budget_info(access_token, base_url, info, err)) {
        root["pixlwiz"] = {{"enabled", true}, {"logged_in", true}, {"ok", false}, {"error", err}};
        return 1;
    }

    nlohmann::json j = pixlwiz_budget_json(info);
    j["ok"] = true;

    if (st.pixlwiz_status_log) {
        std::vector<media::pixlwiz_cli::PixlWizSpendLogEntry> logs;
        std::string lerr;
        if (media::pixlwiz_cli::pixlwiz_get_spend_logs(
                access_token, base_url, st.pixlwiz_status_log_days, 50, logs, lerr)) {
            nlohmann::json ja = nlohmann::json::array();
            for (const auto& e : logs) {
                nlohmann::json row;
                row["request_id"]        = e.request_id;
                row["start_time"]        = e.start_time;
                row["model"]             = e.model;
                row["spend"]             = e.spend;
                row["total_tokens"]      = e.total_tokens;
                row["prompt_tokens"]     = e.prompt_tokens;
                row["completion_tokens"] = e.completion_tokens;
                ja.push_back(std::move(row));
            }
            j["spend_logs"] = std::move(ja);
        } else {
            j["spend_logs_error"] = lerr;
        }
    }

    root["pixlwiz"] = std::move(j);
    return 0;
}

static int print_pixlwiz_status(PmImageCliState& st) {
    std::string access_token, token_err;
    if (!pm_zitadel_oauth_read_access_token(access_token, token_err) || access_token.empty()) {
        std::cout << "Pixlwiz account status\n";
        std::cout << "  State:        not logged in (run `pm-image login` first)\n";
        return 0;
    }

    std::string api_key_unused, base_url;
    media::runtime_settings::merge_provider_credentials("pixlwiz", false, api_key_unused, base_url);

    media::pixlwiz_cli::PixlWizBudgetInfo info;
    std::string err;
    if (!media::pixlwiz_cli::pixlwiz_get_budget_info(access_token, base_url, info, err)) {
        std::cerr << "status: " << err << "\n";
        return 1;
    }

    print_pixlwiz_budget(info);

    if (st.pixlwiz_status_log) {
        std::vector<media::pixlwiz_cli::PixlWizSpendLogEntry> logs;
        std::string lerr;
        if (!media::pixlwiz_cli::pixlwiz_get_spend_logs(
                access_token, base_url, st.pixlwiz_status_log_days, 25, logs, lerr)) {
            std::cerr << "status --log: " << lerr << "\n";
            return 1;
        }

        std::cout << "\nSpend log (last " << st.pixlwiz_status_log_days
                  << " day(s), " << logs.size() << " request(s)):\n";
        if (logs.empty()) {
            std::cout << "  (no entries)\n";
        } else {
            std::cout << "  " << col("Time", 16) << "  "
                      << col("Model", 24) << "  "
                      << col("Tokens", 7) << "  Cost\n";
            std::cout << "  " << std::string(16, '-') << "  "
                      << std::string(24, '-') << "  "
                      << std::string(7, '-') << "  ----\n";
            for (const auto& e : logs) {
                std::ostringstream cost;
                cost << std::fixed << std::setprecision(6) << "$" << e.spend;
                std::cout << "  " << col(fmt_iso_short(e.start_time), 16) << "  "
                          << col(e.model, 24) << "  "
                          << col(std::to_string(e.total_tokens), 7) << "  "
                          << cost.str() << "\n";
            }
        }
    }

    return 0;
}
#endif

#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE) && FEATURE_LICENSE_FILE
static void append_license_status_json(nlohmann::json& root) {
    std::string err;
    const bool valid = media::win::license_is_valid(err);
    nlohmann::json j;
    j["enabled"] = true;
    j["valid"] = valid;
    j["path"] = media::win::license_active_storage_path().string();
    if (!valid)
        j["error"] = err;
    root["license"] = std::move(j);
}

static void print_license_status() {
    std::string err;
    const bool valid = media::win::license_is_valid(err);
    std::cout << "License status\n";
    std::cout << "  State:        " << (valid ? "valid" : "not valid") << "\n";
    std::cout << "  Path:         " << media::win::license_active_storage_path().string() << "\n";
    if (!valid && !err.empty())
        std::cout << "  Detail:       " << err << "\n";
}
#endif

#if defined(_WIN32) && defined(FEATURE_TRIAL_CHECK) && FEATURE_TRIAL_CHECK
static const char* trial_state_string(media::win::TrialStatus::State state) {
    switch (state) {
    case media::win::TrialStatus::State::Godmode: return "godmode";
    case media::win::TrialStatus::State::PendingFirstRun: return "pending_first_run";
    case media::win::TrialStatus::State::Active: return "active";
    case media::win::TrialStatus::State::Expired: return "expired";
    case media::win::TrialStatus::State::Invalid: return "invalid";
    }
    return "unknown";
}

static std::string fmt_trial_utc(std::int64_t t) {
    if (t <= 0)
        return "-";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
    return oss.str();
}

static int append_trial_status_json(nlohmann::json& root) {
    media::win::TrialStatus tstat{};
    std::string err;
    if (!media::win::trial_query_status(tstat, err)) {
        root["trial"] = {{"enabled", true}, {"ok", false}, {"error", err}};
        return 1;
    }

    nlohmann::json j;
    j["enabled"] = true;
    j["ok"] = true;
    j["godmode"] = tstat.godmode;
    j["state"] = trial_state_string(tstat.state);
    j["remaining_seconds"] = tstat.remaining_seconds;
    j["remaining_days_approx"] =
        tstat.remaining_seconds > 0
            ? (static_cast<double>(tstat.remaining_seconds) / 86400.0)
            : 0.0;
    j["install_time"] = tstat.install_time;
    j["expiry_time"] = tstat.expiry_time;
    j["last_run_time"] = tstat.last_run_time;
    j["now"] = tstat.now;
    j["trial_id"] = tstat.trial_id;
    if (!tstat.detail.empty())
        j["detail"] = tstat.detail;
    root["trial"] = std::move(j);
    return 0;
}

static int print_trial_status() {
    media::win::TrialStatus tstat{};
    std::string err;
    if (!media::win::trial_query_status(tstat, err)) {
        std::cerr << "status: " << err << "\n";
        return 1;
    }

    std::cout << "Trial status\n";
    if (tstat.godmode || tstat.state == media::win::TrialStatus::State::Godmode) {
        std::cout << "  Mode:         godmode (trial bypass enabled)\n";
    } else {
        std::cout << "  Mode:         trial\n";
        std::cout << "  State:        " << trial_state_string(tstat.state) << "\n";
        if (tstat.state == media::win::TrialStatus::State::Active) {
            const int rs = tstat.remaining_seconds;
            const int days = rs / 86400;
            const int hours = (rs % 86400) / 3600;
            std::cout << "  Remaining:    ~" << days << " day(s) " << hours
                      << " hour(s) (" << rs << " seconds)\n";
        }
        std::cout << "  Install:      " << fmt_trial_utc(tstat.install_time) << "\n";
        std::cout << "  Expires:      " << fmt_trial_utc(tstat.expiry_time) << "\n";
        std::cout << "  Last run:     " << fmt_trial_utc(tstat.last_run_time) << "\n";
        if (!tstat.trial_id.empty())
            std::cout << "  Trial id:     " << tstat.trial_id << "\n";
        if (!tstat.detail.empty())
            std::cout << "  Detail:       " << tstat.detail << "\n";
    }
    return 0;
}
#endif

void pm_image_register_status(CLI::App& app, PmImageCliState& st) {
    st.status_cmd = app.add_subcommand(
        "status",
        "Show Pixlwiz credits plus license/trial status when those features are enabled.");
    st.pixlwiz_status_cmd = st.status_cmd;
    st.status_cmd->add_flag("--json", st.status_json,
                            "Print machine-readable JSON on stdout.");
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    st.status_cmd->add_flag("--log", st.pixlwiz_status_log,
                            "Show recent Pixlwiz spend log entries.");
    st.status_cmd
        ->add_option("--log-days", st.pixlwiz_status_log_days,
                     "Number of days to look back for --log (default 7).")
        ->check(CLI::Range(1, 365));
#endif
}

int pm_image_cmd_status_pixlwiz(CLI::App& /*app*/, PmImageCliState& st) {
    const bool json = st.status_json || st.pixlwiz_status_json;
    int rc = 0;

    if (json) {
        nlohmann::json root = nlohmann::json::object();
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
        rc = std::max(rc, append_pixlwiz_status_json(st, root));
#endif
#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE) && FEATURE_LICENSE_FILE
        append_license_status_json(root);
#endif
#if defined(_WIN32) && defined(FEATURE_TRIAL_CHECK) && FEATURE_TRIAL_CHECK
        rc = std::max(rc, append_trial_status_json(root));
#endif
        std::cout << root.dump(2) << "\n";
        return rc;
    }

    bool printed = false;
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    rc = std::max(rc, print_pixlwiz_status(st));
    printed = true;
#endif
#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE) && FEATURE_LICENSE_FILE
    if (printed) std::cout << "\n";
    print_license_status();
    printed = true;
#endif
#if defined(_WIN32) && defined(FEATURE_TRIAL_CHECK) && FEATURE_TRIAL_CHECK
    if (printed) std::cout << "\n";
    rc = std::max(rc, print_trial_status());
    printed = true;
#endif
    if (!printed)
        std::cout << "status: no status providers enabled in this build\n";
    return rc;
}
