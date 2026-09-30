#ifndef PM_UI_APP_H
#define PM_UI_APP_H

#include "Mainfrm.h"

class CPmImageApp : public CWinApp
{
public:
    CPmImageApp() = default;
    virtual ~CPmImageApp() override = default;

    CMainFrame& GetMainFrame() { return m_frame; }

protected:
    virtual BOOL InitInstance() override;
    virtual BOOL PreTranslateMessage(MSG& msg) override;

private:
    CPmImageApp(const CPmImageApp&) = delete;
    CPmImageApp& operator=(const CPmImageApp&) = delete;

    CMainFrame m_frame;
};

#endif // PM_UI_APP_H
