#pragma once
//
// media::llm::agent::AgentScheduler — background thread that fires due tasks.
//
// Usage:
//   auto& sched = global_scheduler();
//   sched.configure(provider);
//   sched.start();
//   // …
//   sched.stop();
//
// The scheduler wakes every second, checks which enabled tasks have
// next_run_at <= now, builds a Turn, calls run_turn, appends an event,
// and updates next_run_at (or disables one-shot tasks).
//
#include "agent.hpp"
#include "agent_memory.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace media::llm::agent {

class AgentScheduler {
public:
    AgentScheduler();
    ~AgentScheduler();

    /// Factory called once per tick to get fresh provider settings (reads live settings.json).
    /// Set before calling start(); may be re-set at any time (mutex-protected).
    using ProviderFactory = std::function<ProviderSettings()>;
    void configure(ProviderFactory   factory,
                   EventCallback     on_tick_event = nullptr);

    void start();
    void stop();

    bool is_running() const noexcept { return running_.load(); }

    AgentScheduler(const AgentScheduler&)            = delete;
    AgentScheduler& operator=(const AgentScheduler&) = delete;

private:
    void loop();
    void run_due_task(ScheduledTask task);

    mutable std::mutex config_mutex_;
    ProviderFactory    factory_;
    EventCallback      on_event_;

    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::thread       thread_;
};

/// Process-wide singleton. Configure + start once at app startup.
AgentScheduler& global_scheduler();

} // namespace media::llm::agent
