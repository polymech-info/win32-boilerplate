// FileViewer_HTML.cpp — HTML preview via WebView2 + viewer-next iframe (`FEATURE_VIEWER_WEB`).
//
// The HTML file is served from the markdown-assets virtual host so that relative
// links (CSS, images, scripts in the same folder) resolve correctly — the base URL
// equals the file's parent directory.  The viewer-next shell embeds it in an <iframe>
// rather than navigating WebView2 away from the viewer app.
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
#include <filesystem>
#include <string>
#include <vector>

bool CFileViewer::LoadHtml(LPCWSTR path)
{
#if !defined(FEATURE_VIEWER_WEB)
    // No WebView2 — fall back to showing the raw HTML source as plain text.
    return LoadText(path);
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

    // Security guard (same check as other loaders).
    std::string err_utf8;
    if (const std::string deny = media::llm::llm_fs_guard_deny_reason(filePath); !deny.empty())
        err_utf8 = deny;

    // File size is informational only — HTML is served via the virtual-host
    // mapping to the <iframe>, so there is no in-process read-size limit.
    std::error_code      ec_sz;
    const std::uintmax_t sz = fs::file_size(filePath, ec_sz);

    if (!pmui::markdown_preview_engine_is_webview2()) {
        // Fall back to source view when WebView2 is not available.
        return LoadText(path);
    }

    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create HTML preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::wstring folderW = filePath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    const std::wstring        pathW = filePath.wstring();
    std::vector<std::wstring> sel   = {pathW};

    // SetContext maps folderW to the markdown-assets virtual host so the iframe's
    // base URL equals the HTML file's parent directory.
    pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_html);
    pv->SetHostedFileClientPreview(pathW, ec_sz ? 0u : sz,
                                   pmui::viewer_web::k_viewer_kind_html, err_utf8);
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
