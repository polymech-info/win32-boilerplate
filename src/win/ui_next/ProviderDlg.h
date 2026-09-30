#ifndef PM_UI_PROVIDERDLG_H
#define PM_UI_PROVIDERDLG_H

#include <Windows.h>

/**
 * Show the modal "AI Provider Settings" dialog.
 * Lets the user enter/edit API keys for all known providers, choose the
 * active provider, and optionally override the default model and base URL.
 * Changes are persisted to the encrypted settings store on OK.
 *
 * Returns true if the user pressed Save (OK), false if they cancelled.
 */
bool ShowProviderSettingsDlg(HWND parent);

#endif // PM_UI_PROVIDERDLG_H
