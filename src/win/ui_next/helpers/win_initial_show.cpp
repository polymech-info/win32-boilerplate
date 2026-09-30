#if defined(_WIN32)

#include "helpers/win_initial_show.hpp"

#include <windows.h>

namespace pmui {
namespace {

int g_show_cmd_from_win_main = SW_SHOWNORMAL;

} // namespace

void win32_register_startup_show_cmd(int nShowCmdFromWinMain)
{
    g_show_cmd_from_win_main = nShowCmdFromWinMain;
}

int win32_effective_main_frame_show_cmd()
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    ::GetStartupInfoW(&si);
    if (si.dwFlags & STARTF_USESHOWWINDOW) {
        const int w = static_cast<int>(static_cast<unsigned>(si.wShowWindow) & 0xFFFF);
        if (w == SW_HIDE || w == 0)
            return SW_SHOWNORMAL;
        return w;
    }
    if (g_show_cmd_from_win_main == SW_HIDE || g_show_cmd_from_win_main == 0)
        return SW_SHOWNORMAL;
    return g_show_cmd_from_win_main;
}

} // namespace pmui

#endif
