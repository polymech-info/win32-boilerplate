#include "app_exe_directory.hpp"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#else
#include <unistd.h>
#endif

namespace media::app {

namespace fs = std::filesystem;

fs::path exe_parent_directory()
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH + 1]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[4096]{};
    std::uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0)
        return {};
    std::error_code ec;
    const fs::path raw(buf);
    const fs::path canon = fs::weakly_canonical(raw, ec);
    if (ec || canon.empty())
        return raw.parent_path();
    return canon.parent_path();
#else
    char buf[4096]{};
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 2);
    if (len <= 0)
        return {};
    buf[len] = '\0';
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(std::string_view(buf, static_cast<std::size_t>(len))), ec);
    if (!ec && !canon.empty())
        return canon.parent_path();
    return fs::path(std::string_view(buf, static_cast<std::size_t>(len))).parent_path();
#endif
}

} // namespace media::app
