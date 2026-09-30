// FileViewer_BrowserImage.cpp — SVG / browser-compatible raster preview via WebView2.
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
#include <string>
#include <vector>

bool CFileViewer::LoadBrowserImage(LPCWSTR path)
{
#if !defined(FEATURE_VIEWER_WEB)
    namespace fs = std::filesystem;
    std::wstring extw = path ? fs::path(path).extension().wstring() : std::wstring();
    for (auto& ch : extw)
        ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
    if (extw == L".svg")
        return LoadText(path);
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

    std::error_code      ec_sz;
    const std::uintmax_t sz = fs::file_size(filePath, ec_sz);

    if (!pmui::markdown_preview_engine_is_webview2()) {
        std::wstring extw = filePath.extension().wstring();
        for (auto& ch : extw)
            ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
        if (extw == L".svg")
            return LoadText(path);
        m_label = L"Browser image preview requires WebView2";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create image preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::wstring folderW = filePath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    const std::wstring        pathW = filePath.wstring();
    std::vector<std::wstring> sel   = {pathW};
    pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_image);
    pv->SetHostedFileClientPreview(pathW, ec_sz ? 0u : sz,
                                   pmui::viewer_web::k_viewer_kind_image, err_utf8);
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
