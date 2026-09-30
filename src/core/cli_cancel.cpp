#include "cli_cancel.hpp"

#if !defined(_WIN32)
#  include <csignal>
#endif
#include <thread>

namespace media::cli {

static std::atomic<bool> g_cancel{false};

bool cancel_requested() noexcept
{
    return g_cancel.load(std::memory_order_acquire);
}

void test_request_cancel()
{
    g_cancel.store(true, std::memory_order_release);
}

void test_clear_cancel()
{
    g_cancel.store(false, std::memory_order_release);
}

#if defined(_WIN32)

#  include <windows.h>

static BOOL WINAPI win_ctrl_handler(DWORD type)
{
    g_cancel.store(true, std::memory_order_release);
    // CTRL_CLOSE_EVENT: console window closing — return TRUE so we get ~5 s to
    // unwind; callers must observe cancel_requested() and exit promptly.
    (void)type;
    return TRUE;
}

static void win_console_detach_watchdog() noexcept
{
    if (GetConsoleWindow() == nullptr)
        return;
    std::thread([] {
        while (!g_cancel.load(std::memory_order_acquire)) {
            if (GetConsoleWindow() == nullptr) {
                g_cancel.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }).detach();
}

void install_cli_interrupt_handlers() noexcept
{
    g_cancel.store(false, std::memory_order_release);
    if (GetConsoleWindow() == nullptr)
        (void)AttachConsole(ATTACH_PARENT_PROCESS);
    (void)SetConsoleCtrlHandler(win_ctrl_handler, TRUE);
    win_console_detach_watchdog();
}

#else

static void on_sigint(int)
{
    g_cancel.store(true, std::memory_order_release);
}

void install_cli_interrupt_handlers() noexcept
{
    g_cancel.store(false, std::memory_order_release);
    // Use sigaction with SA_RESETHAND=no and SA_RESTART=no (SA_INTERRUPT on BSD).
    // Without SA_INTERRUPT, blocking syscalls (mach_msg, kevent, CFRunLoopRunInMode)
    // would restart after the signal handler returns and never wake the run loop.
    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    // SA_INTERRUPT ensures blocked calls return EINTR instead of restarting.
#if defined(SA_INTERRUPT)
    sa.sa_flags = SA_INTERRUPT;
#else
    sa.sa_flags = 0; // POSIX: no SA_RESTART → same effect
#endif
    sigaction(SIGINT, &sa, nullptr);
}

#endif

} // namespace media::cli
