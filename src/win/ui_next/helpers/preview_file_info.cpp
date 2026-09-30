#include "stdafx.h"
#include <gdiplus.h>
#include "helpers/preview_file_info.hpp"
#include "file_extensions.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cwctype>

#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;

namespace pmui {
namespace {

std::wstring FormatFileSize(uintmax_t bytes) {
    std::wostringstream oss;
    if (bytes < 1024)
        oss << bytes << L" B";
    else if (bytes < 1024 * 1024)
        oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << L" KB";
    else if (bytes < 1024ULL * 1024 * 1024)
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << L" MB";
    else
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << L" GB";
    return oss.str();
}

std::wstring FormatFileTime(const fs::file_time_type& ft) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    struct tm   local;
    localtime_s(&local, &tt);
    wchar_t buf[64];
    wcsftime(buf, 64, L"%Y-%m-%d %H:%M", &local);
    return buf;
}

} // namespace

std::wstring format_preview_file_info_text(const wchar_t* path)
{
    if (!path || !path[0]) return {};
    std::wostringstream line;
    const wchar_t*    sep = L"  \u00b7  "; // middle dot

    std::error_code ec;
    fs::path        fp(path);

    line << fp.filename().wstring() << sep << fp.parent_path().wstring();

    if (fs::exists(fp, ec)) {
        auto sz = fs::file_size(fp, ec);
        if (!ec) line << sep << FormatFileSize(sz);

        auto lwt = fs::last_write_time(fp, ec);
        if (!ec) line << sep << FormatFileTime(lwt);
    }

    std::wstring ext = fp.extension().wstring();
    for (auto& c : ext) c = (wchar_t)std::towlower((wint_t)(unsigned int)c);
    if (!is_image_ext(ext)) {
        line << sep << L"not an image";
        return line.str();
    }

    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR                   token = 0;
    Gdiplus::GdiplusStartup(&token, &si, nullptr);
    Gdiplus::Image* img = Gdiplus::Image::FromFile(path);
    if (img && img->GetLastStatus() == Gdiplus::Ok) {
        line << sep << img->GetWidth() << L"\u00d7" << img->GetHeight();
        auto pixFmt = img->GetPixelFormat();
        int  bpp    = (pixFmt >> 8) & 0xFF;
        line << sep << bpp << L"-bit";
    } else
        line << sep << L"unreadable";

    delete img;
    if (token) Gdiplus::GdiplusShutdown(token);
    return line.str();
}

std::wstring format_duplicate_group_file_info_text(
    const wchar_t*                    path,
    const std::vector<std::wstring>&  peers,
    const wchar_t*                    method,
    const wchar_t*                    key,
    const wchar_t*                    reportDetail)
{
    (void)reportDetail;
    if (!path || !path[0] || peers.empty()) {
        if (path && path[0]) return format_preview_file_info_text(path);
        return {};
    }
    std::wostringstream head;
    const wchar_t*      sep = L"  \u00b7  ";
    head << L"Dups" << sep << peers.size() << L" files";
    if (method && method[0]) head << sep << method;
    if (key && key[0]) head << sep << key;
    head << sep;
    return head.str() + format_preview_file_info_text(path);
}

} // namespace pmui
