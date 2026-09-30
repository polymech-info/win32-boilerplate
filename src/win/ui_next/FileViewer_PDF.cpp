// FileViewer_PDF.cpp — PDF preview via WebView2 + react-pdf (`FEATURE_VIEWER_WEB`).
#include "stdafx.h"
#include "FileViewer.h"
#include "helpers/markdown_preview_engine.hpp"
#include "file_extensions.hpp"
#include "llm/llm_fs_guard.hpp"
#if defined(FEATURE_VIEWER_WEB)
#include "win/viewers/text/ViewerWebPanel.h"
#include "win/viewers/viewer_web_contract.hpp"
#endif
#include <wxx_webbrowser.h>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

bool CFileViewer::LoadPdf(LPCWSTR path)
{
#if !defined(FEATURE_VIEWER_WEB)
    (void)path;
    return false;
#else
    namespace fs = std::filesystem;

    ClearText();
    ClearMarkdown();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label.Empty();
    m_previewPath.clear();
    ClearFileInfo();

    const fs::path filePath(path);

    std::string err_utf8;
    if (const std::string deny = media::llm::llm_fs_guard_deny_reason(filePath); !deny.empty())
        err_utf8 = deny;

    std::error_code    ec_sz;
    const std::uintmax_t sz = fs::file_size(filePath, ec_sz);

    std::wstring extw = filePath.extension().wstring();
    for (auto& ch : extw)
        ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));

    const std::uintmax_t max_b = pmui::viewer_document_max_bytes_for_ext(extw);

    if (err_utf8.empty() && !ec_sz && sz > max_b) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(sz) / (1024.0 * 1024.0));
        std::ostringstream mx;
        mx << std::fixed << std::setprecision(0) << (static_cast<double>(max_b) / (1024.0 * 1024.0));
        err_utf8 = "File too large to preview as PDF (" + oss.str() + " MB / max " + mx.str() + " MB)";
    }

    if (!pmui::markdown_preview_engine_is_webview2()) {
        ClearMarkdown();
        m_label = L"PDF preview requires WebView2";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create PDF preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::wstring folderW = filePath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    const std::wstring         pathW = filePath.wstring();
    std::vector<std::wstring> sel = {pathW};
    pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_pdf);
    pv->SetHostedFileClientPreview(pathW, ec_sz ? 0u : sz, pmui::viewer_web::k_viewer_kind_pdf, err_utf8);
    pv->RefreshChromeForTheme();
    LayoutViewerMdPanel();
    ::ShowWindow(*pv, SW_SHOW);
    if (m_browser) {
        auto* wb = static_cast<CWebBrowser*>(m_browser);
        if (wb->IsWindow()) ::ShowWindow(*wb, SW_HIDE);
    }
    m_mdMode = true;
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
    return true;
#endif
}
