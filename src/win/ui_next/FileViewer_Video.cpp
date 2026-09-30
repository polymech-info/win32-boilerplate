// FileViewer_Video.cpp — video preview via WebView2 + viewer-next (native `<video>`; `FEATURE_VIEWER_WEB`).
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

bool CFileViewer::LoadVideo(LPCWSTR path)
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

    std::error_code      ec_sz;
    const std::uintmax_t sz = fs::file_size(filePath, ec_sz);
    // No file-size limit for video: the <video> element streams via HTTP range
    // requests from the virtual host mapping — the file is never loaded into memory.

    if (!pmui::markdown_preview_engine_is_webview2()) {
        ClearMarkdown();
        m_label = L"Video preview requires WebView2";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create video preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::wstring folderW = filePath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    const std::wstring         pathW = filePath.wstring();
    std::vector<std::wstring> sel = {pathW};
    pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_video);
    pv->SetHostedFileClientPreview(pathW, ec_sz ? 0u : sz, pmui::viewer_web::k_viewer_kind_video, err_utf8);
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
