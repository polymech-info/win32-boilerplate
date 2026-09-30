#pragma once

#include "constants.hpp"
#include <guiddef.h>
#include <string>
#include <windows.h>

namespace pm::iexecute {

/** Value for Kind in HKCU\…\<app id>\IExecuteMap\<GUID>\Kind (see pm::brand) */
enum class Kind : DWORD {
  None  = 0,
  Resize = 1,
  ConvertJpg = 2,
  OpenUi     = 3,
  Meta       = 4,
  Chat       = 5,
  Transform  = 6,
  Share      = 7,
  /** Viewer workbench: `pm-image --ui-preset=viewer --src …` (Open-with ProgId + Explorer11). */
  Viewer     = 8,
  /** User-authored command from commands.json, invoked via the CLI custom-command wrapper. */
  Custom     = 9,
};

struct Payload {
  Kind           kind{Kind::None};
  int            width   = 0; // resize max width
  bool           inplace = true;
  std::wstring  preset_id;  // transform
};

// Defined inline so the main binary and IExecute DLL both link the same literals (from cmake/Branding.cmake).
inline const wchar_t* k_map_key_root() noexcept { return pm::brand::k_reg_iexecute_map_root_w; }
/** Value name on k_reg_iexecute_w (ClsidList). */
inline const wchar_t* k_manifest_key() noexcept { return pm::brand::k_reg_clsidlist_value_w; }

/** DLL: resolve CLSID → payload (written by pm-image register-explorer). */
bool load_payload(REFCLSID rclsid, Payload* out);

} // namespace pm::iexecute
