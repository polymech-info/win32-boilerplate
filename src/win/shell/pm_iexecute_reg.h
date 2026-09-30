#pragma once

#include "pm_iexecute_map.h"

#include <string>
#include <tuple>
#include <vector>

namespace pm::iexecute {

/** Remove Inproc/Map/Manifest (and legacy static CLSIDs) for a clean re-register. */
void reg_cleanup_user_entries();

struct DelegateTable {
  // key: (width, inplace) -> braced clsid
  std::vector<std::tuple<int, bool, std::wstring>> resize; // not map - small N
  std::wstring                                    convert;
  std::wstring                                    openui;
  std::wstring                                    meta;
  std::wstring                                    chat;
  std::wstring                                    viewer;
  std::wstring                                    share;
  // preset_id -> braced
  std::vector<std::pair<std::wstring, std::wstring>> transform;
  // custom command id -> braced
  std::vector<std::pair<std::wstring, std::wstring>> custom;

  const std::wstring* resize_clsid(int w, bool in) const {
    for (const auto& t : resize) {
      if (std::get<0>(t) == w && std::get<1>(t) == in) return &std::get<2>(t);
    }
    return nullptr;
  }
  const std::wstring* transform_clsid(const std::wstring& preset) const {
    for (const auto& p : transform) {
      if (p.first == preset) return &p.second;
    }
    return nullptr;
  }
  const std::wstring* custom_clsid(const std::wstring& id) const {
    for (const auto& p : custom) {
      if (p.first == id) return &p.second;
    }
    return nullptr;
  }
};

/** Writes HKCU IExecuteMap + InprocServer32 for each verb. Returns false (after reg_cleanup) if any write fails. */
bool build_delegate_table(const std::vector<int>& widths, const std::vector<std::wstring>& transform_preset_ids,
                          const std::vector<std::wstring>& custom_command_ids,
                          const std::wstring& execute_dll, DelegateTable& out);

} // namespace pm::iexecute
