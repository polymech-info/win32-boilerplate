#include "pm_image_run.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include "win/ui_next/helpers/win_initial_show.hpp"
#endif

#if defined(_WIN32) && !defined(PM_IMAGE_CONSOLE_MAIN)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR /*lpCmdLine*/, int nShowCmd) {
    pmui::win32_register_startup_show_cmd(nShowCmd);
    int          w_argc   = 0;
    LPWSTR*      w        = ::CommandLineToArgvW(GetCommandLineW(), &w_argc);
    if (!w) return 1;
    std::vector<std::string> u8;
    u8.reserve(static_cast<size_t>(w_argc));
    for (int i = 0; i < w_argc; i++) {
        if (!w[i] || w[i][0] == 0) {
            u8.emplace_back();
        } else {
            const int    n  = ::WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string  s  = n > 1 ? std::string(static_cast<size_t>(n - 1), '\0') : std::string{};
            if (n > 1)
                (void)::WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), n, nullptr, nullptr);
            u8.push_back(std::move(s));
        }
    }
    ::LocalFree(w);
    w = nullptr;

    std::vector<char*> a;
    a.reserve(static_cast<size_t>(w_argc) + 1u);
    for (int i = 0; i < w_argc; i++)
        a.push_back(const_cast<char*>(u8[static_cast<size_t>(i)].c_str()));
    a.push_back(nullptr);
    return pm_image_run(w_argc, a.data());
}
#else
int main(int argc, char** argv) { return pm_image_run(argc, argv); }
#endif
