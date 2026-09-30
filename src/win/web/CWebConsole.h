#ifndef PM_WIN_WEB_CWEB_CONSOLE_H
#define PM_WIN_WEB_CWEB_CONSOLE_H

#include "win/web/CWebView.h"

#include <map>
#include <memory>

class CWebViewManager;

namespace pm::pty {
class PtySession;
}

namespace pmui::web_console {

/// Returns true when a shell bundle or explicit PM_CONSOLE_URL is available.
bool available();

/// WebView2 options for the docked console panel.
CWebViewOptions MakeDockOptions(bool devTools = true);

/// WebView2 options for a future floating console popup.
CWebViewOptions MakePopupOptions(bool devTools = true);

} // namespace pmui::web_console

class CWebConsoleContainer : public CDockContainerBase
{
public:
    CWebConsoleContainer();
    ~CWebConsoleContainer() override;

    CWebView& GetWebView() { return m_view; }
    void SetBusManager(CWebViewManager* manager);
    void RefreshChromeForTheme();
    void RefreshTabTheme() override;

protected:
    void PreCreate(CREATESTRUCT& cs) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CWebConsoleContainer(const CWebConsoleContainer&) = delete;
    CWebConsoleContainer& operator=(const CWebConsoleContainer&) = delete;

    void HandleWebMessage(const std::string& json_utf8);
    void PostShellReady(const std::string& shell_id);
    void PostShellOutput(std::string shell_id, std::string data);
    void PostShellExit(std::string shell_id, int code);
    void PostShellError(std::string shell_id, std::string error);
    void CloseAllSessions();

    CWebView m_view;
    CWebViewManager* m_busManager = nullptr;
    std::map<std::string, std::unique_ptr<pm::pty::PtySession>> m_sessions;
};

class CDockWebConsole : public CDockPanelBase
{
public:
    CDockWebConsole();
    ~CDockWebConsole() override = default;

    CWebConsoleContainer& GetConsoleContainer() { return m_container; }
    CWebView&             GetWebView()          { return m_container.GetWebView(); }

private:
    CDockWebConsole(const CDockWebConsole&) = delete;
    CDockWebConsole& operator=(const CDockWebConsole&) = delete;

    CWebConsoleContainer m_container;
};

#endif // PM_WIN_WEB_CWEB_CONSOLE_H
