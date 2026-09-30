#pragma once
// Path / size / EXIF text for the central image preview (same content as the
// old File Info dock, without hosting a separate panel).

#include <string>
#include <vector>

namespace pmui {

std::wstring format_preview_file_info_text(const wchar_t* path);

/// Duplicate-group header + peer list + standard preview block for the selected file.
std::wstring format_duplicate_group_file_info_text(
    const wchar_t* path,
    const std::vector<std::wstring>& peers,
    const wchar_t* method,
    const wchar_t* key,
    const wchar_t* reportDetail);

} // namespace pmui
