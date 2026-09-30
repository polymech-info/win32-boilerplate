// FileViewer_Text.cpp — plain-text preview loading for CFileViewer.

#include "stdafx.h"
#include "FileViewer.h"
#include "file_extensions.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/markdown_preview_engine.hpp"
#include "llm/llm_fs_guard.hpp"

#if defined(FEATURE_VIEWER_WEB)
#include "win/viewers/text/ViewerWebPanel.h"
#include "win/viewers/viewer_web_contract.hpp"
#endif

#include <cwctype>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

std::wstring decode_utf8_file_bytes_to_preview_wide(const std::vector<std::uint8_t>& bytes,
                                                    std::uintmax_t total_file_size, std::uintmax_t max_b)
{
    std::size_t off = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        off = 3;

    std::wstring w;
    {
        const int cb = static_cast<int>(bytes.size() - off);
        if (cb > 0) {
            int wc = ::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS,
                reinterpret_cast<const char*>(bytes.data() + off),
                cb, nullptr, 0);

            if (wc <= 0) {
                wc = ::MultiByteToWideChar(
                    CP_UTF8, 0,
                    reinterpret_cast<const char*>(bytes.data() + off),
                    cb, nullptr, 0);
            }

            if (wc > 0) {
                w.resize(static_cast<std::size_t>(wc));

                ::MultiByteToWideChar(
                    CP_UTF8, 0,
                    reinterpret_cast<const char*>(bytes.data() + off),
                    cb, w.data(), wc);
            }
        }
    }

    {
        std::wstring out;
        out.reserve(w.size() + w.size() / 16);
        for (std::size_t i = 0; i < w.size(); ++i) {
            wchar_t c = w[i];

            if (c == L'\n' && (i == 0 || w[i - 1] != L'\r')) {
                out.push_back(L'\r');
                out.push_back(L'\n');
            } else if (c != L'\0') {
                out.push_back(c);
            }
        }
        w.swap(out);
    }

    if (total_file_size > max_b) {
        wchar_t notice[160];
        swprintf_s(
            notice,
            L"\r\n\r\n[truncated — file is %zu KB; preview shows first %zu KB]\r\n",
            static_cast<std::size_t>(total_file_size / 1024),
            static_cast<std::size_t>(max_b / 1024));
        w.append(notice);
    }
    return w;
}

} // namespace

bool CFileViewer::LoadText(LPCWSTR path)
{
    ClearMarkdown();
    ClearText();

#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif

    DropImageAndStream();
    m_label.Empty();
    m_previewPath.clear();
    ClearFileInfo();

#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    m_rawStatusLabel.Empty();
#endif

    namespace fs = std::filesystem;
    const fs::path filePath(path);
    std::ifstream    ifs(filePath, std::ios::binary);
    if (!ifs) {
        m_label = L"Could not open file";
        ClearText();
        Invalidate(FALSE);
        return false;
    }

    ifs.seekg(0, std::ios::end);
    const std::uintmax_t size = static_cast<std::uintmax_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    const std::uintmax_t max_b = pmui::viewer_document_max_bytes_for_path_w(filePath.wstring());
    const std::string      fs_deny = media::llm::llm_fs_guard_deny_reason(filePath);

    if (fs_deny.empty()) {
        const std::size_t sniff_n =
            static_cast<std::size_t>(std::min<std::uintmax_t>(size, 4096));

        std::vector<std::uint8_t> sniff(sniff_n);

        if (sniff_n > 0)
            ifs.read(reinterpret_cast<char*>(sniff.data()),
                     static_cast<std::streamsize>(sniff_n));

        if (sniff_n > 0 && pmui::looks_like_binary(sniff.data(), sniff.size())) {
            m_label = L"Binary file (no text preview)";
            ClearText();
            Invalidate(FALSE);
            return false;
        }
    }

#if defined(FEATURE_VIEWER_WEB)
    if (pmui::markdown_preview_engine_is_webview2()) {
        std::string err_utf8;
        if (!fs_deny.empty())
            err_utf8 = fs_deny;
        else if (size > max_b) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
                << (static_cast<double>(size) / (1024.0 * 1024.0));
            std::ostringstream mx;
            mx << std::fixed << std::setprecision(0)
               << (static_cast<double>(max_b) / (1024.0 * 1024.0));
            err_utf8 = "File too large to preview as text (" + oss.str() +
                       " MB / max " + mx.str() + " MB)";
        }

        EnsureViewerMdPanel();

        auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);

        if (pv && pv->IsWindow()) {
            std::wstring folderW = filePath.parent_path().wstring();
            if (folderW.empty())
                folderW = L".";

            const std::wstring pathW = filePath.wstring();

            std::vector<std::wstring> sel = {pathW};

            pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_text);

            std::string utf8_inline;
            std::string err_host = err_utf8;
            if (err_host.empty() && size <= max_b) {
                if (size > 0) {
                    ifs.clear();
                    ifs.seekg(0, std::ios::beg);
                    std::vector<std::uint8_t> full(static_cast<std::size_t>(size));
                    if (!ifs.read(reinterpret_cast<char*>(full.data()), static_cast<std::streamsize>(size)))
                        err_host = "Could not read file";
                    else if (pmui::looks_like_binary(full.data(), full.size()))
                        err_host = "Binary file (no text preview)";
                    else {
                        const std::wstring w = decode_utf8_file_bytes_to_preview_wide(full, size, max_b);
                        utf8_inline = pmui::wide_to_utf8(w);
                    }
                }
            }

            if (err_host.empty() && size <= max_b)
                pv->SetHostedFileClientPreview(pathW, size, pmui::viewer_web::k_viewer_kind_text, {}, &utf8_inline);
            else
                pv->SetHostedFileClientPreview(pathW, size, pmui::viewer_web::k_viewer_kind_text, err_host);

            pv->RefreshChromeForTheme();

            LayoutViewerMdPanel();

            ::ShowWindow(*pv, SW_SHOW);

            if (m_browser) {
                auto* wb = static_cast<CWebBrowser*>(m_browser);
                if (wb->IsWindow())
                    ::ShowWindow(*wb, SW_HIDE);
            }

            m_mdMode = true;

            SetScrollSizes(CSize(0, 0));

            Invalidate(FALSE);

            return true;
        }

        DestroyViewerMdPanel();

        // Fall through: host `apps/viewer-next` unavailable — use read-only EDIT.
    }
#endif

    if (!fs_deny.empty()) {
        m_label = pmui::utf8_to_wide(fs_deny).c_str();
        ClearText();
        Invalidate(FALSE);
        return false;
    }

    ifs.clear();
    ifs.seekg(0, std::ios::beg);

    const std::size_t toRead =
        static_cast<std::size_t>(std::min<std::uintmax_t>(size, max_b));

    std::vector<std::uint8_t> bytes(toRead);

    if (toRead > 0)
        ifs.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(toRead));

    std::size_t off = 0;

    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF)
        off = 3;

    std::wstring w;

    {
        const int cb = static_cast<int>(bytes.size() - off);

        if (cb > 0) {
            int wc = ::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS,
                reinterpret_cast<const char*>(bytes.data() + off),
                cb, nullptr, 0);

            if (wc <= 0) {
                wc = ::MultiByteToWideChar(
                    CP_UTF8, 0,
                    reinterpret_cast<const char*>(bytes.data() + off),
                    cb, nullptr, 0);
            }

            if (wc > 0) {
                w.resize(static_cast<std::size_t>(wc));

                ::MultiByteToWideChar(
                    CP_UTF8, 0,
                    reinterpret_cast<const char*>(bytes.data() + off),
                    cb, w.data(), wc);
            }
        }
    }

    {
        std::wstring out;

        out.reserve(w.size() + w.size() / 16);

        for (std::size_t i = 0; i < w.size(); ++i) {
            wchar_t c = w[i];

            if (c == L'\n' && (i == 0 || w[i - 1] != L'\r')) {
                out.push_back(L'\r');
                out.push_back(L'\n');
            } else if (c != L'\0') {
                out.push_back(c);
            }
        }

        w.swap(out);
    }

    if (size > max_b) {
        wchar_t notice[160];

        swprintf_s(
            notice,
            L"\r\n\r\n[truncated — file is %zu KB; preview shows first %zu KB]\r\n",
            static_cast<std::size_t>(size / 1024),
            static_cast<std::size_t>(max_b / 1024));

        w.append(notice);
    }

    EnsureTextEdit();

    ::SendMessageW(m_hTextEdit, WM_SETREDRAW, FALSE, 0);

    ::SetWindowTextW(m_hTextEdit, w.c_str());

    ::SendMessageW(m_hTextEdit, EM_SETSEL, (WPARAM)0, (LPARAM)0);

    ::SendMessageW(m_hTextEdit, EM_SCROLLCARET, 0, 0);

    ::SendMessageW(m_hTextEdit, WM_SETREDRAW, TRUE, 0);

    m_textMode = true;

    LayoutTextEdit();

    ::ShowWindow(m_hTextEdit, SW_SHOW);

    SetScrollSizes(CSize(0, 0));

    Invalidate(FALSE);

    return true;
}
