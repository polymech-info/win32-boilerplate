// ChatWebResource.cpp — load apps/* WebView bundles from dist/shared.
//
// When FEATURE_CHAT_WEB is ON the parent CMakeLists.txt:
//   1. Builds apps/chat-next → dist/shared/chat.html (webpack + inline scripts).
//   2. Does not embed in the .exe (keeps pm-image smaller); ship dist/shared/*.html with the binary.
//   3. Compile-define FEATURE_CHAT_WEB=1 so the C++ side enables the WebView2-hosted chat panel.
//
// When FEATURE_CHAT_WEB is OFF this .cpp is NOT compiled; ChatWebResource.h's
// declarations fall through to a separate stub TU (or callers just guard their
// usage with #ifdef FEATURE_CHAT_WEB / pmui::chat_web_available()).
//
#include "stdafx.h"
#include "ChatWebResource.h"

#include <Windows.h>

#include <fstream>
#include <string>
#include <vector>

namespace pmui {

namespace {

namespace fs = std::filesystem;

fs::path module_exe_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0u)
        return {};
    buf.resize(n);
    return fs::path(buf).parent_path();
}

std::string read_utf8_file_next_to_exe(const wchar_t* filename)
{
    const fs::path exe_dir = module_exe_dir();
    if (exe_dir.empty())
        return {};

    std::vector<fs::path> candidates;
    if (exe_dir.filename() == L"win-x64")
        candidates.push_back(exe_dir.parent_path() / L"shared" / filename);
    candidates.push_back(exe_dir / L"shared" / filename);
    candidates.push_back(exe_dir / filename); // legacy colocated layout

    for (const auto& p : candidates) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec) || ec)
            continue;
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs)
            continue;
        std::string out((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (!out.empty())
            return out;
    }
    return {};
}

} // namespace

#ifdef FEATURE_CHAT_WEB

bool chat_web_available() { return true; }

std::string load_chat_web_html() { return read_utf8_file_next_to_exe(L"chat.html"); }
std::string load_settings_web_html() { return read_utf8_file_next_to_exe(L"settings.html"); }
std::string load_xblox_web_html() { return read_utf8_file_next_to_exe(L"xblox.html"); }

#else  // !FEATURE_CHAT_WEB

bool chat_web_available() { return false; }
std::string load_chat_web_html() { return {}; }
std::string load_settings_web_html() { return read_utf8_file_next_to_exe(L"settings.html"); }
std::string load_xblox_web_html() { return read_utf8_file_next_to_exe(L"xblox.html"); }

#endif

} // namespace pmui
