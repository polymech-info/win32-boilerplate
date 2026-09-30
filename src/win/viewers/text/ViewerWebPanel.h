#ifndef PM_UI_VIEWERWEBPANEL_H
#define PM_UI_VIEWERWEBPANEL_H
//
// CViewerWebPanel — WebView2 host for apps/viewer-next (markdown + pluggable viewers).
// Mirrors the Explorer-facing slice of CChatWebView: folder, selection, locale, theme.
//
// Compile-gated by FEATURE_VIEWER_WEB (requires FEATURE_CHAT_WEB for the WebView2 SDK).
//
#include "stdafx.h"
#include "win/viewers/viewer_web_contract.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ICoreWebView2Controller;
class CWebViewManager;

class CViewerWebPanel : public CWnd
{
public:
    CViewerWebPanel();
    ~CViewerWebPanel() override;

    /// Push Explorer context into `window.pmViewer.setStatus({...})` (selection, folder,
    /// viewerKind, optional features). @p viewer_kind selects the web module (markdown vs future three).
    void SetContext(const std::vector<std::wstring>& selection, const std::wstring& folder,
                    std::string_view               viewer_kind = pmui::viewer_web::k_viewer_kind_markdown,
                    std::string_view               display_language = {});

    void SetDisplayLanguage(std::string_view display_language);

    void SetViewerKind(std::string_view viewer_kind);

    /// Pushes raw UTF-8 markdown + optional `https://` base URL for relative assets into `setStatus.features`
    /// (`markdownText`, `markdownBaseUrl`). Maps @p markdown_assets_folder to `k_markdown_assets_vhost_w`; binary loads use `vw_hosted_read`.
    /// Call after `SetContext` clears prior features.
    void SetMarkdownClientPreview(std::string_view markdown_utf8, std::string_view markdown_base_url_https_utf8,
                                  const std::wstring& markdown_assets_folder);

    /// 3D preview: maps @p model_file_path parent to `k_markdown_assets_vhost_w`, sets native path for `vw_hosted_read`, and pushes
    /// `threeModelUrl` (+ `threeFileName`, sizes). When @p error_utf8 is non-empty, skips URL and sets `threeError`.
    void SetThreeClientPreview(const std::wstring& model_file_path, std::uintmax_t file_size_bytes,
                               std::string_view error_utf8 = {});
    /// Optional OpenSCAD source text for web-side parameter UI parsing.
    void SetOpenSCADSourceText(std::string_view scad_utf8);

    /// PDF, spreadsheet (csv/xls/xlsx), video, browser image, or plain text / source: same vhost mapping as 3D; @p viewer_kind is
    /// `k_viewer_kind_pdf`, `k_viewer_kind_spreadsheet`, `k_viewer_kind_video`, `k_viewer_kind_image`, or `k_viewer_kind_text`; pushes `hostedFileUrl` /
    /// `hostedFileName` (+ optional error/size caps).
    /// When @p inline_text_utf8 is non-null and @p viewer_kind is `k_viewer_kind_text`, pushes `hostedFileText`
    /// instead of `hostedFileUrl` (small bodies without bridge read).
    void SetHostedFileClientPreview(const std::wstring& file_path, std::uintmax_t file_size_bytes,
                                    std::string_view viewer_kind, std::string_view error_utf8 = {},
                                    const std::string* inline_text_utf8 = nullptr);

    void RefreshChromeForTheme();

    /// Post an arbitrary UTF-8 JSON string directly to the WebView2 content via
    /// `PostWebMessageAsString`. No-op if the WebView has not been initialised yet.
    void PostRawJson(std::string_view json_utf8);

    /// Register this embedded viewer as the named `preview` endpoint on the shared CWebView bus.
    void SetBusManager(CWebViewManager* manager);

    /// Returns the first path in the current selection (as passed to `SetContext`),
    /// or an empty string if no selection has been set yet.
    std::wstring SelectionPath() const;

protected:
    void    PreCreate(CREATESTRUCT& cs) override;
    int     OnCreate(CREATESTRUCT& cs) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CViewerWebPanel(const CViewerWebPanel&) = delete;
    CViewerWebPanel& operator=(const CViewerWebPanel&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void ResyncWebViewChrome(ICoreWebView2Controller* ctrl, HWND host);
    void RemoveChromeNavMouseSubclass();
    void SyncChromeNavMouseSubclassOnHost();
    void RegisterCentrePreviewNavAccelerator(ICoreWebView2Controller* ctrl);

    /// Chrome often maps mouse X1/X2 to @c WM_APPCOMMAND (browser back/forward) instead of @c WM_XBUTTONDOWN.
    bool TryConsumeCentrePreviewNavFromHostMouse(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& lrOut);

    static constexpr UINT_PTR kChromeNavMouseSubclassId = 0x706D7677u;

    static LRESULT CALLBACK ChromeNavMouseSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                       UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static BOOL CALLBACK EnumChromeNavMouseSubclass(HWND w, LPARAM lp);
};

#endif
