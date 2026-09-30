#include "stdafx.h"
#include "queue_path_enumeration.hpp"
#include "file_extensions.hpp"

#include <glob/glob.h>

#include <cwctype>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pmui::queue_paths {
namespace {

bool ext_is_image(const fs::path& path)
{
    std::wstring ext = path.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return pmui::is_image_ext(ext) || pmui::is_browser_image_ext(ext);
}

void append_image_files(
    const fs::path&    dir,
    bool               recursive,
    std::error_code&   ec,
    std::vector<fs::path>& out)
{
    const auto options = fs::directory_options::skip_permission_denied;
    try {
        if (recursive) {
            for (fs::recursive_directory_iterator it(dir, options, ec), end;
                 it != end; it.increment(ec)) {
                if (ec)
                    return;
                if (!it->is_regular_file())
                    continue;
                if (ext_is_image(it->path()))
                    out.push_back(it->path());
            }
        } else {
            for (fs::directory_iterator it(dir, options, ec), end; it != end; it.increment(ec)) {
                if (ec)
                    return;
                if (!it->is_regular_file())
                    continue;
                if (ext_is_image(it->path()))
                    out.push_back(it->path());
            }
        }
    } catch (const std::exception&) {
        // Symlinks, cloud placeholders, or permission oddities may throw; keep partial result.
    } catch (...) {
    }
}

} // namespace

std::vector<fs::path> image_files_in_directory(
    const fs::path&  dir,
    bool             recursive,
    std::error_code& ec)
{
    std::vector<fs::path> out;
    ec.clear();
    if (!fs::is_directory(dir, ec) || ec)
        return out;
    append_image_files(dir, recursive, ec, out);
    return out;
}

static bool ext_is_previewable(const fs::path& path)
{
    std::wstring ext = path.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return pmui::is_image_ext(ext) || pmui::is_browser_image_ext(ext)
        || pmui::is_text_preview_eligible_for_path(path.wstring())
        || pmui::is_viewer_3d_ext(ext) || pmui::is_viewer_pdf_ext(ext) || pmui::is_viewer_spreadsheet_ext(ext)
        || pmui::is_video_preview_eligible_for_path(path.wstring());
}

std::vector<fs::path> previewable_files_in_directory(const fs::path& dir, std::error_code& ec)
{
    std::vector<fs::path> out;
    ec.clear();
    if (!fs::is_directory(dir, ec) || ec)
        return out;
    const auto options = fs::directory_options::skip_permission_denied;
    try {
        for (fs::directory_iterator it(dir, options, ec), end; it != end; it.increment(ec)) {
            if (ec)
                return out;
            if (!it->is_regular_file())
                continue;
            if (ext_is_previewable(it->path()))
                out.push_back(it->path());
        }
    } catch (const std::exception&) {
    } catch (...) {
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().wstring() < b.filename().wstring();
    });
    return out;
}

std::vector<fs::path> previewable_paths_matching_filename_wildcard(const fs::path& pattern_path,
    std::error_code& ec)
{
    std::vector<fs::path> out;
    ec.clear();
    const std::wstring pws = pattern_path.wstring();
    const bool         has_glob = pws.find(L'*') != std::wstring::npos || pws.find(L'?') != std::wstring::npos
                          || pws.find(L"**") != std::wstring::npos;
    if (!has_glob)
        return out;

    fs::path pat = pattern_path;
    if (!pat.is_absolute()) {
        pat = fs::absolute(pat, ec);
        if (ec)
            ec.clear();
    }
    pat = pat.lexically_normal();

    const std::string pat_u8 = pmui::wide_to_utf8(pat.wstring());

    std::vector<fs::path> matched;
    try {
        if (pws.find(L"**") != std::wstring::npos)
            matched = glob::rglob(pat_u8);
        else
            matched = glob::glob(pat_u8);
    } catch (const std::exception&) {
        return out;
    } catch (...) {
        return out;
    }

    for (const auto& p : matched) {
        std::error_code stec;
        if (!fs::is_regular_file(p, stec) || stec)
            continue;
        if (ext_is_previewable(p))
            out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().wstring() < b.filename().wstring();
    });
    return out;
}

std::vector<fs::path> paths_to_enqueue(
    const fs::path&  p,
    bool             recursive_for_directories,
    std::error_code& ec)
{
    std::vector<fs::path> out;
    ec.clear();
    if (fs::is_directory(p, ec)) {
        if (ec)
            return out;
        return image_files_in_directory(p, recursive_for_directories, ec);
    }
    // Any non-directory path (file, missing, etc.): legacy AddFile accepted it as one row.
    out.push_back(p);
    return out;
}

std::vector<fs::path> image_paths_from_hdrop(HDROP hDrop)
{
    std::vector<fs::path> out;
    if (!hDrop)
        return out;

    const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
        if (len == 0) continue;
        std::wstring w(static_cast<size_t>(len) + 1, L'\0');
        ::DragQueryFileW(hDrop, i, w.data(), len + 1);
        w.resize(len);

        std::error_code ec;
        std::vector<fs::path> batch = paths_to_enqueue(fs::path(w), true, ec);
        out.insert(out.end(),
                   std::make_move_iterator(batch.begin()),
                   std::make_move_iterator(batch.end()));
    }
    return out;
}

} // namespace pmui::queue_paths
