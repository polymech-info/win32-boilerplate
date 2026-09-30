// FileViewer_Markdown.cpp — Markdown preview: WebView2 + apps/viewer-next, or legacy MSHTML;
// spreadsheet preview lives here (same WebView2 panel). PDF and 3D: FileViewer_PDF.cpp / FileViewer_3D.cpp.
#include "stdafx.h"
#include "FileViewer.h"
#include "constants.hpp"
#include "helpers/markdown_preview_base.hpp"
#include "helpers/markdown_preview_engine.hpp"
#include "file_extensions.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/theme.hpp"
#include "llm/llm_fs_guard.hpp"
#include "html/html.h"
#if defined(FEATURE_VIEWER_WEB)
#include "win/viewers/text/ViewerWebPanel.h"
#include "win/viewers/viewer_web_contract.hpp"
#endif
#include <wxx_webbrowser.h>
#include <cwctype>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

void CFileViewer::EnsureBrowser()
{
#if defined(FEATURE_VIEWER_WEB)
    DestroyViewerMdPanel();
#endif
    if (m_browser) return;
    auto* wb = new CWebBrowser();
    try {
        wb->Create(*this);
    } catch (...) {
        delete wb;
        return;
    }
    m_browser = wb;
    if (wb->IsWindow()) {
        ApplyPreviewChildTheming();
    }
    LayoutBrowser();
}

void CFileViewer::EnsureViewerMdPanel()
{
#if defined(FEATURE_VIEWER_WEB)
    if (m_browser) {
        if (GetHwnd()) ::KillTimer(GetHwnd(), TIMER_MD_CHROME);
        delete static_cast<CWebBrowser*>(m_browser);
        m_browser = nullptr;
    }
    if (m_viewerMdPanel) return;
    auto* p = new CViewerWebPanel();
    try {
        p->Create(*this);
    } catch (...) {
        delete p;
        return;
    }
    m_viewerMdPanel = p;
    p->SetBusManager(m_busManager);
    if (p->IsWindow()) {
        ApplyPreviewChildTheming();
    }
    LayoutViewerMdPanel();
#endif
}

void CFileViewer::LayoutBrowser()
{
    if (!m_browser) return;
    auto* wb = static_cast<CWebBrowser*>(m_browser);
    if (!wb->IsWindow()) return;
    CRect rc = GetClientRect();
    ::SetWindowPos(*wb, nullptr, 0, 0, rc.Width(), rc.Height(),
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

void CFileViewer::LayoutViewerMdPanel()
{
#if defined(FEATURE_VIEWER_WEB)
    if (!m_viewerMdPanel) return;
    auto* p = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!p->IsWindow()) return;
    CRect rc = GetClientRect();
    ::SetWindowPos(*p, nullptr, 0, 0, rc.Width(), rc.Height(),
                   SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void CFileViewer::DestroyViewerMdPanel()
{
#if defined(FEATURE_VIEWER_WEB)
    if (!m_viewerMdPanel) return;
    ReleaseViewerWebFolderMapping();
    delete static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    m_viewerMdPanel = nullptr;
#endif
}

bool CFileViewer::LoadMarkdown(LPCWSTR path)
{
    namespace fs = std::filesystem;
    ClearText();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label.Empty();
    m_previewPath.clear();
    ClearFileInfo();

    const fs::path mdPath(path);
    if (const std::string deny = media::llm::llm_fs_guard_deny_reason(mdPath); !deny.empty()) {
        ClearMarkdown();
        m_label = pmui::utf8_to_wide(deny).c_str();
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::ifstream ifs(mdPath, std::ios::binary);
    if (!ifs) {
        ClearMarkdown();
        return LoadText(path);
    }
    std::string md((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());
    if (md.size() > kTextLimitBytes) md.resize(kTextLimitBytes);

    if (md.size() >= 3 && (unsigned char)md[0] == 0xEF
                       && (unsigned char)md[1] == 0xBB
                       && (unsigned char)md[2] == 0xBF) {
        md.erase(0, 3);
    }

    const std::string title = mdPath.filename().string();
    const auto&       pal   = pmui::theme_palette();
    const html::HtmlTheme webTheme = pal.dark ? html::HtmlTheme::Dark : html::HtmlTheme::Light;

    std::string baseHrefUtf8;
#if defined(FEATURE_VIEWER_WEB)
    std::string base_href_https_assets;
#endif
    if (auto* resolve = pmui::markdown_base_href_resolver()) {
        baseHrefUtf8 = (*resolve)(path);
#if defined(FEATURE_VIEWER_WEB)
        if (!baseHrefUtf8.empty() && pmui::markdown_preview_engine_is_webview2() && baseHrefUtf8.size() >= 7
            && baseHrefUtf8.compare(0, 7, "file://") == 0) {
            // `pm-md` folder mapping for relative `<img src>` etc.; binary `fetch()` uses `vw_hosted_read` on Win32.
            base_href_https_assets = std::string("https://")
                + pmui::wide_to_utf8(std::wstring(pm::brand::k_markdown_assets_vhost_w)) + "/";
        }
#endif
    }

#if defined(FEATURE_VIEWER_WEB)
    if (pmui::markdown_preview_engine_is_webview2()) {
        if (!m_mdTempHtml.empty()) {
            std::error_code ec;
            fs::remove(m_mdTempHtml, ec);
            m_mdTempHtml.clear();
        }
        EnsureViewerMdPanel();
        auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
        if (pv && pv->IsWindow()) {
            std::wstring folderW = mdPath.parent_path().wstring();
            if (folderW.empty())
                folderW = L".";
            const std::wstring         pathW = mdPath.wstring();
            std::vector<std::wstring> sel = {pathW};
            pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_markdown);
            std::wstring assetsMapFolder;
            std::string  baseUrlForClient;
            if (!base_href_https_assets.empty()) {
                std::error_code ec;
                assetsMapFolder   = fs::absolute(mdPath.parent_path(), ec).wstring();
                baseUrlForClient = base_href_https_assets;
            }
            pv->SetMarkdownClientPreview(md, baseUrlForClient, assetsMapFolder);
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
        }
        DestroyViewerMdPanel();
    }
#endif

    const std::string body = html::md_to_html(md);
    const std::string* baseHrefPtr = nullptr;
    if (!baseHrefUtf8.empty())
        baseHrefPtr = &baseHrefUtf8;
    std::string fullDoc = html::wrap_markdown_html(
        body, title, webTheme,
        static_cast<uint32_t>(pal.window_bg),
        static_cast<uint32_t>(pal.window_fg),
        baseHrefPtr);

    EnsureBrowser();
    if (!m_browser) {
        return LoadText(path);
    }

    if (!m_mdTempHtml.empty()) {
        std::error_code ec; fs::remove(m_mdTempHtml, ec);
    }
    fs::path tmp = fs::temp_directory_path() /
                   (std::string(pm::brand::k_temp_md_file_prefix_u8) + std::to_string(::GetTickCount64()) + ".html");
    {
        std::ofstream ofs(tmp, std::ios::binary);
        if (!ofs.write(fullDoc.data(), (std::streamsize)fullDoc.size())) {
            return LoadText(path);
        }
    }
    m_mdTempHtml = tmp.wstring();

    auto* wb = static_cast<CWebBrowser*>(m_browser);
    LayoutBrowser();
    ::ShowWindow(*wb, SW_SHOW);
    wb->Navigate(m_mdTempHtml.c_str());
    ApplyPreviewChildTheming();
    ::SetTimer(GetHwnd(), TIMER_MD_CHROME, 150, nullptr);
#if defined(FEATURE_VIEWER_WEB)
    if (m_viewerMdPanel) {
        auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
        if (pv->IsWindow()) ::ShowWindow(*pv, SW_HIDE);
    }
#endif

    m_mdMode = true;
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
    return true;
}

bool CFileViewer::LoadSpreadsheet(LPCWSTR path)
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
    if (const std::string deny = media::llm::llm_fs_guard_deny_reason(filePath); !deny.empty()) {
        ClearMarkdown();
        m_label = pmui::utf8_to_wide(deny).c_str();
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::error_code    ec_sz;
    const std::uintmax_t sz = fs::file_size(filePath, ec_sz);

    std::wstring extw = filePath.extension().wstring();
    for (auto& ch : extw)
        ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));

    const std::uintmax_t max_b = pmui::viewer_document_max_bytes_for_ext(extw);

    std::string err_utf8;
    if (!ec_sz && sz > max_b) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(sz) / (1024.0 * 1024.0));
        std::ostringstream mx;
        mx << std::fixed << std::setprecision(0) << (static_cast<double>(max_b) / (1024.0 * 1024.0));
        err_utf8 = "File too large to preview as spreadsheet (" + oss.str() + " MB / max " + mx.str() + " MB)";
    }

    if (!pmui::markdown_preview_engine_is_webview2()) {
        ClearMarkdown();
        m_label = L"Spreadsheet preview requires WebView2";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create spreadsheet preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::wstring folderW = filePath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    const std::wstring         pathW = filePath.wstring();
    std::vector<std::wstring> sel = {pathW};
    pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_spreadsheet);
    pv->SetHostedFileClientPreview(pathW, ec_sz ? 0u : sz, pmui::viewer_web::k_viewer_kind_spreadsheet, err_utf8);
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

bool CFileViewer::LoadAgentFlow(LPCWSTR path)
{
    namespace fs = std::filesystem;
    ClearText();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label.Empty();
    m_previewPath.clear();
    ClearFileInfo();

    const fs::path jsonPath(path);

    std::error_code ec_sz;
    const std::uintmax_t sz = fs::file_size(jsonPath, ec_sz);
    if (ec_sz || sz == 0) {
        ClearMarkdown();
        return LoadText(path);
    }

    // Cap at text limit for inline JSON (5 MB)
    const std::uintmax_t max_b = kTextLimitBytes;
    std::string err_utf8;
    if (!ec_sz && sz > max_b) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(sz) / (1024.0 * 1024.0));
        std::ostringstream mx;
        mx << std::fixed << std::setprecision(0) << (static_cast<double>(max_b) / (1024.0 * 1024.0));
        err_utf8 = "File too large to visualize as flow (" + oss.str() + " MB / max " + mx.str() + " MB)";
    }

    // Read file content for inline text mode
    std::string jsonText;
    if (err_utf8.empty()) {
        std::ifstream ifs(jsonPath, std::ios::binary);
        if (!ifs) {
            ClearMarkdown();
            return LoadText(path);
        }
        jsonText.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (jsonText.size() > kTextLimitBytes)
            jsonText.resize(kTextLimitBytes);
        // Strip BOM if present
        if (jsonText.size() >= 3 && (unsigned char)jsonText[0] == 0xEF
            && (unsigned char)jsonText[1] == 0xBB
            && (unsigned char)jsonText[2] == 0xBF) {
            jsonText.erase(0, 3);
        }
    }

#if !defined(FEATURE_VIEWER_WEB)
    (void)err_utf8;
    (void)jsonText;
    ClearMarkdown();
    m_label = L"Agent flow preview requires WebView2";
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
    return false;
#else
    if (!pmui::markdown_preview_engine_is_webview2()) {
        ClearMarkdown();
        m_label = L"Agent flow preview requires WebView2";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create agent flow preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    std::wstring folderW = jsonPath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    const std::wstring         pathW = jsonPath.wstring();
    std::vector<std::wstring> sel = {pathW};
    pv->SetContext(sel, folderW, pmui::viewer_web::k_viewer_kind_agent_flow);

    const std::wstring fnameW = jsonPath.filename().wstring();
    const std::string  fnameU8 = pmui::wide_to_utf8(fnameW);

    if (!err_utf8.empty()) {
        // Error case - pass error without inline text
        pv->SetHostedFileClientPreview(pathW, ec_sz ? 0u : sz, pmui::viewer_web::k_viewer_kind_agent_flow, err_utf8);
    } else {
        // Success - pass inline JSON text
        pv->SetHostedFileClientPreview(pathW, static_cast<std::uintmax_t>(jsonText.size()),
            pmui::viewer_web::k_viewer_kind_agent_flow, {}, &jsonText);
    }
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
