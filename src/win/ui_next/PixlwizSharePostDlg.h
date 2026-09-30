#pragma once

#include <Windows.h>

#include <string>

/** Outcome of `RunPixlwizSharePostDialog` (IDD_PIXLWIZ_SHARE_POST). */
struct PixlwizSharePostFields {
    std::wstring title;
    std::wstring description;
    /// `public`, `listed`, or `private` — maps to POST /api/posts `settings.visibility`.
    std::string visibility = "public";
};

/** Modal dialog: title, description, private + in-feeds toggles. Returns true on Share (IDOK). */
bool RunPixlwizSharePostDialog(HWND parent, PixlwizSharePostFields& fields);

/** Themed success dialog (IDD_PIXLWIZ_SHARE_SUCCESS): post created, picture count, open URL / Close. */
void RunPixlwizShareSuccessDialog(HWND parent, const std::wstring& url_w, size_t picture_count);
