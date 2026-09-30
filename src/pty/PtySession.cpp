#include "stdafx.h"
#include "pty/PtySession.h"

#include "helpers/text_conv.hpp"
#include "logger/logger.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#endif

namespace pm::pty {
namespace {

#if defined(_WIN32)

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) : m_h(h) {}
    ~Handle() { reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : m_h(other.release()) {}
    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    HANDLE get() const { return m_h; }
    HANDLE* put()
    {
        reset();
        return &m_h;
    }
    HANDLE release()
    {
        HANDLE h = m_h;
        m_h = nullptr;
        return h;
    }
    void reset(HANDLE h = nullptr)
    {
        if (m_h && m_h != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_h);
        m_h = h;
    }
    explicit operator bool() const { return m_h && m_h != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_h = nullptr;
};

std::wstring ps_single_quote(std::wstring value)
{
    std::wstring out;
    out.reserve(value.size() + 2);
    out.push_back(L'\'');
    for (wchar_t ch : value) {
        if (ch == L'\'')
            out += L"''";
        else
            out.push_back(ch);
    }
    out.push_back(L'\'');
    return out;
}

std::wstring powershell_prompt_command(const std::wstring& cwd)
{
    if (cwd.empty())
        return L"powershell.exe -NoLogo";

    std::wstring root = cwd;
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
        root.pop_back();
    if (root.empty())
        return L"powershell.exe -NoLogo";

    return L"powershell.exe -NoLogo -NoExit -Command \""
        L"function global:prompt { "
        L"$p=(Get-Location).ProviderPath; "
        L"$root=" + ps_single_quote(root) + L"; "
        L"if ($p -and $p.StartsWith($root,[System.StringComparison]::OrdinalIgnoreCase)) { "
        L"$r=$p.Substring($root.Length).TrimStart('\\'); "
        L"if ($r.Length) { 'PS .\\' + $r + '> ' } else { 'PS .> ' } "
        L"} else { 'PS ' + $p + '> ' } "
        L"}\"";
}

std::wstring shell_command_line(const SpawnOptions& options)
{
    const std::string& shell_type = options.shell_type;
    if (shell_type == "cmd")
        return L"cmd.exe";
    if (shell_type == "bash")
        return L"bash";
    if (shell_type == "wsl")
        return L"wsl.exe";
    if (shell_type == "git-bash")
        return L"\"C:\\Program Files\\Git\\bin\\bash.exe\" --login -i";
    if (options.one_shot)
        return L"powershell.exe -NoLogo";
    return powershell_prompt_command(options.cwd);
}

std::wstring expand_env_value(const std::wstring& value)
{
    if (value.empty())
        return {};
    const DWORD needed = ::ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0)
        return value;
    std::wstring out(needed, L'\0');
    const DWORD written = ::ExpandEnvironmentStringsW(value.c_str(), out.data(), needed);
    if (written == 0 || written > needed)
        return value;
    out.resize(written > 0 ? written - 1 : 0);
    return out;
}

std::wstring read_registry_env_value(HKEY root, const wchar_t* subkey, const wchar_t* name)
{
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS rc = ::RegGetValueW(root, subkey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || bytes <= sizeof(wchar_t))
        return {};

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    rc = ::RegGetValueW(root, subkey, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, value.data(), &bytes);
    if (rc != ERROR_SUCCESS)
        return {};

    value.resize(wcsnlen_s(value.c_str(), value.size()));
    return type == REG_EXPAND_SZ ? expand_env_value(value) : value;
}

std::wstring getenv_w(const wchar_t* name)
{
    const DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
        return {};
    std::wstring value(needed, L'\0');
    const DWORD n = ::GetEnvironmentVariableW(name, value.data(), needed);
    if (n == 0)
        return {};
    value.resize(n);
    return value;
}

void append_path_part(std::wstring& path, const std::wstring& part)
{
    if (part.empty())
        return;
    if (!path.empty() && path.back() != L';')
        path.push_back(L';');
    path += part;
}

bool path_contains_part(const std::wstring& path, const std::wstring& part)
{
    if (path.empty() || part.empty())
        return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(L';', start);
        std::wstring item = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        while (!item.empty() && (item.back() == L'\\' || item.back() == L'/'))
            item.pop_back();
        std::wstring cmp = part;
        while (!cmp.empty() && (cmp.back() == L'\\' || cmp.back() == L'/'))
            cmp.pop_back();
        if (!item.empty() && ::CompareStringOrdinal(item.c_str(), -1, cmp.c_str(), -1, TRUE) == CSTR_EQUAL)
            return true;
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return false;
}

void prepend_path_part(std::wstring& path, const std::wstring& part)
{
    if (part.empty() || path_contains_part(path, part))
        return;
    if (path.empty()) {
        path = part;
        return;
    }
    path = part + L";" + path;
}

std::wstring exe_adjacent_bin_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size())
        return {};
    buf.resize(n);
    const size_t slash = buf.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    std::wstring bin = buf.substr(0, slash + 1) + L"bin";
    const DWORD attrs = ::GetFileAttributesW(bin.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        return {};
    return bin;
}

std::wstring merged_shell_path()
{
    std::wstring path = getenv_w(L"Path");
    if (path.empty())
        path = getenv_w(L"PATH");

    append_path_part(path, read_registry_env_value(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        L"Path"));
    append_path_part(path, read_registry_env_value(HKEY_CURRENT_USER, L"Environment", L"Path"));
    prepend_path_part(path, exe_adjacent_bin_dir());
    return path;
}

std::vector<wchar_t> build_shell_environment_block()
{
    std::vector<std::wstring> entries;
    bool has_path = false;
    const std::wstring path = merged_shell_path();

    if (LPWCH env = ::GetEnvironmentStringsW()) {
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
        ::FreeEnvironmentStringsW(env);
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

std::string win_error_utf8(const char* prefix, DWORD code = ::GetLastError())
{
    wchar_t* msg = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD n = ::FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring text;
    if (n && msg)
        text.assign(msg, n);
    if (msg)
        ::LocalFree(msg);
    std::string out(prefix ? prefix : "Win32 error");
    out += " (";
    out += std::to_string(code);
    out += ")";
    if (!text.empty()) {
        out += ": ";
        out += pmui::wide_to_utf8(text);
    }
    return out;
}

Handle create_kill_on_close_job(const std::string& shell_id)
{
    Handle job(::CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        logger::warn("[console-pty] create job failed shell=" + shell_id
            + " err=" + win_error_utf8("CreateJobObjectW failed"));
        return {};
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(
            job.get(),
            JobObjectExtendedLimitInformation,
            &info,
            sizeof(info))) {
        logger::warn("[console-pty] configure job failed shell=" + shell_id
            + " err=" + win_error_utf8("SetInformationJobObject failed"));
        return {};
    }
    return job;
}

using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(HPCON);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
};

const ConPtyApi& conpty_api()
{
    static const ConPtyApi api = [] {
        ConPtyApi out;
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        if (!kernel)
            kernel = ::LoadLibraryW(L"kernel32.dll");
        if (!kernel)
            return out;
        out.create = reinterpret_cast<CreatePseudoConsoleFn>(::GetProcAddress(kernel, "CreatePseudoConsole"));
        out.resize = reinterpret_cast<ResizePseudoConsoleFn>(::GetProcAddress(kernel, "ResizePseudoConsole"));
        out.close = reinterpret_cast<ClosePseudoConsoleFn>(::GetProcAddress(kernel, "ClosePseudoConsole"));
        return out;
    }();
    return api;
}

class WinConPtySession final : public PtySession {
public:
    WinConPtySession(SpawnOptions options, EventSink sink)
        : m_options(std::move(options)), m_sink(std::move(sink))
    {
    }

    ~WinConPtySession() override { close(); }

    bool start()
    {
        const auto& api = conpty_api();
        if (!api.create || !api.resize || !api.close) {
            emit_error("Windows ConPTY APIs are not available on this system.");
            return false;
        }

        if (m_options.cols <= 0) m_options.cols = 80;
        if (m_options.rows <= 0) m_options.rows = 24;

        Handle in_read;
        Handle in_write;
        Handle out_read;
        Handle out_write;
        if (!::CreatePipe(in_read.put(), in_write.put(), nullptr, 0)) {
            emit_error(win_error_utf8("CreatePipe stdin failed"));
            return false;
        }
        if (!::CreatePipe(out_read.put(), out_write.put(), nullptr, 0)) {
            emit_error(win_error_utf8("CreatePipe stdout failed"));
            return false;
        }

        const COORD size{
            static_cast<SHORT>(m_options.cols),
            static_cast<SHORT>(m_options.rows),
        };
        HRESULT hr = api.create(size, in_read.get(), out_write.get(), 0, &m_hpc);
        if (FAILED(hr)) {
            emit_error("CreatePseudoConsole failed: 0x" + std::to_string(static_cast<unsigned long>(hr)));
            return false;
        }

        in_read.reset();
        out_write.reset();
        m_input = std::move(in_write);
        m_output = std::move(out_read);

        SIZE_T attr_size = 0;
        (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
        std::vector<unsigned char> attr_storage(attr_size);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
        if (!::InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
            emit_error(win_error_utf8("InitializeProcThreadAttributeList failed"));
            return false;
        }

        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        si.lpAttributeList = attrs;
        if (!::UpdateProcThreadAttribute(
                attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_hpc, sizeof(m_hpc), nullptr, nullptr)) {
            emit_error(win_error_utf8("UpdateProcThreadAttribute failed"));
            ::DeleteProcThreadAttributeList(attrs);
            return false;
        }

        std::wstring cmd = shell_command_line(m_options);
        std::vector<wchar_t> env_block = build_shell_environment_block();
        PROCESS_INFORMATION pi{};
        const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
        const wchar_t* cwd = m_options.cwd.empty() ? nullptr : m_options.cwd.c_str();
        BOOL ok = ::CreateProcessW(
            nullptr,
            cmd.data(),
            nullptr,
            nullptr,
            FALSE,
            flags,
            env_block.empty() ? nullptr : env_block.data(),
            cwd,
            &si.StartupInfo,
            &pi);

        ::DeleteProcThreadAttributeList(attrs);

        if (!ok) {
            emit_error(win_error_utf8("CreateProcessW failed"));
            return false;
        }

        m_pid = pi.dwProcessId;
        m_process.reset(pi.hProcess);
        m_thread.reset(pi.hThread);

        m_job = create_kill_on_close_job(m_options.shell_id);
        if (m_job && !::AssignProcessToJobObject(m_job.get(), m_process.get())) {
            logger::warn("[console-pty] assign job failed shell=" + m_options.shell_id
                + " pid=" + std::to_string(m_pid)
                + " err=" + win_error_utf8("AssignProcessToJobObject failed"));
            m_job.reset();
        }

        logger::trace("[console-pty] start shell=" + m_options.shell_id
            + " pid=" + std::to_string(m_pid)
            + " oneShot=" + std::to_string(m_options.one_shot ? 1 : 0)
            + " type=" + m_options.shell_type);

        m_running.store(true);
        m_reader = std::thread([this] { reader_loop(); });
        m_waiter = std::thread([this] { waiter_loop(); });
        return true;
    }

    void write(const std::string& data) override
    {
        std::lock_guard<std::mutex> lock(m_io_mutex);
        if (!m_input)
            return;
        DWORD written = 0;
        (void)::WriteFile(m_input.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    }

    void resize(int cols, int rows) override
    {
        if (!m_hpc || cols <= 0 || rows <= 0)
            return;
        const auto& api = conpty_api();
        if (!api.resize)
            return;
        const COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
        (void)api.resize(m_hpc, size);
    }

    void close() override
    {
        bool expected = true;
        const bool was_running = m_running.compare_exchange_strong(expected, false);

        if (was_running)
            logger::trace("[console-pty] close shell=" + m_options.shell_id
                + " pid=" + std::to_string(m_pid));

        if (m_job && was_running) {
            (void)::TerminateJobObject(m_job.get(), 1);
        } else if (m_process && was_running) {
            DWORD code = STILL_ACTIVE;
            if (::GetExitCodeProcess(m_process.get(), &code) && code == STILL_ACTIVE) {
                (void)::TerminateProcess(m_process.get(), 1);
            }
        }

        close_pseudoconsole();

        if (m_reader.joinable()) {
            (void)::CancelSynchronousIo(static_cast<HANDLE>(m_reader.native_handle()));
        }

        {
            std::lock_guard<std::mutex> lock(m_io_mutex);
            m_input.reset();
            m_output.reset();
        }

        if (m_reader.joinable())
            m_reader.join();
        if (m_waiter.joinable())
            m_waiter.join();
        m_thread.reset();
        m_process.reset();
        m_job.reset();
    }

private:
    void close_pseudoconsole()
    {
        std::lock_guard<std::mutex> lock(m_hpc_mutex);
        if (!m_hpc)
            return;
        const auto& api = conpty_api();
        if (api.close)
            api.close(m_hpc);
        m_hpc = nullptr;
    }

    void reader_loop()
    {
        std::vector<char> buf(8192);
        for (;;) {
            HANDLE h = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_io_mutex);
                h = m_output.get();
            }
            if (!h || !m_running.load())
                break;

            DWORD read = 0;
            if (!::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) || read == 0)
                break;
            if (m_sink.on_output)
                m_sink.on_output(m_options.shell_id, std::string(buf.data(), buf.data() + read));
        }

        close_pseudoconsole();
    }

    void waiter_loop()
    {
        int exit_code = 0;
        if (m_process) {
            DWORD code = 0;
            (void)::WaitForSingleObject(m_process.get(), INFINITE);
            if (::GetExitCodeProcess(m_process.get(), &code))
                exit_code = static_cast<int>(code);
        }

        const bool notify = m_running.exchange(false);
        close_pseudoconsole();
        if (m_reader.joinable())
            (void)::CancelSynchronousIo(static_cast<HANDLE>(m_reader.native_handle()));
        if (notify && m_sink.on_exit) {
            logger::trace("[console-pty] process exit shell=" + m_options.shell_id
                + " pid=" + std::to_string(m_pid)
                + " code=" + std::to_string(exit_code));
            m_sink.on_exit(m_options.shell_id, exit_code);
        }
    }

    void emit_error(const std::string& error)
    {
        if (m_sink.on_error)
            m_sink.on_error(m_options.shell_id, error);
    }

    SpawnOptions       m_options;
    EventSink          m_sink;
    HPCON              m_hpc = nullptr;
    Handle             m_input;
    Handle             m_output;
    Handle             m_process;
    Handle             m_thread;
    Handle             m_job;
    DWORD              m_pid = 0;
    std::atomic<bool>  m_running{false};
    std::mutex         m_io_mutex;
    std::mutex         m_hpc_mutex;
    std::thread        m_reader;
    std::thread        m_waiter;
};

#endif // defined(_WIN32)

} // namespace

std::unique_ptr<PtySession> create_session(const SpawnOptions& options, EventSink sink)
{
#if defined(_WIN32)
    auto session = std::make_unique<WinConPtySession>(options, std::move(sink));
    if (!session->start())
        return nullptr;
    return session;
#else
    if (sink.on_error)
        sink.on_error(options.shell_id, "Native PTY is not implemented for this platform yet.");
    return nullptr;
#endif
}

} // namespace pm::pty
