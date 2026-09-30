// `CPmImageApp` is the **only** CWinApp for the Windows workbench. Every UI path
// uses it, including `pm-image --ui-preset=main` → `launch_ui_next.cpp` →
// `run_pm_image_ui()` → `app.Run()`. There is no separate “ui-next app” binary
// or second message pump.

#include "stdafx.h"
#include "App.h"

BOOL CPmImageApp::PreTranslateMessage(MSG& msg)
{
    // Run before CMessagePump::PreTranslateMessage (accelerator + parent walk).
    // The parent walk only reaches CMainFrame when msg.hwnd's chain includes the
    // frame; some hosted controls break that, which silently disabled app hotkeys.
    if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) {
        if (m_frame.GetHwnd() && m_frame.IsWindow()) {
            if (m_frame.TryProcessGlobalHotkeys(msg))
                return TRUE;
        }
    }
    if (msg.message == WM_XBUTTONDOWN && m_frame.GetHwnd() && m_frame.IsWindow()) {
        if (m_frame.TryProcessGlobalHotkeys(msg))
            return TRUE;
    }
    return CMessagePump::PreTranslateMessage(msg);
}

BOOL CPmImageApp::InitInstance()
{
    m_frame.Create();
    return TRUE;
}
