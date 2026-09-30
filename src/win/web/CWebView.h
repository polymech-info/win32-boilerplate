#ifndef PM_WIN_WEB_CWEBVIEW_H
#define PM_WIN_WEB_CWEBVIEW_H
//
// CWebView — flexible WebView2 wrapper for general-purpose web UI hosting.
//
// Supports both docked (child HWND) and popup (detached/transparent top-level)
// window modes. Accepts remote https:// URLs and local folder mappings via
// WebView2 virtual hosts.
//
// Host ↔ web communication:
//   Web → host : chrome.webview.postMessage(jsonString)   → onMessage callback
//   Host → web : CWebView::PostToWeb(json_utf8)           → cwWebSetHostMessageHandler (queued until registered)
//   JS console : console.{log,warn,error,info} bridged    → logger + onConsole callback
//   Launch argv: window.SYSTEM_CONTEXT.launch.arguments   → pm.args() (e.g. --prompt, --mic)
//
// Compile-gated by FEATURE_BROWSER (CMakeLists.txt option). The header is
// always present; the .cpp is added to sources only when FEATURE_BROWSER=ON.
//
#include "stdafx.h"
#include "helpers/dock_helpers.h"

#include <functional>
#include <memory>
#include <string>

namespace cweb {
/// Dark-themed placeholder page for opaque popups / docked panels.
const char* default_hello_html() noexcept;
/// Borderless placeholder with HTML title bar (min/max/close, drag).
const char* default_transparent_hello_html() noexcept;
/// Playground placeholder for pure popup mode.
const char* default_playground_html() noexcept;
} // namespace cweb

// ── CWebViewOptions ─────────────────────────────────────────────────────────

struct CWebViewOptions {
    enum class Mode {
        Docked,            ///< Child HWND hosted inside a dock panel (default).
        Popup,             ///< Detached top-level with native system title bar (WS_OVERLAPPEDWINDOW).
        PopupTransparent,  ///< Borderless popup — HTML title bar with min/max/close/drag.
        PopupPure,         ///< Borderless popup — web app owns all chrome; resize edges only.
    };

    Mode mode = Mode::Docked;

    /// Remote URL (https:// / http://) or empty to use html / mappedLocalUrl.
    std::wstring url;

    /// Inline HTML used when url is empty (NavigateToString — no localStorage persistence).
    std::string  html;

    /// Optional: local folder to map as a virtual host for local file access.
    std::wstring localFolder;

    /// Virtual hostname for localFolder (e.g. L"pm-browser.local").
    /// Navigate to https://pm-browser.local/<file> after mapping.
    std::wstring vhostName = L"pm-browser.local";

    /// Map fixed/remote drives as pm-files-c.local, pm-files-d.local, ... for local image/file thumbnails.
    bool mapFixedDriveFileHosts = false;

    /// Allow user-visible DevTools (F12). Default off for production panels.
    bool devToolsEnabled = false;

    /// Allow WebView2's built-in browser context menu.
    bool defaultContextMenusEnabled = true;

    /// Allow page navigation to external URLs (default: allow; set false to lock to vhost/initial URL).
    bool allowExternalNav = true;

    /// Allow web content to request resizing the host popup HWND via
    /// chrome.webview.postMessage({t:"cweb_resize", w, h}).
    /// Ignored for docked mode and clamped to sane bounds by host code.
    bool allowWebResizeHostWindow = false;

    /// If enabled, inject a one-shot auto-size reporter into each document.
    /// The page posts {t:"cweb_resize",w,h} as soon as content size stabilizes.
    /// Requires allowWebResizeHostWindow=true to have effect.
    bool autoResizeHostWindowFromWebContent = false;

    /// Enable native resize frame behavior for borderless popup modes.
    /// When false, resize-edge hit testing/inset is disabled.
    bool frame = true;

    /// Make host window click-through (`WS_EX_TRANSPARENT`).
    /// Useful for overlay UIs where pointer input should pass through.
    bool clickthrough = false;

    /// Best-effort modal popup behavior: disables the detected owner window
    /// while this popup is alive, then restores it on destroy.
    bool modal = false;

    /// Keep a native DWM drop shadow for popup windows.
    /// Useful for framed pure popups that otherwise look too flat.
    bool keepDwmShadow = false;

    /// Close popup windows when ESC is pressed (default: on).
    /// Ignored for docked mode.
    bool close_on_esc = true;

    // ── Callbacks (called on the UI thread) ───────────────────────────────

    /// Called when the web page posts a message via chrome.webview.postMessage(jsonStr).
    /// Receives the raw UTF-8 JSON string. Fired before the built-in type dispatch.
    std::function<void(const std::string& json_utf8)> onMessage;

    /// Called for every JS console.{log/warn/error/info} line captured by the bridge.
    /// level = "log" | "warn" | "error" | "info"; defaults to logger::* when nullptr.
    std::function<void(const std::string& level, const std::string& msg)> onConsole;

    // ── Presets ───────────────────────────────────────────────────────────

    /// Preset: standalone popup window with an ordinary system title bar (close / min / max).
    static CWebViewOptions ForPopup(bool devTools = false)
    {
        CWebViewOptions o;
        o.mode            = Mode::Popup;
        o.html            = cweb_default_hello_html_str();
        o.devToolsEnabled = devTools;
        return o;
    }

    /// Preset: docked child panel (default behaviour — no title bar of its own).
    static CWebViewOptions ForDocked(bool devTools = false)
    {
        CWebViewOptions o;
        o.mode            = Mode::Docked;
        o.html            = cweb_default_hello_html_str();
        o.devToolsEnabled = devTools;
        return o;
    }

    /// Preset: borderless popup with HTML title bar (min/max/close/drag).
    static CWebViewOptions ForTransparentPopup(bool devTools = false)
    {
        CWebViewOptions o;
        o.mode            = Mode::PopupTransparent;
        o.html            = cweb_default_transparent_hello_html_str();
        o.devToolsEnabled = devTools;
        return o;
    }

    /// Preset: borderless popup — web app owns all chrome. No title bar.
    /// Resize edges still work; the web content supplies its own close button.
    static CWebViewOptions ForPurePopup(bool devTools = false)
    {
        CWebViewOptions o;
        o.mode            = Mode::PopupPure;
        o.html            = cweb_playground_html_str();
        o.devToolsEnabled = devTools;
        o.allowWebResizeHostWindow = true;
        o.autoResizeHostWindowFromWebContent = true;
        return o;
    }

    /// Preset: floating popup loading viewer.html via pm-vw.invalid virtual host.
    static CWebViewOptions ForViewerPopup(bool transparent = false, bool devTools = false)
    {
        CWebViewOptions o;
        o.mode            = transparent ? Mode::PopupTransparent : Mode::Popup;
        o.url             = L"https://pm-vw.invalid/viewer.html";
        o.localFolder     = cweb_exe_folder();
        o.vhostName       = L"pm-vw.invalid";
        o.devToolsEnabled = devTools;
        return o;
    }

    // Public default-page accessors for callers that want to seed/clone HTML.
    static const char*   cweb_default_hello_html_str() noexcept;
    static const char*   cweb_default_transparent_hello_html_str() noexcept;
    static const char*   cweb_playground_html_str() noexcept;

private:
    static std::wstring  cweb_exe_folder();
};

// ── CWebView ─────────────────────────────────────────────────────────────────

/// CWebView — WebView2 host window (child or popup).
///
/// Typical docked usage:
/// @code
///   CWebViewOptions o;
///   o.url = L"https://example.com";
///   m_view.SetOptions(o);
///   m_view.Create(parentHwnd, CRect(0,0,0,0), nullptr, WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS);
/// @endcode
///
/// Typical popup usage:
/// @code
///   CWebViewOptions o;
///   o.mode = CWebViewOptions::Mode::Popup;
///   o.url = L"https://example.com";
///   m_popupView.SetOptions(o);
///   m_popupView.Create(nullptr, CRect(x,y,w,h), L"Browser", WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_CLIPSIBLINGS);
/// @endcode
class CWebView : public CWnd
{
public:
    CWebView();
    ~CWebView() override;

    /// Apply configuration. Call before or immediately after window creation.
    /// Safe to call multiple times; opts are applied lazily.
    void SetOptions(CWebViewOptions opts);

    /// Navigate to a URL. Queued until the WebView2 controller is ready.
    void Navigate(const std::wstring& url);

    /// Navigate to inline HTML. Queued until the WebView2 controller is ready.
    void NavigateHtml(const std::string& html_utf8);

    /// Post a raw UTF-8 JSON string to the page via PostWebMessageAsString.
    /// The page should listen for the `cwWebOnHostMessage` handler installed by the bridge script.
    /// No-op if the controller is not yet ready.
    void PostToWeb(const std::string& json_utf8);

    /// Thread-safe variant of PostToWeb — marshals to the WebView UI thread via PostMessage.
    void PostToWebFromAnyThread(std::string json_utf8);

    /// Execute JavaScript. Queued until the controller is ready, then executed in order.
    void RunScript(const std::wstring& js);

    /// Re-apply theme background colour, controller bounds, and preferred-colour-scheme.
    /// Call after a theme or DPI change.
    void RefreshChromeForTheme();

protected:
    void    PreCreate(CREATESTRUCT& cs) override;
    int     OnCreate(CREATESTRUCT& cs) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CWebView(const CWebView&) = delete;
    CWebView& operator=(const CWebView&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── CDockBrowserContainer ─────────────────────────────────────────────────────

/// Dock container that hosts a CWebView as its sole view.
class CDockBrowserContainer : public CDockContainerBase
{
public:
    CDockBrowserContainer();
    ~CDockBrowserContainer() override = default;

    CWebView& GetWebView() { return m_view; }

    void RefreshTabTheme() override;

protected:
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CDockBrowserContainer(const CDockBrowserContainer&) = delete;
    CDockBrowserContainer& operator=(const CDockBrowserContainer&) = delete;

    CWebView m_view;
};

// ── CDockWebBrowser ───────────────────────────────────────────────────────────

/// Top-level docker wrapping CDockBrowserContainer.
class CDockWebBrowser : public CDockPanelBase
{
public:
    CDockWebBrowser();
    ~CDockWebBrowser() override = default;

    CDockBrowserContainer& GetBrowserContainer() { return m_container; }
    CWebView&              GetWebView()           { return m_container.GetWebView(); }

private:
    CDockWebBrowser(const CDockWebBrowser&) = delete;
    CDockWebBrowser& operator=(const CDockWebBrowser&) = delete;

    CDockBrowserContainer m_container;
};

#endif // PM_WIN_WEB_CWEBVIEW_H
