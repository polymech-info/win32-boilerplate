#pragma once

#include <nlohmann/json.hpp>

namespace pmui::pixlwiz_auth {

/// OAuth summary for UI surfaces. Never includes access or refresh tokens.
nlohmann::json read_auth_summary_payload();

/// Pixlwiz LiteLLM budget payload. Shape: { ok, spend?, max_budget?, budget_duration?, budget_reset_at?, error? }.
nlohmann::json read_credit_payload();

/// Settings-friendly combined payload: auth summary plus budget_ok / budget_error fields.
nlohmann::json read_settings_status_payload();

/// Host message for chat/settings-style push channels: { kind:"hostPixlwizAuth", ...auth summary }.
nlohmann::json make_host_auth_message();

/// Host message for chat/settings-style push channels: { kind:"hostPixlwizCredit", ...credit payload }.
nlohmann::json make_host_credit_message(const nlohmann::json& credit_payload);

} // namespace pmui::pixlwiz_auth
