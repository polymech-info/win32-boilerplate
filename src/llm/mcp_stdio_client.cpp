#include "mcp_stdio_client.hpp"

#include "llm/tools/run/RunTool.hpp"
#include "logger/logger.h"

#include <condition_variable>
#include <cctype>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace media::llm::mcp {
namespace {

#if defined(_WIN32)

std::wstring utf8_to_wide(const std::string& u8)
{
    if (u8.empty())
        return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), static_cast<int>(u8.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), static_cast<int>(u8.size()), w.data(), n);
    return w;
}

std::wstring quote_warg(const std::wstring& s)
{
    if (s.find_first_of(L" \t\"") == std::wstring::npos)
        return s;
    std::wstring o = L"\"";
    for (wchar_t c : s) {
        if (c == L'"')
            o += L"\\\"";
        else
            o += c;
    }
    o += L'"';
    return o;
}

std::wstring build_mcp_cmdline(const media::llm::run::ShellSpec& spec,
                               const std::string& command_utf8,
                               const std::vector<std::string>& args_utf8)
{
    std::wstring line = quote_warg(utf8_to_wide(spec.exe))
                      + L" " + utf8_to_wide(spec.flag)
                      + L" " + quote_warg(utf8_to_wide(command_utf8));
    for (const auto& a : args_utf8)
        line += L" " + quote_warg(utf8_to_wide(a));
    return line;
}

#endif

void parse_jsonrpc_result(const nlohmann::json& envelope, nlohmann::json& result_out, std::string& err)
{
    err.clear();
    result_out = nlohmann::json::object();
    if (envelope.contains("error") && envelope["error"].is_object()) {
        const auto& e = envelope["error"];
        err           = e.value("message", std::string("jsonrpc error"));
        err += " (code " + std::to_string(e.value("code", 0)) + ")";
        return;
    }
    if (envelope.contains("result"))
        result_out = envelope["result"];
}

} // namespace

struct StdioMcpClient::Impl {
#if defined(_WIN32)
    HANDLE hProcess    = nullptr;
    HANDLE hWriteStdin = nullptr;
    HANDLE hReadStdout = nullptr;
#else
    pid_t pid            = -1;
    int   fd_stdin_write = -1;
    int   fd_stdout_read = -1;
#endif
    std::map<std::string, std::string> env_extra; // from mcp.json "env" object

    std::mutex              q_mu;
    std::condition_variable q_cv;
    std::deque<std::string> lines;
    bool                    reader_eof = false;
    std::thread             reader_th;
    int                       next_jsonrpc_id = 1;
    bool                      initialized     = false;

#if defined(_WIN32)

    void reader_thread_body_win()
    {
        std::string acc;
        char        buf[16384];
        for (;;) {
            DWORD n = 0;
            if (!::ReadFile(hReadStdout, buf, sizeof(buf), &n, nullptr) || n == 0)
                break;
            acc.append(buf, n);
            for (;;) {
                const auto pos = acc.find('\n');
                if (pos == std::string::npos)
                    break;
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                while (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty()) {
                    std::lock_guard<std::mutex> lk(q_mu);
                    lines.push_back(std::move(line));
                    q_cv.notify_one();
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(q_mu);
            reader_eof = true;
        }
        q_cv.notify_all();
    }

    void start_reader_win() { reader_th = std::thread([this] { reader_thread_body_win(); }); }

    bool write_all(const std::string& data, std::string& err)
    {
        const char* p    = data.data();
        size_t      left = data.size();
        while (left > 0) {
            DWORD n = 0;
            if (!::WriteFile(hWriteStdin, p, static_cast<DWORD>(left > 65536 ? 65536 : left), &n, nullptr) || n == 0) {
                err = "WriteFile stdin failed";
                return false;
            }
            p += n;
            left -= n;
        }
        return true;
    }

#else

    void reader_thread_body_posix(int fd_read)
    {
        std::string acc;
        char        buf[16384];
        for (;;) {
            const ssize_t n = ::read(fd_read, buf, sizeof(buf));
            if (n <= 0)
                break;
            acc.append(buf, static_cast<size_t>(n));
            for (;;) {
                const auto pos = acc.find('\n');
                if (pos == std::string::npos)
                    break;
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                while (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty()) {
                    std::lock_guard<std::mutex> lk(q_mu);
                    lines.push_back(std::move(line));
                    q_cv.notify_one();
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(q_mu);
            reader_eof = true;
        }
        q_cv.notify_all();
    }

    void start_reader_posix() { reader_th = std::thread([this] { reader_thread_body_posix(fd_stdout_read); }); }

    bool write_all(const std::string& data, std::string& err)
    {
        const char* p    = data.data();
        size_t      left = data.size();
        while (left > 0) {
            const ssize_t n = ::write(fd_stdin_write, p, left > 65536 ? 65536 : left);
            if (n <= 0) {
                err = std::string("write stdin failed: ") + std::strerror(errno);
                return false;
            }
            p += static_cast<size_t>(n);
            left -= static_cast<size_t>(n);
        }
        return true;
    }

#endif

    bool wait_response_for_id(int want_id, nlohmann::json& envelope, std::string& err)
    {
        for (;;) {
            std::unique_lock<std::mutex> lk(q_mu);
            q_cv.wait(lk, [&] { return !lines.empty() || reader_eof; });
            if (lines.empty() && reader_eof) {
                err = "MCP stdio: stdout closed before JSON-RPC response";
                return false;
            }
            std::string line = std::move(lines.front());
            lines.pop_front();
            lk.unlock();

            nlohmann::json j;
            try {
                j = nlohmann::json::parse(line);
            } catch (...) {
                logger::debug(std::string("MCP stdio: skip non-JSON line: ") + line.substr(0, 120));
                continue;
            }
            if (j.contains("method") && !j.contains("id"))
                continue;
            if (j.contains("id")) {
                int rid = -1;
                if (j["id"].is_number_integer())
                    rid = static_cast<int>(j["id"].get<long long>());
                else if (j["id"].is_number_unsigned())
                    rid = static_cast<int>(j["id"].get<unsigned long long>());
                if (rid == want_id) {
                    envelope = std::move(j);
                    return true;
                }
            }
        }
    }

    void stop_process()
    {
#if defined(_WIN32)
        if (hWriteStdin && hWriteStdin != INVALID_HANDLE_VALUE) {
            ::CloseHandle(hWriteStdin);
            hWriteStdin = nullptr;
        }
        if (hProcess && hProcess != INVALID_HANDLE_VALUE) {
            (void)::WaitForSingleObject(hProcess, 8000);
        }
        if (reader_th.joinable())
            reader_th.join();
        if (hReadStdout && hReadStdout != INVALID_HANDLE_VALUE) {
            ::CloseHandle(hReadStdout);
            hReadStdout = nullptr;
        }
        if (hProcess && hProcess != INVALID_HANDLE_VALUE) {
            ::CloseHandle(hProcess);
            hProcess = nullptr;
        }
#else
        if (fd_stdin_write >= 0) {
            ::close(fd_stdin_write);
            fd_stdin_write = -1;
        }
        if (pid > 0) {
            (void)::kill(pid, SIGTERM);
            int st = 0;
            (void)::waitpid(pid, &st, 0);
            pid = -1;
        }
        if (reader_th.joinable())
            reader_th.join();
        if (fd_stdout_read >= 0) {
            ::close(fd_stdout_read);
            fd_stdout_read = -1;
        }
#endif
    }
};

StdioMcpClient::StdioMcpClient(std::string command, std::vector<std::string> args,
                               std::map<std::string, std::string> env)
    : impl_(std::make_unique<Impl>())
{
    impl_->env_extra = std::move(env);
#if defined(_WIN32)
    // Build a custom environment block that extends the inherited env with our extras.
    // GetEnvironmentStringsW returns a double-null-terminated block of L"KEY=VALUE\0" pairs.
    auto build_env_block = [&](const std::map<std::string, std::string>& extras) -> std::vector<wchar_t> {
        std::vector<std::pair<std::wstring, std::wstring>> pairs;
        LPWCH base = ::GetEnvironmentStringsW();
        if (base) {
            for (LPWCH p = base; *p; ) {
                std::wstring entry(p);
                p += entry.size() + 1;
                auto eq = entry.find(L'=');
                if (eq != std::wstring::npos)
                    pairs.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
            }
            ::FreeEnvironmentStringsW(base);
        }
        for (const auto& [k, v] : extras) {
            std::wstring wk = utf8_to_wide(k);
            bool found = false;
            for (auto& p : pairs) {
                if (p.first == wk) { p.second = utf8_to_wide(v); found = true; break; }
            }
            if (!found) pairs.emplace_back(wk, utf8_to_wide(v));
        }
        std::vector<wchar_t> block;
        for (const auto& [k, v] : pairs) {
            for (wchar_t c : k)   block.push_back(c);
            block.push_back(L'=');
            for (wchar_t c : v)   block.push_back(c);
            block.push_back(L'\0');
        }
        block.push_back(L'\0');
        return block;
    };

    const auto         shell_spec = media::llm::run::detect_shell("cmd");
    const std::wstring cmdline    = build_mcp_cmdline(shell_spec, command, args);
    std::vector<wchar_t>         cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength              = sizeof(sa);
    sa.bInheritHandle       = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hChildStd_OUT_rd = nullptr;
    HANDLE hChildStd_OUT_wr = nullptr;
    HANDLE hChildStd_IN_rd  = nullptr;
    HANDLE hChildStd_IN_wr   = nullptr;

    if (!::CreatePipe(&hChildStd_OUT_rd, &hChildStd_OUT_wr, &sa, 0)) {
        impl_.reset();
        return;
    }
    if (!::SetHandleInformation(hChildStd_OUT_rd, HANDLE_FLAG_INHERIT, 0)) {
        ::CloseHandle(hChildStd_OUT_rd);
        ::CloseHandle(hChildStd_OUT_wr);
        impl_.reset();
        return;
    }
    if (!::CreatePipe(&hChildStd_IN_rd, &hChildStd_IN_wr, &sa, 0)) {
        ::CloseHandle(hChildStd_OUT_rd);
        ::CloseHandle(hChildStd_OUT_wr);
        impl_.reset();
        return;
    }
    if (!::SetHandleInformation(hChildStd_IN_wr, HANDLE_FLAG_INHERIT, 0)) {
        ::CloseHandle(hChildStd_OUT_rd);
        ::CloseHandle(hChildStd_OUT_wr);
        ::CloseHandle(hChildStd_IN_rd);
        ::CloseHandle(hChildStd_IN_wr);
        impl_.reset();
        return;
    }

    HANDLE hNul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hNul == INVALID_HANDLE_VALUE) {
        ::CloseHandle(hChildStd_OUT_rd);
        ::CloseHandle(hChildStd_OUT_wr);
        ::CloseHandle(hChildStd_IN_rd);
        ::CloseHandle(hChildStd_IN_wr);
        impl_.reset();
        return;
    }
    (void)::SetHandleInformation(hNul, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    STARTUPINFOW si{};
    si.cb            = sizeof(si);
    si.dwFlags       = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow   = SW_HIDE;
    si.hStdOutput    = hChildStd_OUT_wr;
    si.hStdInput     = hChildStd_IN_rd;
    si.hStdError     = hNul;

    PROCESS_INFORMATION pi{};
#ifndef CREATE_NO_WINDOW
#define PM_CREATE_NO_WINDOW 0x08000000u
#else
#define PM_CREATE_NO_WINDOW CREATE_NO_WINDOW
#endif
    std::vector<wchar_t> env_block = build_env_block(impl_->env_extra);
    const DWORD create_flags = PM_CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    const std::wstring shell_exe_w = utf8_to_wide(shell_spec.exe);
    const BOOL ok = ::CreateProcessW(shell_exe_w.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
                                     create_flags, env_block.data(), nullptr, &si, &pi);
    ::CloseHandle(hChildStd_OUT_wr);
    ::CloseHandle(hChildStd_IN_rd);
    ::CloseHandle(hNul);
    if (!ok) {
        ::CloseHandle(hChildStd_OUT_rd);
        ::CloseHandle(hChildStd_IN_wr);
        impl_.reset();
        return;
    }
    ::CloseHandle(pi.hThread);
    impl_->hProcess    = pi.hProcess;
    impl_->hWriteStdin = hChildStd_IN_wr;
    impl_->hReadStdout = hChildStd_OUT_rd;
#else
    // Resolve the best available shell once in the parent, before fork().
    // detect_shell("auto") picks $SHELL → /bin/bash → /bin/sh.
    // Guard against pwsh: its flag ("-NoProfile -NonInteractive -Command") can't
    // be passed as a single execvp argv entry; fall back to /bin/sh in that case.
    auto shell_spec = media::llm::run::detect_shell("auto");
    if (shell_spec.is_pwsh)
        shell_spec = {"/bin/sh", "-c", false};

    int in_pipe[2];
    int out_pipe[2];
    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0) {
        impl_.reset();
        return;
    }
    const pid_t p = ::fork();
    if (p < 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        impl_.reset();
        return;
    }
    if (p == 0) {
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        const int dn = ::open("/dev/null", O_WRONLY);
        if (dn >= 0) {
            ::dup2(dn, STDERR_FILENO);
            ::close(dn);
        }

        // `command` is passed as-is as the shell command string for -c; it may
        // already be a full command line ("npx -y mcp-remote https://...") or a
        // bare executable name ("uvx"). Only the separate `args` entries need
        // POSIX single-quote escaping so they survive shell word splitting.
        auto sh_quote = [](const std::string& s) -> std::string {
            std::string out = "'";
            for (char c : s) {
                if (c == '\'') out += "'\\''";
                else           out += c;
            }
            out += "'";
            return out;
        };
        std::string shell_cmd = command; // NOT quoted — IS the shell command line
        for (const auto& a : args) {
            shell_cmd += ' ';
            shell_cmd += sh_quote(a);
        }
        for (const auto& [k, v] : impl_->env_extra)
            ::setenv(k.c_str(), v.c_str(), 1);
        const char* sh_argv[] = {shell_spec.exe.c_str(), shell_spec.flag.c_str(), shell_cmd.c_str(), nullptr};
        (void)::execvp(shell_spec.exe.c_str(), const_cast<char* const*>(sh_argv));
        ::_exit(127);
    }
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    impl_->pid            = p;
    impl_->fd_stdin_write = in_pipe[1];
    impl_->fd_stdout_read = out_pipe[0];
#endif
}

StdioMcpClient::~StdioMcpClient()
{
    if (!impl_)
        return;
    impl_->stop_process();
}

bool StdioMcpClient::initialize(std::string& err)
{
    err.clear();
    if (!impl_ || (
#if defined(_WIN32)
                     impl_->hProcess == nullptr || impl_->hProcess == INVALID_HANDLE_VALUE
#else
                     impl_->pid < 0
#endif
                         )) {
        err = "MCP stdio: process not started (spawn failed — is `uvx` on PATH?)";
        return false;
    }

#if defined(_WIN32)
    impl_->start_reader_win();
#else
    impl_->start_reader_posix();
#endif

    const int init_id = impl_->next_jsonrpc_id++;
    nlohmann::json params = nlohmann::json::object();
    params["protocolVersion"] = "2025-11-25";
    params["capabilities"]    = nlohmann::json::object();
    params["clientInfo"]      = nlohmann::json{{"name", "pm-image"}, {"version", "mcp-stdio"}};

    const nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", init_id}, {"method", "initialize"}, {"params", params}};

    if (!impl_->write_all(req.dump() + "\n", err))
        return false;

    nlohmann::json env;
    if (!impl_->wait_response_for_id(init_id, env, err))
        return false;

    std::string    perr;
    nlohmann::json init_result;
    parse_jsonrpc_result(env, init_result, perr);
    if (!perr.empty()) {
        err = perr;
        return false;
    }
    (void)init_result;

    const nlohmann::json notif = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}, {"params", nlohmann::json::object()}};
    if (!impl_->write_all(notif.dump() + "\n", err))
        return false;

    impl_->initialized = true;
    return true;
}

void StdioMcpClient::try_delete_session()
{
    if (!impl_)
        return;
    impl_->stop_process();
    impl_->initialized = false;
}

nlohmann::json StdioMcpClient::tools_list_all(std::string& err)
{
    err.clear();
    if (!impl_ || !impl_->initialized) {
        err = "MCP stdio: not initialized";
        return {};
    }

    nlohmann::json all = nlohmann::json::array();
    std::string    cursor;
    for (;;) {
        const int rid = impl_->next_jsonrpc_id++;
        nlohmann::json params = nlohmann::json::object();
        if (!cursor.empty())
            params["cursor"] = cursor;

        const nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", rid}, {"method", "tools/list"}, {"params", params}};
        if (!impl_->write_all(req.dump() + "\n", err))
            return {};

        nlohmann::json env;
        if (!impl_->wait_response_for_id(rid, env, err))
            return {};

        std::string    perr;
        nlohmann::json result;
        parse_jsonrpc_result(env, result, perr);
        if (!perr.empty()) {
            err = perr;
            return {};
        }
        if (result.contains("tools") && result["tools"].is_array()) {
            for (const auto& t : result["tools"])
                all.push_back(t);
        }
        cursor.clear();
        if (result.contains("nextCursor") && result["nextCursor"].is_string()) {
            cursor = result["nextCursor"].get<std::string>();
            if (!cursor.empty())
                continue;
        }
        break;
    }
    return all;
}

nlohmann::json StdioMcpClient::tools_call(const std::string& remote_tool_name, const nlohmann::json& arguments,
                                          std::string& err)
{
    err.clear();
    if (!impl_ || !impl_->initialized) {
        err = "MCP stdio: not initialized";
        return {};
    }

    const int        rid = impl_->next_jsonrpc_id++;
    nlohmann::json   params = {{"name", remote_tool_name},
                             {"arguments", arguments.is_object() ? arguments : nlohmann::json::object()}};
    const nlohmann::json req  = {{"jsonrpc", "2.0"}, {"id", rid}, {"method", "tools/call"}, {"params", params}};
    if (!impl_->write_all(req.dump() + "\n", err))
        return {};

    nlohmann::json env;
    if (!impl_->wait_response_for_id(rid, env, err))
        return {};

    std::string    perr;
    nlohmann::json result;
    parse_jsonrpc_result(env, result, perr);
    if (!perr.empty()) {
        err = perr;
        return {};
    }
    return result;
}

} // namespace media::llm::mcp
