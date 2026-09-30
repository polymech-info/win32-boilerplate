#pragma once

#include <string>
#include <vector>

namespace media::pixlwiz_cli {

/// Budget and spend information returned by GET /v2/user/info.
struct PixlWizBudgetInfo {
    std::string user_id;
    std::string user_email;
    double      spend       = 0.0;
    /// < 0 means not set (unlimited).
    double      max_budget  = -1.0;
    /// e.g. "monthly", "daily", "weekly" — empty if not configured.
    std::string budget_duration;
    /// ISO-8601 timestamp of next budget reset — empty if not available.
    std::string budget_reset_at;
    /// Allowed model names for this user; empty = all models permitted.
    std::vector<std::string> models;
};

/// One row from GET /spend/logs/v2.
struct PixlWizSpendLogEntry {
    std::string request_id;
    std::string start_time;   ///< ISO-8601
    std::string model;
    double      spend          = 0.0;
    int         total_tokens   = 0;
    int         prompt_tokens  = 0;
    int         completion_tokens = 0;
};

/// GET {base_url}/v2/user/info with Bearer <access_token>.
/// @param base_url  Root like "https://llm.polymech.info" (no trailing slash needed).
/// @returns true on success and fills @p out; false and sets @p err on failure.
bool pixlwiz_get_budget_info(const std::string& access_token,
                              const std::string& base_url,
                              PixlWizBudgetInfo& out,
                              std::string&       err);

/// GET {base_url}/spend/logs/v2?start_date=...&end_date=...&user_id=...
/// @param user_id    ZITADEL sub (numeric string) — the server auto-filters for regular users.
/// @param days_back  How many calendar days to look back from today (default 7).
/// @param page_size  Max entries to return (1–100).
bool pixlwiz_get_spend_logs(const std::string&              access_token,
                             const std::string&              base_url,
                             int                             days_back,
                             int                             page_size,
                             std::vector<PixlWizSpendLogEntry>& out,
                             std::string&                    err);

} // namespace media::pixlwiz_cli
