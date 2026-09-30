#include "stdafx.h"
#include "FileViewer.h"
#include "helpers/markdown_preview_engine.hpp"
#include "llm/llm_fs_guard.hpp"
#if defined(FEATURE_VIEWER_WEB)
#include "win/viewers/text/ViewerWebPanel.h"
#include "win/viewers/viewer_web_contract.hpp"
#endif
#include <wxx_webbrowser.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::wstring resolve_openscad_executable_path()
{
    namespace fs = std::filesystem;
    std::vector<wchar_t> exeBuf(MAX_PATH * 4, 0);
    const DWORD n = ::GetModuleFileNameW(nullptr, exeBuf.data(), static_cast<DWORD>(exeBuf.size() - 1));
    if (n == 0 || n >= exeBuf.size() - 1)
        return {};

    const fs::path exePath(exeBuf.data());
    const fs::path exeDir = exePath.parent_path();
    const fs::path exeParent = exeDir.parent_path();

    // Preferred packaged location first (`dist/bin/openscad.exe`), then legacy colocated paths.
    const std::vector<fs::path> candidates = {
        exeDir / L"dist" / L"bin" / L"openscad.exe",
        exeParent / L"dist" / L"bin" / L"openscad.exe",
        exeDir / L"bin" / L"openscad.exe",
        exeParent / L"bin" / L"openscad.exe",
        exeDir / L"openscad.exe",
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec && fs::is_regular_file(candidate, ec) && !ec)
            return candidate.wstring();
    }

    // Portable fallback for non-bundled setups.
    wchar_t found[MAX_PATH * 4]{};
    if (::SearchPathW(nullptr, L"openscad.exe", nullptr, static_cast<DWORD>(std::size(found)), found, nullptr) > 0)
        return found;

    return {};
}

std::wstring trim_ws_copy(std::wstring s)
{
    auto is_ws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (!s.empty() && is_ws(s.front()))
        s.erase(s.begin());
    while (!s.empty() && is_ws(s.back()))
        s.pop_back();
    return s;
}

std::wstring escape_cmd_arg(const std::wstring& s)
{


    std::wstring out;
    out.reserve(s.size() + 8);
    out.push_back(L'"');
    for (wchar_t ch : s) {
        if (ch == L'\\' || ch == L'"')
            out.push_back(L'\\');
        out.push_back(ch);
    }
    out.push_back(L'"');
    return out;
}

std::vector<std::wstring> tokenize_defines_cli(const std::wstring& s)
{
    std::vector<std::wstring> tokens;
    std::wstring cur;
    bool in_quotes = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t ch = s[i];
        if (ch == L'"') {
            in_quotes = !in_quotes;
            cur.push_back(ch);
            continue;
        }
        if (!in_quotes && (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n')) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty())
        tokens.push_back(cur);
    return tokens;
}

std::wstring build_sanitized_defines_cli(const std::wstring& defines)
{
    const auto tokens = tokenize_defines_cli(defines);
    std::wstring out;
    bool expect_value = false;
    for (const auto& tok : tokens) {
        if (tok == L"-D" || tok == L"--define") {
            if (!out.empty())
                out.push_back(L' ');
            out += L"-D";
            expect_value = true;
            continue;
        }
        if (!expect_value)
            continue;
        if (!out.empty())
            out.push_back(L' ');
        out += escape_cmd_arg(tok);
        expect_value = false;
    }
    return out;
}

std::string read_utf8_text_file(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text;
}

} // namespace

bool CFileViewer::LoadOpenSCAD(LPCWSTR path)
{
#if !defined(FEATURE_VIEWER_WEB)
    (void)path;
    return false;
#else
    namespace fs = std::filesystem;

    ClearText();
    ClearMarkdown();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label.Empty();
    m_previewPath.clear();
    ClearFileInfo();

    m_openScadDefinesCli = trim_ws_copy(m_openScadDefinesCli);

    const fs::path scadPath(path);
    m_openScadSourcePath = scadPath.wstring();
    m_openScadSourceUtf8 = read_utf8_text_file(scadPath.wstring());

    std::string immediate_err_utf8;
    if (const std::string deny = media::llm::llm_fs_guard_deny_reason(scadPath); !deny.empty())
        immediate_err_utf8 = deny;

    std::error_code ec_stat;
    const bool isFile = fs::is_regular_file(scadPath, ec_stat);
    if (immediate_err_utf8.empty() && (!isFile || ec_stat))
        immediate_err_utf8 = "OpenSCAD source file not found";

    std::uintmax_t source_size = 0;
    if (!ec_stat && isFile) {
        std::error_code ec_sz;
        source_size = fs::file_size(scadPath, ec_sz);
        if (ec_sz)
            source_size = 0;
    }

    if (!immediate_err_utf8.empty()) {
        PushOpenSCADPreviewToWeb(scadPath.wstring(), scadPath.wstring(), source_size, immediate_err_utf8);
        return false;
    }

    const std::wstring openscadExeW = resolve_openscad_executable_path();
    if (openscadExeW.empty()) {
        PushOpenSCADPreviewToWeb(scadPath.wstring(), scadPath.wstring(), source_size, "openscad.exe not found (checked dist/bin and PATH)");
        return false;
    }

    bool expected = false;
    if (!m_openScadCompileInFlight.compare_exchange_strong(expected, true))
        return true;
    const unsigned token = ++m_openScadCompileToken;
    const std::wstring defines = build_sanitized_defines_cli(m_openScadDefinesCli);
    const std::wstring scadPathW = scadPath.wstring();
    const HWND host = GetHwnd();
    if (!host || !::IsWindow(host)) {
        m_openScadCompileInFlight.store(false);
        return false;
    }
    PushOpenSCADPreviewToWeb(scadPathW, scadPathW, source_size, "Compiling OpenSCAD preview...");

    auto* in_flight = &m_openScadCompileInFlight;
    std::thread([host, token, openscadExeW, scadPathW, defines, source_size, source_utf8 = m_openScadSourceUtf8, in_flight]() {
        namespace fs = std::filesystem;
        auto* out = new OpenScadCompileResult();
        out->token = token;
        out->sourcePathW = scadPathW;
        out->sourceUtf8 = source_utf8;
        out->previewSize = source_size;

        const fs::path openscadExe(openscadExeW);
        const auto nowTick = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        fs::path tempStl = fs::temp_directory_path() / (L"pm_viewer_openscad_" + std::to_wstring(nowTick) + L".stl");

        std::wstring cmd = L"\"" + openscadExe.wstring() + L"\"";
        if (!defines.empty()) {
            cmd += L" ";
            cmd += defines;
        }
        cmd += L" -o \"" + tempStl.wstring() + L"\" \"" + scadPathW + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
        if (!::CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
            out->errUtf8 = "Failed to launch openscad.exe";
        } else {
            ::CloseHandle(pi.hThread);
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 1;
            (void)::GetExitCodeProcess(pi.hProcess, &exitCode);
            ::CloseHandle(pi.hProcess);
            if (exitCode != 0) {
                out->errUtf8 = "OpenSCAD compile failed";
            } else {
                std::error_code ec_out;
                if (!fs::exists(tempStl, ec_out) || ec_out) {
                    out->errUtf8 = "OpenSCAD compile produced no STL output";
                } else {
                    out->success = true;
                    out->previewPathW = tempStl.wstring();
                    std::error_code ec_stl_sz;
                    out->previewSize = fs::file_size(tempStl, ec_stl_sz);
                    if (ec_stl_sz)
                        out->previewSize = 0;
                }
            }
        }
        if (!::PostMessageW(host, UWM_OPENSCAD_COMPILE_DONE, reinterpret_cast<WPARAM>(out), static_cast<LPARAM>(token))) {
            if (out->success && !out->previewPathW.empty()) {
                std::error_code ec_rm;
                fs::remove(out->previewPathW, ec_rm);
            }
            in_flight->store(false);
            delete out;
        }
    }).detach();

    return true;
#endif
}

bool CFileViewer::ReloadOpenSCADWithDefines(const std::wstring& defines_cli)
{
    const std::wstring src = m_openScadSourcePath;
    if (src.empty())
        return false;
    m_openScadDefinesCli = trim_ws_copy(defines_cli);
    return LoadOpenSCAD(src.c_str());
}
