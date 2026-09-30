#pragma once
//
// AppSettingsDlg — global "App Settings" modal.
// Exposes:
//   - Display language: en / es / de / it / fr (ribbon + menus; restart may be offered)
//   - Theme:  System / Light / Dark
//   - Font size: Default / +1 / +2 / +3 / +4 (extra points on top of the
//                system message font; default is +2). Modal dialogs whose
//                .rc rows use tiny fixed heights must re-measure after WM_SETFONT
//                (`helpers/ui_dialog_relayout.hpp`: modals + settings scroll host).
//   - Shortcuts to AI Provider / Chat Provider dialogs
//   - Export / import full settings (optional PME1 encryption for export); CLI: top-level
//     `--settings <path>` reads from that file for one process without copying; `settings import <path>`
//     merges into the profile store; `settings export [path]` writes a copy (default path settings.json).
//   - Machine fingerprint (read-only + Copy) when FEATURE_LICENSE_FILE
//
// Persisted under settings.json["appearance"] via media::settings::save_appearance.
// On OK, the parent frame should re-apply the new font + theme to every panel.
//
#include <Windows.h>

/**
 * Returns true if the user clicked Save and settings were persisted.
 * If @p out_restarting is non-null and the user chose to restart after a display-language change,
 * sets *out_restarting = true (process is exiting; skip ApplyAppearance in the caller).
 */
bool ShowAppSettingsDlg(HWND parent, bool* out_restarting = nullptr);
