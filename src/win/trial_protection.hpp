#pragma once

#include <cstdint>
#include <string>

#if defined(FEATURE_TRIAL_CHECK)

namespace media::win {

/// Snapshot for `pm-image status` (read-only; does not update last_run or persist).
struct TrialStatus {
    enum class State {
        Godmode,         ///< `godmod` bypass enabled
        PendingFirstRun, ///< No trial blob yet (next normal launch will start trial)
        Active,          ///< Within 14-day window
        Expired,         ///< Valid payload but past install_time + 14d
        Invalid          ///< Tamper / HMAC / mismatch / machine / clock rollback
    };
    State state = State::PendingFirstRun;
    bool godmode = false;
    std::int64_t install_time = 0; ///< unix seconds
    std::int64_t last_run_time = 0;
    std::int64_t expiry_time = 0; ///< install_time + 14d
    std::int64_t now = 0;
    int remaining_seconds = 0; ///< max(0, expiry - now) when Active; 0 otherwise
    std::string trial_id;
    std::string detail; ///< non-empty when state == Invalid (or extra context)
};

/// Load and verify trial stores without writing. Returns false only on fatal errors (e.g. sodium).
bool trial_query_status(TrialStatus& out, std::string& err_out);

/// Returns true if HKCU\Software\PolyMech\pm-image "Gdm" is set (hidden `godmod` command).
bool trial_is_godmode();

/// Enable trial bypass for the current Windows user profile (persists in registry).
bool trial_set_godmode(bool enable, std::string& err_out);

/// Remove registry trial blob, godmode flag, hidden file, and ADS (hidden `purgetrial` command).
bool trial_purge_all(std::string& err_out);

/**
 * Enforce 14-day trial: load/verify HMAC payload from registry + hidden file (+ optional ADS).
 * On success updates last_run_time and rewrites stores (registry write may be delayed).
 * @return true if the app may run; false if blocked (message for stderr).
 */
bool trial_enforce_or_exit(std::string& message_out);

} // namespace media::win

#endif // FEATURE_TRIAL_CHECK
