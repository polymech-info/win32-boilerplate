#include "pm_iexecute_map.h"

#include <objbase.h>
#include <windows.h>

namespace pm::iexecute {

static std::wstring guid_string(REFCLSID id) {
  wchar_t buf[64]{};
  (void)StringFromGUID2(id, buf, 64);
  return std::wstring(buf);
}

static DWORD reg_dword(HKEY h, const wchar_t* name) {
  DWORD  v = 0;
  DWORD  cb = sizeof v;
  DWORD  t  = 0;
  LSTATUS s = RegGetValueW(h, nullptr, name, RRF_RT_REG_DWORD, &t, &v, &cb);
  if (s != ERROR_SUCCESS) return 0xFFFFFFFFu;
  return v;
}

static std::wstring reg_sz(HKEY h, const wchar_t* name) {
  wchar_t buf[4096]{};
  DWORD   cb  = static_cast<DWORD>(sizeof buf);
  DWORD   t   = 0;
  LSTATUS s   = RegGetValueW(h, nullptr, name, RRF_RT_REG_SZ, &t, buf, &cb);
  if (s != ERROR_SUCCESS) return {};
  return std::wstring(buf);
}

bool load_payload(REFCLSID rclsid, Payload* out) {
  if (!out) return false;
  *out = {};
  const std::wstring  rel  = std::wstring(k_map_key_root()) + guid_string(rclsid);
  HKEY h = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, rel.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS)
    return false;
  const DWORD k = reg_dword(h, L"Kind");
  if (k == 0xFFFFFFFFu) {
    RegCloseKey(h);
    return false;
  }
  out->kind   = static_cast<Kind>(k);
  out->width  = static_cast<int>(reg_dword(h, L"W"));
  out->inplace = (reg_dword(h, L"In") != 0);
  out->preset_id = reg_sz(h, L"Preset");
  RegCloseKey(h);
  if (out->kind == Kind::None) return false;
  return true;
}

} // namespace pm::iexecute
