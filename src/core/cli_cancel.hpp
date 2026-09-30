#pragma once
//
// Shared Ctrl+C (SIGINT) / console interrupt flag for long-running CLI work
// (batch resume, `llm agent` tool calls). Install once in main; tools check
// between I/O-bound steps and yield briefly so the handler can run.
//
#include <atomic>
#include <thread>

namespace media::cli {

/// True after the user requested interrupt (console Ctrl+C, POSIX SIGINT).
[[nodiscard]] bool cancel_requested() noexcept;

/// For tests; production uses the OS signal/console handler.
void test_request_cancel();
void test_clear_cancel();

/// Yields the current thread — call before heavy work in tight loops so Ctrl+C
/// and the interrupt flag are observed without starving the system.
inline void yield_to_interrupt() noexcept
{
    std::this_thread::yield();
}

/// SetConsoleCtrlHandler (Win) and/or SIGINT (POSIX). Call once at process start.
void install_cli_interrupt_handlers() noexcept;

} // namespace media::cli
