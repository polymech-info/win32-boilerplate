#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace media::win {

/// Capture the on-screen pixels covered by @p hwnd (i.e. whatever the user
/// would see if they took a screen photo) and save them as a PNG to
/// @p out_path. Uses GetDC(NULL) + BitBlt(SRCCOPY | CAPTUREBLT) and
/// GDI+ Bitmap::Save under the hood.
///
/// Notes:
///  - The capture includes anything overlapping the window (other apps,
///    cursor crumbs around it, etc.) — by design. For a "private" capture of
///    only our own window contents, switch to PrintWindow with
///    PW_RENDERFULLCONTENT, but layered / DWM children may render incorrectly.
///  - GDI+ is started up locally per call (cheap; mirrors FileInfoPanel).
///  - On failure, @p err_utf8 is populated with a short reason. The function
///    never throws.
bool capture_window_to_png(HWND hwnd,
                           const std::wstring& out_path,
                           std::string& err_utf8);

} // namespace media::win
