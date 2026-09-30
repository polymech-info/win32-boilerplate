#include "pm_iexecute_reg.h"
#include "pm_iexecute_map.h"
#include "constants.hpp"

#include <objbase.h>

namespace pm::iexecute {
namespace detail {
void reg_set_multi_sz(HKEY root, const wchar_t* sub, const wchar_t* name, const std::wstring& value) {
  HKEY h = nullptr;
  if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
    return;
  (void)RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(h);
}

void set_sz_key(HKEY h, const wchar_t* name, const std::wstring& v) {
  (void)RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                       static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}
void set_dword_key(HKEY h, const wchar_t* n, DWORD v) {
  (void)RegSetValueExW(h, n, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v);
}

void append_clsid_to_manifest(REFCLSID g) {
  wchar_t b[64]{};
  (void)StringFromGUID2(g, b, 64);
  std::wstring  cur;
  HKEY          h0 = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_iexecute_w, 0, KEY_READ, &h0) == ERROR_SUCCESS) {
    wchar_t buf[16384]{};
    DWORD   cb  = static_cast<DWORD>(sizeof buf);
    DWORD   t   = 0;
    LSTATUS s   = RegGetValueW(h0, nullptr, pm::brand::k_reg_clsidlist_value_w, RRF_RT_REG_SZ, &t, buf, &cb);
    if (s == ERROR_SUCCESS) cur = std::wstring(buf);
    RegCloseKey(h0);
  }
  if (!cur.empty()) cur += L';';
  cur += b;
  reg_set_multi_sz(HKEY_CURRENT_USER, pm::brand::k_reg_iexecute_w, pm::brand::k_reg_clsidlist_value_w, cur);
}

/** @return false if map or Inproc was not written; never appends to the ClsidList manifest in that case. */
bool reg_add_delegate(Kind k, int width, bool inplace, const std::wstring& preset_id, const std::wstring& dll_path,
                      std::wstring& out_braced) {
  out_braced.clear();
  if (dll_path.empty()
      || GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  GUID   g;
  (void)CoCreateGuid(&g);
  wchar_t  br[64]{};
  (void)StringFromGUID2(g, br, 64);
  const std::wstring  brs(br);
  const std::wstring  map_path = std::wstring(k_map_key_root()) + brs;
  const std::wstring  scls     = L"Software\\Classes\\CLSID\\" + brs + L"\\InprocServer32";

  HKEY h = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, map_path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr)
      != ERROR_SUCCESS) {
    return false;
  }
  set_dword_key(h, L"Kind", static_cast<DWORD>(k));
  set_dword_key(h, L"W", static_cast<DWORD>(width < 0 ? 0 : width));
  set_dword_key(h, L"In", inplace ? 1u : 0u);
  if (!preset_id.empty()) set_sz_key(h, L"Preset", preset_id);
  RegCloseKey(h);
  h = nullptr;

  HKEY h2 = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, scls.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h2, nullptr)
      != ERROR_SUCCESS) {
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, map_path.c_str());
    return false;
  }
  set_sz_key(h2, nullptr, dll_path);
  set_sz_key(h2, L"ThreadingModel", L"Apartment");
  RegCloseKey(h2);
  append_clsid_to_manifest(g);
  out_braced = brs;
  return true;
}
} // namespace detail
void reg_cleanup_user_entries() {
  (void)RegDeleteTreeW(HKEY_CURRENT_USER, pm::brand::k_reg_iexecute_map_w);
  static const wchar_t* k_legacy_braced[] = {L"{A0B1C2D3-4E5F-6789-AB01-CD23EF45AB01}", L"{A0B1C2D3-4E5F-6789-AB01-CD23EF45AB02}",
                                            L"{A0B1C2D3-4E5F-6789-AB01-CD23EF45AB03}", L"{A0B1C2D3-4E5F-6789-AB01-CD23EF45AB04}"};
  for (const wchar_t* b : k_legacy_braced) {
    std::wstring p = L"Software\\Classes\\CLSID\\";
    p += b;
    (void)RegDeleteTreeW(HKEY_CURRENT_USER, p.c_str());
  }
  HKEY h = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_iexecute_w, 0, KEY_READ, &h) == ERROR_SUCCESS) {
    wchar_t buf[32768]{};
    DWORD   cb  = static_cast<DWORD>(sizeof buf);
    DWORD   t   = 0;
    LSTATUS s   = RegGetValueW(h, nullptr, pm::brand::k_reg_clsidlist_value_w, RRF_RT_REG_SZ, &t, buf, &cb);
    RegCloseKey(h);
    h = nullptr;
    if (s == ERROR_SUCCESS) {
      std::wstring  all(buf);
      size_t        a = 0;
      while (a < all.size()) {
        size_t b2 = all.find(L';', a);
        if (b2 == std::wstring::npos) b2 = all.size();
        std::wstring one = all.substr(a, b2 - a);
        a = b2 + 1;
        if (one.empty() || one[0] != L'{') continue;
        std::wstring p = L"Software\\Classes\\CLSID\\" + one;
        (void)RegDeleteTreeW(HKEY_CURRENT_USER, p.c_str());
      }
    }
  }
  (void)RegCreateKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_iexecute_w, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h,
                        nullptr);
  if (h) {
    (void)RegSetValueExW(h, pm::brand::k_reg_clsidlist_value_w, 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
    RegCloseKey(h);
  }
}

bool build_delegate_table(const std::vector<int>& widths, const std::vector<std::wstring>& transform_preset_ids,
                          const std::vector<std::wstring>& custom_command_ids,
                          const std::wstring& execute_dll, DelegateTable& out) {
  out = {};
  auto fail = [&] {
    reg_cleanup_user_entries();
    out = {};
    return false;
  };
#if FEATURE_COMMAND_RESIZE
  for (int w : widths) {
    std::wstring a, b;
    if (!detail::reg_add_delegate(Kind::Resize, w, true, L"", execute_dll, a) || a.empty()) return fail();
    if (!detail::reg_add_delegate(Kind::Resize, w, false, L"", execute_dll, b) || b.empty()) return fail();
    out.resize.push_back(std::make_tuple(w, true, std::move(a)));
    out.resize.push_back(std::make_tuple(w, false, std::move(b)));
  }
#endif
#if FEATURE_COMMAND_COMPRESS
  if (!detail::reg_add_delegate(Kind::ConvertJpg, 0, true, L"", execute_dll, out.convert) || out.convert.empty())
    return fail();
#endif
  // OpenUi always enabled (basic "Open in Workbench" verb)
  if (!detail::reg_add_delegate(Kind::OpenUi, 0, true, L"", execute_dll, out.openui) || out.openui.empty()) return fail();
#if FEATURE_COMMAND_META
  if (!detail::reg_add_delegate(Kind::Meta, 0, true, L"", execute_dll, out.meta) || out.meta.empty()) return fail();
#endif
#if FEATURE_COMMAND_LLM
  if (!detail::reg_add_delegate(Kind::Chat, 0, true, L"", execute_dll, out.chat) || out.chat.empty()) return fail();
#endif
  // Viewer always enabled (basic viewer functionality)
  if (!detail::reg_add_delegate(Kind::Viewer, 0, true, L"", execute_dll, out.viewer) || out.viewer.empty()) return fail();
#if FEATURE_PIXLWIZ_SHARE
  if (!detail::reg_add_delegate(Kind::Share, 0, true, L"", execute_dll, out.share) || out.share.empty()) return fail();
#endif
#if FEATURE_COMMAND_TRANSFORM
  for (const std::wstring& pr : transform_preset_ids) {
    std::wstring cls;
    if (!detail::reg_add_delegate(Kind::Transform, 0, true, pr, execute_dll, cls) || cls.empty()) return fail();
    out.transform.push_back({pr, std::move(cls)});
  }
#endif
  for (const std::wstring& id : custom_command_ids) {
    std::wstring cls;
    if (!detail::reg_add_delegate(Kind::Custom, 0, true, id, execute_dll, cls) || cls.empty()) return fail();
    out.custom.push_back({id, std::move(cls)});
  }
  return true;
}

} // namespace pm::iexecute
