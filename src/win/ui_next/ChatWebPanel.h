#ifndef PM_UI_CHATWEBPANEL_H
#define PM_UI_CHATWEBPANEL_H
//
// CChatWebPanel — WebView2-hosted chat dock.
//
// Web UI: apps/chat-next (webpack → dist/shared/chat.html),
// mirrored to a virtual https origin for persistent localStorage.
//
// Image rendering: the controller registers a virtual host
// "pm-files.local" mapped READ-ONLY to the host filesystem root, so the
// chat UI can render LLM-generated images (image_transform output) via
//   <img src="https://pm-files.local/<windows-path-with-forward-slashes>">.
//
// Compile-gated by FEATURE_CHAT_WEB. When OFF, this header still exists
// but the implementation is empty stubs (CMake doesn't add ChatWebPanel.cpp
// to the source list).
//
#include "stdafx.h"
#include "helpers/dock_helpers.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <string_view>
#include <vector>

class CWebViewManager;

class CChatWebView : public CWnd
{
public:
    CChatWebView();
    virtual ~CChatWebView() override;

    /// Called by CMainFrame on selection / folder change. Pushes an updated
    /// status bar into the web UI via window.pmChat.setStatus(...), and
    /// optionally the display language (same codes as App Settings) via
    /// window.pmChat.setLocale(...) so the web composer can localize labels.
    /// Explorer updates replace `selection` for the merged strip. New image paths are appended
    /// to the explicit extras list only when @p explorer_ctrl_additive is true and the new
    /// selection is a strict superset of the previous one (Ctrl+additive multi-select). Web
    /// `addContextPaths` (drag, paste, …) still merge into extras independently.
    void SetContext(const std::vector<std::wstring>& selection, const std::wstring& folder,
                    std::string_view display_language = {}, bool explorer_ctrl_additive = false);

    /// Re-apply a new display language from settings without re-reading Explorer context.
    void SetDisplayLanguage(std::string_view display_language);

    /// Append a transcript line from outside (e.g. provider-saved
    /// notification). Goes through window.pmChat.appendText({role,text}).
    void AppendTranscript(const std::wstring& role, const std::wstring& text);

    /// Programmatically focus the web composer's input (a JS shim does it).
    void FocusInput();

    /// Re-apply host / WebView2 colours, `prefers-color-scheme`, and web `setFontExtraPt`
    /// after App Settings (theme and/or font size).
    void RefreshChromeForTheme();

    /// Push `kind: "hostPixlwizAuth"` to the page (OAuth summary from disk; no secrets).
    void PostPixlwizAuthToWeb();

    /// Register this legacy WebView2 host as the named `chat` endpoint on the shared CWebView bus.
    void SetBusManager(CWebViewManager* manager);

    /// Post a raw UTF-8 JSON string to the page via PostWebMessageAsString.
    void PostToWeb(const std::string& json_utf8);

protected:
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    virtual int     OnCreate(CREATESTRUCT& cs) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CChatWebView(const CChatWebView&) = delete;
    CChatWebView& operator=(const CChatWebView&) = delete;

    friend class ChatShellDropTarget;       // Win32 `IDropTarget` in ChatWebPanel.cpp (OLE `CF_HDROP`)

    struct Impl;                          // PIMPL keeps WebView2 COM types out of the header
    std::unique_ptr<Impl> m_impl;
};

class CChatWebContainer : public CDockContainerBase
{
public:
    CChatWebContainer();
    virtual ~CChatWebContainer() override = default;
    CChatWebView& GetChatWebView() { return m_view; }

    /// `SysTabControl32` inset + blank-page paint used `window_bg` (#1e1e1e) while WebView2
    /// uses `web_surface_bg` (#1f2125) — reads as a ~4px “frame”. Align all tab chrome to
    /// the same colour as the embedded chat HTML.
    void RefreshTabTheme() override;

protected:
    virtual void PreCreate(CREATESTRUCT& cs) override;
    virtual void DrawTabBorders(CDC& dc, RECT& rc) override;
    virtual void DrawTabs(CDC& dc) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CChatWebContainer(const CChatWebContainer&) = delete;
    CChatWebContainer& operator=(const CChatWebContainer&) = delete;
    CChatWebView m_view;
};

class CDockChatWeb : public CDockPanelBase
{
public:
    CDockChatWeb();
    virtual ~CDockChatWeb() override = default;
    CChatWebContainer& GetChatWebContainer() { return m_container; }

private:
    CDockChatWeb(const CDockChatWeb&) = delete;
    CDockChatWeb& operator=(const CDockChatWeb&) = delete;
    CChatWebContainer m_container;
};

namespace pmui {

/// Rich HWND / layout snapshot for diagnosing WebView2 + dock chrome (written to `debug.json`
/// from **View → Debug**). Safe if the view is not created yet.
nlohmann::json debug_snapshot_chat_web_ui(const CChatWebView& view);

} // namespace pmui

#endif // PM_UI_CHATWEBPANEL_H
