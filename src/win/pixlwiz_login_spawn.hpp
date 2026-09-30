#pragma once

#include <Windows.h>

#include <string>

namespace pmui {

/** Result posted with @c UWM_PIXLWIZ_LOGIN_DONE (`lparam`). Main frame deletes after handling. */
struct PixlwizLoginDonePayload {
    bool         create_process_ok = false;
    DWORD        win32_error       = 0;
    DWORD        exit_code         = 1;
    std::wstring spawn_error; ///< CreateProcess / temp file setup (UTF-16)
    std::wstring stdio_log_utf16; ///< stdout+stderr from child (UTF-8 bytes decoded to UTF-16)
};

/**
 * Win32 **Pixlwiz → Login**: starts `pm-image --no-mcp login` with @c CREATE_NO_WINDOW; stdio → temp file;
 * posts @ref UWM_PIXLWIZ_LOGIN_DONE (worker thread). Same code path as CLI login (env / flags, then
 * `constants.hpp` defaults for issuer + client id — no `.env` required for those). When those env vars are unset,
 * the command line also includes `--issuer` / `--client-id` from the same constants. System browser opens for ZITADEL.
 */
void StartPixlwizLoginAsync(HWND main_frame_hwnd);

} // namespace pmui
