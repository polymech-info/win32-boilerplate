#include "stdafx.h"
#include "win/viewers/ViewerWebResource.h"

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

fs::path find_file_next_to_exe(const wchar_t* filename)
{
    const fs::path exe_dir = module_exe_dir();
    if (exe_dir.empty())
        return fs::path{};

    std::vector<fs::path> candidates;
    if (exe_dir.filename() == L"win-x64")
        candidates.push_back(exe_dir.parent_path() / L"shared" / filename);
    candidates.push_back(exe_dir / L"shared" / filename);
    candidates.push_back(exe_dir / filename); // legacy colocated layout

    for (const auto& p : candidates) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec) || ec)
            continue;
        return p;
    }
    return fs::path{};
}

std::string read_utf8_file_next_to_exe(const wchar_t* filename)
{
    const fs::path p = find_file_next_to_exe(filename);
    if (p.empty())
        return {};
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs)
        return {};
    std::string out((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return out;
}

} // namespace

#ifdef FEATURE_VIEWER_WEB

bool viewer_web_available() { return true; }

std::string load_viewer_web_html() { return read_utf8_file_next_to_exe(L"viewer.html"); }

std::wstring viewer_web_bundle_folder()
{
    const fs::path p = find_file_next_to_exe(L"viewer.html");
    if (p.empty())
        return {};
    return p.parent_path().wstring();
}

#else

bool viewer_web_available() { return false; }
std::string load_viewer_web_html() { return {}; }
std::wstring viewer_web_bundle_folder() { return {}; }

#endif

} // namespace pmui
