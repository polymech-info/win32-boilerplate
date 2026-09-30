#include "stdafx.h"
#include "helpers/markdown_preview_base.hpp"
#include <filesystem>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

namespace pmui {

std::string default_markdown_base_href_utf8(LPCWSTR markdown_file_path)
{
    if (!markdown_file_path || !markdown_file_path[0])
        return {};

    namespace fs = std::filesystem;
    const fs::path p(markdown_file_path);
    fs::path parent = p.parent_path();
    if (parent.empty())
        return {};

    std::wstring wdir = parent.lexically_normal().wstring();
    if (wdir.empty())
        return {};
    if (wdir.back() != L'\\' && wdir.back() != L'/')
        wdir.push_back(L'\\');

    if (wdir.size() >= 4 && wdir[0] == L'\\' && wdir[1] == L'\\' && wdir[2] == L'?' && wdir[3] == L'\\')
        return {};

    wchar_t urlW[4096]{};
    DWORD   cch = static_cast<DWORD>(sizeof(urlW) / sizeof(urlW[0]));
    const HRESULT hr = UrlCreateFromPathW(wdir.c_str(), urlW, &cch, 0);
    if (FAILED(hr))
        return {};

    std::wstring wurl(urlW);
    if (wurl.empty())
        return {};
    if (wurl.back() != L'/')
        wurl.push_back(L'/');

    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wurl.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wurl.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

static MarkdownBaseHrefUtf8Fn* g_resolver = &default_markdown_base_href_utf8;

MarkdownBaseHrefUtf8Fn* markdown_base_href_resolver() noexcept
{
    return g_resolver;
}

void set_markdown_base_href_resolver(MarkdownBaseHrefUtf8Fn* fn) noexcept
{
    g_resolver = fn;
}

} // namespace pmui
