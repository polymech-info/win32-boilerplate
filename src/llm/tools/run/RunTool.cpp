#include "RunTool.hpp"

#include "core/cli_cancel.hpp"
#include "llm/llm_fs_guard.hpp"
#include "logging/pm_log.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace media::llm::run {

// ═══════════════════════════════════════════════════════════════════════════════
// §1  Cancellation atomics (cross-thread readable from the UI stop handler)
// ═══════════════════════════════════════════════════════════════════════════════

#if defined(_WIN32)
static std::atomic<void*>        g_run_process{nullptr};
static std::atomic<unsigned long> g_run_pid{0};
#else
static std::atomic<int>          g_run_pid{0};
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// §1b  Streaming output hook (set by agent_factory; called from reader threads)
// ═══════════════════════════════════════════════════════════════════════════════

static std::mutex       g_hook_mtx;
static OutputChunkHook  g_chunk_hook;

void set_output_chunk_hook(OutputChunkHook hook) {
    std::lock_guard<std::mutex> lk(g_hook_mtx);
    g_chunk_hook = std::move(hook);
}

void clear_output_chunk_hook() {
    std::lock_guard<std::mutex> lk(g_hook_mtx);
    g_chunk_hook = nullptr;
}

// §2  Command Validators — see RunValidators.hpp / RunValidators.cpp

namespace {

constexpr const char k_ch[] = "run_tool";

// ═══════════════════════════════════════════════════════════════════════════════
// §3  Shell detection helpers
// ═══════════════════════════════════════════════════════════════════════════════

#if defined(_WIN32)

void terminate_process_tree(DWORD pid) {
    if (pid == 0) return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid)
                    terminate_process_tree(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h) return;
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 1000);
    CloseHandle(h);
}

std::string env_or(const char* var, const char* fallback) {
    const char* v = std::getenv(var);
    return (v && v[0]) ? std::string(v) : std::string(fallback);
}

bool file_exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::path(p), ec);
}

std::wstring widen_utf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring ws(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), ws.data(), n);
    return ws;
}

std::wstring getenv_w(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(needed, L'\0');
    const DWORD n = GetEnvironmentVariableW(name, value.data(), needed);
    if (n == 0) return {};
    value.resize(n);
    return value;
}

bool path_contains_part(const std::wstring& path, const std::wstring& part) {
    if (path.empty() || part.empty()) return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(L';', start);
        std::wstring item = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        std::wstring cmp = part;
        while (!item.empty() && (item.back() == L'\\' || item.back() == L'/')) item.pop_back();
        while (!cmp.empty() && (cmp.back() == L'\\' || cmp.back() == L'/')) cmp.pop_back();
        if (!item.empty() && CompareStringOrdinal(item.c_str(), -1, cmp.c_str(), -1, TRUE) == CSTR_EQUAL)
            return true;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return false;
}

void prepend_path_part(std::wstring& path, const std::wstring& part) {
    if (part.empty() || path_contains_part(path, part)) return;
    path = path.empty() ? part : part + L";" + path;
}

std::wstring exe_adjacent_bin_dir() {
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    const size_t slash = buf.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    std::wstring bin = buf.substr(0, slash + 1) + L"bin";
    const DWORD attrs = GetFileAttributesW(bin.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return bin;
}

std::vector<wchar_t> build_run_environment_block() {
    std::vector<std::wstring> entries;
    bool has_path = false;
    std::wstring path = getenv_w(L"Path");
    if (path.empty()) path = getenv_w(L"PATH");
    prepend_path_part(path, exe_adjacent_bin_dir());

    if (LPWCH env = GetEnvironmentStringsW()) {
        for (const wchar_t* p = env; p && *p;) {
            std::wstring entry(p);
            const size_t eq = entry.find(L'=');
            if (eq != std::wstring::npos && _wcsicmp(entry.substr(0, eq).c_str(), L"Path") == 0) {
                entries.push_back(L"Path=" + path);
                has_path = true;
            } else {
                entries.push_back(std::move(entry));
            }
            p += wcslen(p) + 1;
        }
        FreeEnvironmentStringsW(env);
    }
    if (!has_path && !path.empty())
        entries.push_back(L"Path=" + path);

    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

ShellSpec detect_windows(const std::string& hint) {
    if (hint == "cmd")
        return { env_or("SystemRoot", "C:\\Windows") + "\\System32\\cmd.exe", "/c", false };

    if (hint == "bash" || hint == "sh") {
        const std::string git_bash = env_or("ProgramFiles", "C:\\Program Files") +
            "\\Git\\bin\\bash.exe";
        if (file_exists(git_bash))
            return { git_bash, "-c", false };
        return { "bash", "-c", false };
    }

    if (hint == "auto" || hint == "pwsh") {
        const std::string pf = env_or("ProgramFiles", "C:\\Program Files");
        const std::string pwsh7 = pf + "\\PowerShell\\7\\pwsh.exe";
        if (file_exists(pwsh7))
            return { pwsh7, "-NoProfile -NonInteractive -Command", true };

        const std::string sys = env_or("SystemRoot", "C:\\Windows");
        const std::string ps5 = sys + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (file_exists(ps5))
            return { ps5, "-NoProfile -NonInteractive -Command", true };

        if (hint == "pwsh")
            return { "pwsh", "-NoProfile -NonInteractive -Command", true };

        return { sys + "\\System32\\cmd.exe", "/c", false };
    }

    return { "cmd.exe", "/c", false };
}

#else

ShellSpec detect_unix(const std::string& hint) {
    if (hint == "bash")
        return { "/bin/bash", "-c", false };
    if (hint == "sh")
        return { "/bin/sh", "-c", false };
    if (hint == "pwsh")
        return { "pwsh", "-NoProfile -NonInteractive -Command", true };

    const char* sh = std::getenv("SHELL");
    if (sh && sh[0] == '/') {
        std::error_code ec;
        if (fs::exists(fs::path(sh), ec))
            return { sh, "-c", false };
    }
    if (fs::exists("/bin/bash"))
        return { "/bin/bash", "-c", false };
    return { "/bin/sh", "-c", false };
}

#endif

// ═══════════════════════════════════════════════════════════════════════════════
// §4  Output helpers
// ═══════════════════════════════════════════════════════════════════════════════

constexpr int    k_timeout_default_ms = 30000;
constexpr int    k_timeout_max_ms     = 120000;
constexpr size_t k_output_max_bytes   = 128 * 1024;

std::string normalize_crlf(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

std::string tail_truncate(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    constexpr const char kTrunc[] = "\n\xe2\x80\xa6 [output truncated; showing last 128 KB] \xe2\x80\xa6\n";
    return std::string(kTrunc) + s.substr(s.size() - max_bytes + sizeof(kTrunc));
}

media::llm::path::ExecuteResult fatal(const std::string& msg) {
    media::llm::path::ExecuteResult r;
    r.ok = false;
    r.error = msg;
    r.envelope = nlohmann::json{{"ok", false}, {"error", msg}};
    return r;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// §6  Public API — detect_shell
// ═══════════════════════════════════════════════════════════════════════════════

ShellSpec detect_shell(const std::string& hint) {
    std::string h = hint;
    std::transform(h.begin(), h.end(), h.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (h.empty()) h = "auto";

#if defined(_WIN32)
    auto spec = detect_windows(h);
#else
    auto spec = detect_unix(h);
#endif

    pm::log::debug_lazy(k_ch, [&] {
        return "detect_shell hint=\"" + hint + "\" exe=\"" + spec.exe
            + "\" flag=\"" + spec.flag + "\"";
    });
    return spec;
}

// ═══════════════════════════════════════════════════════════════════════════════
// §7  Public API — spawn_shell_process
// ═══════════════════════════════════════════════════════════════════════════════

#if defined(_WIN32)

SpawnResult spawn_shell_process(const ShellSpec& shell,
                                const std::string& command,
                                const std::string& cwd) {
    SpawnResult sr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        sr.error = "CreatePipe(stdout) failed";
        return sr;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        sr.error = "CreatePipe(stderr) failed";
        return sr;
    }
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdoutWrite;
    si.hStdError  = hStderrWrite;
    si.hStdInput  = INVALID_HANDLE_VALUE;
    si.wShowWindow = SW_HIDE;

    std::wstring cmdline;
    {
        cmdline = L"\"" + widen_utf8(shell.exe) + L"\" " + widen_utf8(shell.flag) + L" \"";
        std::wstring wcmd = widen_utf8(command);
        for (wchar_t c : wcmd) {
            if (c == L'"') cmdline += L"\\\"";
            else cmdline += c;
        }
        cmdline += L"\"";
    }

    std::wstring wcwd;
    if (!cwd.empty()) {
        wcwd = widen_utf8(cwd);
    }

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> env_block = build_run_environment_block();
    BOOL ok = CreateProcessW(
        nullptr, cmdline.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
        env_block.empty() ? nullptr : env_block.data(), cwd.empty() ? nullptr : wcwd.c_str(),
        &si, &pi);

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!ok) {
        DWORD err = GetLastError();
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        sr.error = "CreateProcessW failed, error=" + std::to_string(err);
        return sr;
    }

    CloseHandle(pi.hThread);

    sr.ok = true;
    sr.process_handle = pi.hProcess;
    sr.pid = pi.dwProcessId;
    sr.stdout_read = hStdoutRead;
    sr.stderr_read = hStderrRead;

    pm::log::debug_lazy(k_ch, [&] {
        return "spawn pid=" + std::to_string(sr.pid) + " exe=\"" + shell.exe + "\"";
    });
    return sr;
}

#else // POSIX

SpawnResult spawn_shell_process(const ShellSpec& shell,
                                const std::string& command,
                                const std::string& cwd) {
    SpawnResult sr;

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (pipe(stdout_pipe) != 0) { sr.error = "pipe(stdout) failed"; return sr; }
    if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        sr.error = "pipe(stderr) failed";
        return sr;
    }

    pid_t child = fork();
    if (child < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        sr.error = "fork() failed";
        return sr;
    }

    if (child == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        close(STDIN_FILENO);

        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0)
                _exit(127);
        }

        execlp(shell.exe.c_str(), shell.exe.c_str(), "-c", command.c_str(), nullptr);
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    sr.ok = true;
    sr.pid = child;
    sr.stdout_fd = stdout_pipe[0];
    sr.stderr_fd = stderr_pipe[0];
    return sr;
}

#endif

// ═══════════════════════════════════════════════════════════════════════════════
// §8  Public API — execute  (the full do_run lifecycle)
// ═══════════════════════════════════════════════════════════════════════════════

media::llm::path::ExecuteResult
execute(const nlohmann::json& args,
        const std::string& agent_path_base) {
    namespace chr = std::chrono;

    const std::string command    = args.value("command", std::string{});
    const std::string shell_hint = args.value("shell", std::string{"auto"});
    int timeout_ms = args.value("timeout_ms", k_timeout_default_ms);
    if (timeout_ms < 1)              timeout_ms = k_timeout_default_ms;
    if (timeout_ms > k_timeout_max_ms) timeout_ms = k_timeout_max_ms;

    // ── 1. Validate ──
    const bool godmode = media::llm::path::get_agent_godmode();
    const auto vr = validate_command(command, shell_hint);
    if (!godmode && vr.tier == ValidationTier::Deny) {
        pm::log::warn_lazy(k_ch, [&] { return "deny: " + vr.reason + " cmd=\"" + command.substr(0, 80) + "\""; });
        return fatal("run: blocked \xe2\x80\x94 " + vr.reason);
    }
    if (vr.tier == ValidationTier::Warn) {
        pm::log::warn_lazy(k_ch, [&] { return "warn (allowed): " + vr.reason; });
    }

    // ── 2. CWD ──
    const std::string cwd = agent_path_base.empty()
        ? fs::current_path().string()
        : agent_path_base;

    if (!godmode && !cwd.empty()) {
        const std::string deny = media::llm::llm_fs_guard_deny_reason(fs::path(cwd));
        if (!deny.empty())
            return fatal("run: cwd blocked \xe2\x80\x94 " + deny);
    }

    // ── 3. Shell detect ──
    const auto shell = detect_shell(shell_hint);

    // ── 4. Spawn ──
    pm::log::info_lazy(k_ch, [&] {
        return "spawn cmd=\"" + command.substr(0, 120) + "\" shell=\"" + shell.exe
            + "\" cwd=\"" + cwd + "\" timeout=" + std::to_string(timeout_ms) + "ms";
    });

    auto spawn_res = spawn_shell_process(shell, command, cwd);
    if (!spawn_res.ok) {
        pm::log::error_lazy(k_ch, [&] { return "spawn failed: " + spawn_res.error; });
        return fatal("run: spawn failed \xe2\x80\x94 " + spawn_res.error);
    }

#if defined(_WIN32)
    HANDLE hProc = static_cast<HANDLE>(spawn_res.process_handle);
    HANDLE hOut  = static_cast<HANDLE>(spawn_res.stdout_read);
    HANDLE hErr  = static_cast<HANDLE>(spawn_res.stderr_read);
    g_run_process.store(hProc, std::memory_order_release);
    g_run_pid.store(spawn_res.pid, std::memory_order_release);
#else
    g_run_pid.store(spawn_res.pid, std::memory_order_release);
    int fdOut = spawn_res.stdout_fd;
    int fdErr = spawn_res.stderr_fd;
#endif

    // ── 5. Reader threads ──
    std::string captured_stdout, captured_stderr;
    std::mutex  mtx_out, mtx_err;

    auto reader = [](auto handle, std::string& buf, std::mutex& mtx,
                     const std::string& stream_name) {
#if defined(_WIN32)
        char tmp[4096];
        DWORD n = 0;
        while (ReadFile(handle, tmp, sizeof(tmp), &n, nullptr) && n > 0) {
            std::string chunk(tmp, n);
            { std::lock_guard<std::mutex> lk(mtx); buf.append(chunk); }
            { std::lock_guard<std::mutex> lk(g_hook_mtx);
              if (g_chunk_hook) g_chunk_hook(stream_name, chunk); }
        }
#else
        char tmp[4096];
        ssize_t n;
        while ((n = ::read(handle, tmp, sizeof(tmp))) > 0) {
            std::string chunk(tmp, static_cast<size_t>(n));
            { std::lock_guard<std::mutex> lk(mtx); buf.append(chunk); }
            { std::lock_guard<std::mutex> lk(g_hook_mtx);
              if (g_chunk_hook) g_chunk_hook(stream_name, chunk); }
        }
#endif
    };

    std::thread t_out(reader,
#if defined(_WIN32)
        hOut,
#else
        fdOut,
#endif
        std::ref(captured_stdout), std::ref(mtx_out), std::string("stdout"));

    std::thread t_err(reader,
#if defined(_WIN32)
        hErr,
#else
        fdErr,
#endif
        std::ref(captured_stderr), std::ref(mtx_err), std::string("stderr"));

    // ── 6. Wait with timeout + cancel ──
    const auto deadline = chr::steady_clock::now() + chr::milliseconds(timeout_ms);
    bool timed_out = false;
    bool cancelled = false;
    int  exit_code = -1;

#if defined(_WIN32)
    constexpr DWORD k_poll_ms = 200;
    while (true) {
        DWORD wait_ms = static_cast<DWORD>(
            std::min<long long>(k_poll_ms,
                chr::duration_cast<chr::milliseconds>(deadline - chr::steady_clock::now()).count()));
        if (wait_ms == 0 || chr::steady_clock::now() >= deadline) wait_ms = 0;

        DWORD wr = WaitForSingleObject(hProc, wait_ms);
        if (wr == WAIT_OBJECT_0) {
            DWORD ec = 0;
            GetExitCodeProcess(hProc, &ec);
            exit_code = static_cast<int>(ec);
            break;
        }
        if (chr::steady_clock::now() >= deadline) {
            timed_out = true;
            terminate_process_tree(spawn_res.pid);
            WaitForSingleObject(hProc, 3000);
            pm::log::warn_lazy(k_ch, [&] { return "timeout after " + std::to_string(timeout_ms) + "ms"; });
            break;
        }
        if (media::cli::cancel_requested()) {
            cancelled = true;
            terminate_process_tree(spawn_res.pid);
            WaitForSingleObject(hProc, 3000);
            pm::log::info_lazy(k_ch, [&] { return "cancelled by user"; });
            break;
        }
    }
    g_run_process.store(nullptr, std::memory_order_release);
    g_run_pid.store(0, std::memory_order_release);
#else
    constexpr int k_poll_us = 200000;
    while (true) {
        int status = 0;
        pid_t wp = waitpid(spawn_res.pid, &status, WNOHANG);
        if (wp > 0) {
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        if (chr::steady_clock::now() >= deadline) {
            timed_out = true;
            kill(spawn_res.pid, SIGKILL);
            waitpid(spawn_res.pid, nullptr, 0);
            pm::log::warn_lazy(k_ch, [&] { return "timeout after " + std::to_string(timeout_ms) + "ms"; });
            break;
        }
        if (media::cli::cancel_requested()) {
            cancelled = true;
            kill(spawn_res.pid, SIGTERM);
            usleep(500000);
            kill(spawn_res.pid, SIGKILL);
            waitpid(spawn_res.pid, nullptr, 0);
            pm::log::info_lazy(k_ch, [&] { return "cancelled by user"; });
            break;
        }
        usleep(k_poll_us);
    }
    g_run_pid.store(0, std::memory_order_release);
#endif

    // ── 7. Drain ──
    t_out.join();
    t_err.join();

#if defined(_WIN32)
    CloseHandle(hOut);
    CloseHandle(hErr);
    CloseHandle(hProc);
#else
    close(fdOut);
    close(fdErr);
#endif

    // ── 8. Normalize & truncate ──
    captured_stdout = normalize_crlf(std::move(captured_stdout));
    captured_stderr = normalize_crlf(std::move(captured_stderr));
    captured_stdout = tail_truncate(captured_stdout, k_output_max_bytes);
    captured_stderr = tail_truncate(captured_stderr, k_output_max_bytes);

    pm::log::info_lazy(k_ch, [&] {
        return "done exit=" + std::to_string(exit_code)
            + " stdout=" + std::to_string(captured_stdout.size()) + "B"
            + " stderr=" + std::to_string(captured_stderr.size()) + "B"
            + (timed_out ? " TIMEOUT" : "")
            + (cancelled ? " CANCELLED" : "");
    });

    // ── 9. Envelope ──
    media::llm::path::ExecuteResult r;
    r.ok = !timed_out && !cancelled;
    r.envelope = nlohmann::json{
        {"ok", r.ok},
        {"exit_code", exit_code},
        {"stdout", captured_stdout},
        {"stderr", captured_stderr},
    };
    if (timed_out) {
        r.error = "run: timed out after " + std::to_string(timeout_ms) + "ms";
        r.envelope["error"] = r.error;
        r.envelope["timed_out"] = true;
    }
    if (cancelled) {
        r.error = "run: cancelled";
        r.envelope["error"] = r.error;
        r.envelope["cancelled"] = true;
    }
    return r;
}

// ═══════════════════════════════════════════════════════════════════════════════
// §9  Public API — abort_active_run
// ═══════════════════════════════════════════════════════════════════════════════

void abort_active_run() {
#if defined(_WIN32)
    DWORD pid = static_cast<DWORD>(g_run_pid.load(std::memory_order_acquire));
    if (pid != 0) {
        terminate_process_tree(pid);
        pm::log::info(k_ch, "abort_active_run: terminated process tree");
        return;
    }
    HANDLE h = static_cast<HANDLE>(g_run_process.load(std::memory_order_acquire));
    if (h) {
        TerminateProcess(h, 1);
        pm::log::info(k_ch, "abort_active_run: terminated process");
    }
#else
    int pid = g_run_pid.load(std::memory_order_acquire);
    if (pid > 0) {
        kill(pid, SIGTERM);
        usleep(200000);
        kill(pid, SIGKILL);
        pm::log::info(k_ch, "abort_active_run: terminated process");
    }
#endif
}

} // namespace media::llm::run
