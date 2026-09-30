#pragma once

#if defined(_WIN32)

namespace pmui {

/** Call once from `wWinMain` (4th arg) before UI / CLI init. */
void win32_register_startup_show_cmd(int nShowCmdFromWinMain);

/**
 * First `ShowWindow` for the main frame when `PreCreate` stripped `WS_VISIBLE`.
 * Honors `STARTUPINFOW::wShowWindow` / `nShowCmd` except inherited `SW_HIDE` (0), which would keep
 * the workbench invisible when launched from some hosts without a real console workaround.
 */
int win32_effective_main_frame_show_cmd();

} // namespace pmui

#endif
