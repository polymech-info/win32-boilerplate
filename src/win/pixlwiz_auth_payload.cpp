#include "win/pixlwiz_auth_payload.hpp"

#include "core/pixlwiz_user_budget.hpp"
#include "core/settings_runtime.hpp"
#include "lib/pm_zitadel_oauth.hpp"

namespace pmui::pixlwiz_auth {

nlohmann::json read_auth_summary_payload()
{
    std::string err;
    nlohmann::json summary = nlohmann::json::object();
    const bool ok = pm_zitadel_oauth_read_summary_json(summary, err);

    nlohmann::json data = nlohmann::json::object();
    data["read_ok"] = ok;
    if (!ok)
        data["read_error"] = err;
    for (auto it = summary.begin(); it != summary.end(); ++it)
        data[it.key()] = it.value();
    return data;
}

nlohmann::json read_credit_payload()
{
    std::string access_token;
    std::string token_err;
    if (!pm_zitadel_oauth_read_access_token(access_token, token_err) || access_token.empty()) {
        return nlohmann::json{
            {"ok", false},
            {"error", token_err.empty() ? "not logged in" : token_err},
        };
    }

    std::string api_key_unused;
    std::string base_url;
    media::runtime_settings::merge_provider_credentials("pixlwiz", false, api_key_unused, base_url);

    media::pixlwiz_cli::PixlWizBudgetInfo info{};
    std::string err;
    if (!media::pixlwiz_cli::pixlwiz_get_budget_info(access_token, base_url, info, err)) {
        return nlohmann::json{
            {"ok", false},
            {"error", err.empty() ? "pixlwiz budget fetch failed" : err},
        };
    }

    return nlohmann::json{
        {"ok", true},
        {"spend", info.spend},
        {"max_budget", (info.max_budget >= 0.0) ? nlohmann::json(info.max_budget) : nlohmann::json(nullptr)},
        {"budget_duration", info.budget_duration},
        {"budget_reset_at", info.budget_reset_at},
    };
}

nlohmann::json read_settings_status_payload()
{
    nlohmann::json data = read_auth_summary_payload();
    const nlohmann::json credit = read_credit_payload();
    if (credit.value("ok", false)) {
        data["budget_ok"] = true;
        data["spend"] = credit.value("spend", 0.0);
        data["max_budget"] = credit.contains("max_budget") ? credit["max_budget"] : nlohmann::json(nullptr);
        data["budget_duration"] = credit.value("budget_duration", std::string{});
        data["budget_reset_at"] = credit.value("budget_reset_at", std::string{});
    } else {
        data["budget_ok"] = false;
        data["budget_error"] = credit.value("error", std::string{});
    }
    return data;
}

nlohmann::json make_host_auth_message()
{
    nlohmann::json msg = read_auth_summary_payload();
    msg["kind"] = "hostPixlwizAuth";
    return msg;
}

nlohmann::json make_host_credit_message(const nlohmann::json& credit_payload)
{
    nlohmann::json msg = nlohmann::json::object();
    msg["kind"] = "hostPixlwizCredit";
    msg["ok"] = credit_payload.value("ok", false);
    if (msg["ok"].get<bool>()) {
        msg["spend"] = credit_payload.value("spend", 0.0);
        msg["max_budget"] = credit_payload.contains("max_budget") ? credit_payload["max_budget"] : nlohmann::json(nullptr);
    } else {
        msg["error"] = credit_payload.value("error", std::string{"unknown"});
    }
    return msg;
}

} // namespace pmui::pixlwiz_auth
