#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace media::assistant::app_batch {

struct RunOptions {
    int default_delay_ms = 50;
    int default_wait_timeout_ms = 5000;
    int default_wait_interval_ms = 100;
    bool stop_on_error = true;
};

bool run_json_batch(const nlohmann::json& doc,
                    const RunOptions& opts,
                    nlohmann::json& report,
                    std::string& err);

} // namespace media::assistant::app_batch
