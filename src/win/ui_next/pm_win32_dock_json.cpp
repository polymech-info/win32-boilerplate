// Win32++ dock tree + tabbed-container state in settings.json (replaces HKCU\…\Dock Settings).
#include "stdafx.h"
#include "Mainfrm.h"
#include "pm_win32_dock_json.hpp"
#include "wxx_exception.h"
#include "win/settings_store.hpp"
#include "ui_log_file.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

using json    = nlohmann::json;
using Win32xx::CDockContainer;
using Win32xx::CDocker;
using Win32xx::CUserException;
using Win32xx::DockInfo;
using Win32xx::DockPtr;

namespace {

json rect_to_json(const RECT& r)
{
    return json::array({ r.left, r.top, r.right, r.bottom });
}

void rect_from_json(const json& o, RECT& r)
{
    r = {};
    if (!o.is_array() || o.size() < 4)
        return;
    r.left   = o[0].get<LONG>();
    r.top    = o[1].get<LONG>();
    r.right  = o[2].get<LONG>();
    r.bottom = o[3].get<LONG>();
}

bool dock_info_from_json(const json& o, DockInfo& di, std::string& err)
{
    try {
        di.dockStyle   = o.value("dockStyle", 0u);
        di.dockSize    = o.value("dockSize", 0);
        di.dockID      = o.value("dockID", 0);
        di.dockParentID = o.value("dockParentID", 0);
        di.isInAncestor = o.value("isInAncestor", false);
        di.isHidden     = o.value("isHidden", false);
        if (o.contains("rect"))
            rect_from_json(o["rect"], di.rect);
    } catch (const std::exception& ex) {
        err = ex.what();
        return false;
    }
    return true;
}

json dock_info_to_json(const DockInfo& di)
{
    json o;
    o["dockStyle"]   = di.dockStyle;
    o["dockSize"]    = di.dockSize;
    o["dockID"]      = di.dockID;
    o["dockParentID"] = di.dockParentID;
    o["isInAncestor"] = di.isInAncestor;
    o["isHidden"]  = di.isHidden;
    o["rect"]      = rect_to_json(di.rect);
    return o;
}

const char* dock_id_label(int id)
{
    // IDs are stable (Mainfrm.h); use literals for 6 so this file compiles when
    // FEATURE_NODES omits the constexpr member.
    using F = CMainFrame;
    if (id == F::DOCK_ID_QUEUE)
        return "queue";
    if (id == F::DOCK_ID_LOG)
        return "log";
    if (id == F::DOCK_ID_SETTINGS)
        return "settings";
    if (id == F::DOCK_ID_FILEINFO)
        return "fileinfo";
    if (id == 6)
        return "nodes";
    if (id == 7)
        return "filetree (IExplorerBrowser)";
    if (id == F::DOCK_ID_FINDRESULTS)
        return "findresults";
    if (id == F::DOCK_ID_CHAT)
        return "chat";
    if (id == F::DOCK_ID_DUPLICATERESULTS)
        return "duplicateresults";
    return "?";
}

template <typename F>
void log_dock_panel_ms(int dock_id, const char* phase, F&& do_work)
{
    const auto t0 = std::chrono::steady_clock::now();
    do_work();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                        .count();
    pmui::ui_log_file_eventf("OnInitialUpdate: dock panel id=%d (%s) %s %lld ms", dock_id, dock_id_label(dock_id),
        phase, (long long)ms);
}

} // namespace

namespace pm::win32_dock {
namespace {
const char* slot_or_main(const char* s)
{
    return (s && s[0]) ? s : "main";
}

std::string dock_id_list_for_log(const std::vector<UINT>& ids)
{
    std::string out;
    for (UINT id : ids) {
        if (!out.empty())
            out += ",";
        out += std::to_string(id);
    }
    return out;
}
} // namespace

bool load_win32_dock_doc(json& out, std::string& err, const char* workbench_slot)
{
    const char* slot = slot_or_main(workbench_slot);
    out = json();
    err.clear();
    if (media::settings::ui_reset_session())
        return true;
    std::string raw;
    if (!media::settings::load_settings_utf8(raw, err))
        return false;
    if (raw.empty())
        return true;
    try {
        const auto j = json::parse(raw);
        if (j.contains("workbench") && j["workbench"].is_object() && j["workbench"].contains(slot)
            && j["workbench"][slot].is_object() && j["workbench"][slot].contains("win32_dock")) {
            out = j["workbench"][slot]["win32_dock"];
        } else if (std::strcmp(slot, "main") == 0 && j.contains("win32_dock")) {
            out = j["win32_dock"];
        }
    } catch (const std::exception& e) {
        err = std::string("win32_dock: ") + e.what();
        return false;
    }
    return out.is_object() || out.is_null();
}

bool save_win32_dock_doc(const json& doc, std::string& err, const char* workbench_slot)
{
    const char* slot = slot_or_main(workbench_slot);
    if (!doc.is_object() && !doc.is_null()) {
        err = "win32_dock: expected object or null";
        return false;
    }
    if (doc.is_null())
        return true;
    std::string raw;
    json        j = json::object();
    if (media::settings::load_settings_utf8(raw, err) && !raw.empty()) {
        try {
            j = json::parse(raw);
        } catch (const std::exception&) {
            j = json::object();
        }
    } else if (!err.empty()) {
        return false;
    }
    if (!j.contains("workbench") || !j["workbench"].is_object())
        j["workbench"] = json::object();
    if (!j["workbench"].contains(slot) || !j["workbench"][slot].is_object())
        j["workbench"][slot] = json::object();
    j["workbench"][slot]["win32_dock"] = doc;
    if (std::strcmp(slot, "main") == 0 && j.contains("win32_dock"))
        j.erase("win32_dock");
    return media::settings::save_settings_utf8(j.dump(2), err);
}

void erase_from_settings(const char* workbench_slot)
{
    const char* slot = slot_or_main(workbench_slot);
    std::string err, raw;
    if (!media::settings::load_settings_utf8(raw, err))
        return;
    if (raw.empty())
        return;
    try {
        json j = json::parse(raw);
        if (!j.is_object())
            return;
        bool changed = false;
        if (std::strcmp(slot, "main") == 0 && j.contains("win32_dock")) {
            j.erase("win32_dock");
            changed = true;
        }
        if (j.contains("workbench") && j["workbench"].is_object() && j["workbench"].contains(slot)
            && j["workbench"][slot].is_object() && j["workbench"][slot].contains("win32_dock")) {
            j["workbench"][slot].erase("win32_dock");
            changed = true;
        }
        if (!changed)
            return;
        (void)media::settings::save_settings_utf8(j.dump(2), err);
    } catch (...) {
    }
}

// Tab order + active page per tab group (JSON `containers` array).
bool apply_containers_from_json(CMainFrame& root, const json& container_array, std::string& err)
{
    if (!container_array.is_array() || container_array.empty())
        return true;

    try {
        CDocker& d = static_cast<CDocker&>(root);
        for (size_t containerIndex = 0; containerIndex < container_array.size(); ++containerIndex) {
            const auto& c = container_array[containerIndex];
            auto fail = [&](const std::string& reason) {
                err = "LoadContainer[" + std::to_string(containerIndex) + "]: " + reason;
                return false;
            };
            if (!c.is_object())
                return fail("entry is not an object");

            std::vector<UINT> tabOrder;
            if (c.contains("tabs") && c["tabs"].is_array()) {
                for (const auto& t : c["tabs"])
                    tabOrder.push_back(t.get<UINT>());
            }
            const DWORD   parentID    = c.value("parent_dock_id", 0u);
            const DWORD   active_dock = c.value("active_dock_id", 0u);
            if (tabOrder.empty() && active_dock == 0)
                continue;

            CDocker* pDocker = d.GetDockFromID(static_cast<int>(parentID));
            if (!pDocker)
                pDocker = &d;
            auto* pParentContainer = pDocker->GetContainer();
            if (!pParentContainer)
                return fail("parent container missing for dock id=" + std::to_string(parentID));
            const auto liveCount = pParentContainer->GetAllContainers().size();
            if (tabOrder.size() != liveCount) {
                return fail("tab count mismatch parent=" + std::to_string(parentID)
                    + " saved=[" + dock_id_list_for_log(tabOrder) + "] live_count=" + std::to_string(liveCount));
            }

            for (UINT tab = 0; tab < tabOrder.size(); ++tab) {
                CDocker* pOld = d.GetDockFromView(pParentContainer->GetContainerFromIndex((int)tab));
                if (!pOld)
                    return fail("live tab has no docker at index=" + std::to_string(tab));
                const UINT       oldID = static_cast<UINT>(pOld->GetDockID());
                const auto it        = std::find(tabOrder.begin(), tabOrder.end(), oldID);
                if (it == tabOrder.end())
                    return fail("live dock id=" + std::to_string(oldID)
                        + " missing from saved tabs [" + dock_id_list_for_log(tabOrder) + "]");
                const UINT   oldTab = static_cast<UINT>(it - tabOrder.begin());
                if (tab >= pParentContainer->GetAllContainers().size())
                    return fail("tab index out of range: " + std::to_string(tab));
                if (oldTab >= pParentContainer->GetAllContainers().size())
                    return fail("saved tab index out of range: " + std::to_string(oldTab));
                if (tab != oldTab)
                    pParentContainer->SwapTabs((int)tab, (int)oldTab);
            }

            if (active_dock) {
                CDocker* a = d.GetDockFromID((int)active_dock);
                if (!a)
                    return fail("active dock id missing: " + std::to_string(active_dock));
                if (CDockContainer* pC = a->GetContainer()) {
                    int page = pC->GetContainerIndex(pC);
                    if (page >= 0)
                        pC->SelectPage(page);
                    else
                        return fail("active dock id not in its container: " + std::to_string(active_dock));
                } else
                    return fail("active dock has no container: " + std::to_string(active_dock));
            }
        }
    } catch (const CUserException&) {
        if (err.empty())
            err = "LoadContainer: JSON parse/apply";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    return true;
}

/**
 * Apply dock tree from JSON.
 * @param frame The main frame.
 * @param doc The JSON document.
 * @param err The error string.
 * @return True if the dock tree was applied successfully, false otherwise.
 */
bool apply_dock_tree_from_json(CMainFrame& frame, const json& doc, std::string& err)
{
    if (!doc.is_object() || doc.value("v", 0) != kJsonVersion)
        return false;
    if (!doc.contains("children") || !doc["children"].is_array()) {
        err = "win32_dock: missing children[]";
        return false;
    }
    std::list<DockInfo> dockList;
    std::set<int> seenDockIds;
    for (const auto& o : doc["children"]) {
        if (!o.is_object()) {
            err = "win32_dock: child not object";
            return false;
        }
        DockInfo di{};
        if (!dock_info_from_json(o, di, err))
            return false;
        if (di.dockID == 0)
            continue;
        if (!seenDockIds.insert(di.dockID).second) {
            pmui::ui_log_file_eventf(
                "win32_dock: duplicate child dockID=%d ignored while loading", di.dockID);
            continue;
        }
        dockList.push_back(di);
    }
    const DWORD ancestorStyle = doc.value("ancestor_style", 0u);
    if (dockList.empty()) {
        frame.SetDockStyle(ancestorStyle);
        return true;
    }

    std::map<int, int> tabParents;
    if (doc.contains("containers") && doc["containers"].is_array()) {
        try {
            for (const auto& c : doc["containers"]) {
                if (!c.is_object() || !c.contains("tabs") || !c["tabs"].is_array())
                    continue;
                const int parentID = c.value("parent_dock_id", 0);
                if (parentID == 0)
                    continue;
                for (const auto& t : c["tabs"]) {
                    const int tabID = t.get<int>();
                    if (tabID != parentID)
                        tabParents[tabID] = parentID;
                }
            }
        } catch (const std::exception& e) {
            err = std::string("win32_dock containers: ") + e.what();
            return false;
        }
    }
    for (DockInfo& di : dockList) {
        const auto tabParent = tabParents.find(di.dockID);
        if (tabParent == tabParents.end())
            continue;
        di.dockParentID = tabParent->second;
        di.isInAncestor = false;
        di.dockStyle &= ~static_cast<DWORD>(0xF);
        di.dockStyle |= Win32xx::DS_DOCKED_CONTAINER;
    }

    // Recreate dock tree from `DockInfo` list (inlines here so the friended `apply` can call
    // CMainFrame::NewDockerFromID; nested helpers in this file are not friended).
    const auto t_apply0 = std::chrono::steady_clock::now();
    try {
        frame.SetDockStyle(ancestorStyle);
        std::set<int> skippedHiddenDocks;
        for (const DockInfo& di : dockList) {
            if (di.isHidden) {
                skippedHiddenDocks.insert(di.dockID);
                continue;
            }
            if ((di.dockParentID == 0) || (di.isInAncestor)) {
                if ((di.dockStyle & 0xF) || (di.isInAncestor)) {
                    log_dock_panel_ms(di.dockID, "docked (NewDocker+AddDockedChild)", [&] {
                        if (frame.GetDockFromID(di.dockID) != nullptr)
                            return;
                        DockPtr   docker  = frame.NewDockerFromID(di.dockID);
                        CDocker*  pDocker = docker.get();
                        if (!pDocker)
                            throw CUserException();
                        CDocker* pDock
                            = frame.AddDockedChild(std::move(docker), di.dockStyle, di.dockSize, di.dockID);
                    });
                } else {
                    log_dock_panel_ms(di.dockID, "undocked (NewDocker+AddUndockedChild)", [&] {
                        if (frame.GetDockFromID(di.dockID) != nullptr)
                            return;
                        DockPtr docker = frame.NewDockerFromID(di.dockID);
                        CDocker*  pDocker = docker.get();
                        if (!pDocker)
                            throw CUserException();
                        frame.AddUndockedChild(std::move(docker), di.dockStyle, di.dockSize, di.rect, di.dockID,
                            di.isHidden);
                    });
                }
            }
        }
        for (auto it = dockList.begin(); it != dockList.end();) {
            if (((*it).dockParentID == 0) || ((*it).isInAncestor))
                it = dockList.erase(it);
            else
                ++it;
        }
        while (dockList.size() > 0) {
            bool found = false;
            for (auto it = dockList.begin(); it != dockList.end(); ++it) {
                const DockInfo di  = *it;
                if (di.isHidden || skippedHiddenDocks.count(di.dockParentID) != 0) {
                    skippedHiddenDocks.insert(di.dockID);
                    found = true;
                    dockList.erase(it);
                    break;
                }
                CDocker*       pDockParent = frame.GetDockFromID(di.dockParentID);
                if (pDockParent != nullptr) {
                    log_dock_panel_ms(di.dockID, "child (NewDocker+AddDockedChild)", [&] {
                        if (frame.GetDockFromID(di.dockID) != nullptr)
                            return;
                        DockPtr docker = frame.NewDockerFromID(di.dockID);
                        if (docker.get() == nullptr)
                            throw CUserException();
                        CDocker* pDockChild
                            = pDockParent->AddDockedChild(std::move(docker), di.dockStyle, di.dockSize, di.dockID);
                    });
                    found   = true;
                    dockList.erase(it);
                    break;
                }
            }
            if (!found)
                throw CUserException();
            if (!frame.VerifyDockers())
                throw CUserException();
        }
        const auto apply_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t_apply0)
                                  .count();
        pmui::ui_log_file_eventf("OnInitialUpdate: apply_dock_tree_from_json total (all panels above) %lld ms",
            (long long)apply_ms);
    } catch (const CUserException&) {
        err = "dock tree";
        frame.CloseAllDockers();
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        frame.CloseAllDockers();
        return false;
    }
    return true;
}

static bool append_one_container(
    CMainFrame& f, CDockContainer* pContainer, const std::set<int>& savedDockIds, json& out_arr, std::string& err)
{
    if (!pContainer) {
        err = "no container";
        return false;
    }
    CDocker* pD = f.GetDockFromView(pContainer);
    if (pD == nullptr) {
        err = "GetDockFromView";
        return false;
    }
    const DWORD             parentId = (DWORD)pD->GetDockID();
    if (savedDockIds.count(static_cast<int>(parentId)) == 0) {
        err = "container parent dock not serialized: " + std::to_string(parentId);
        return false;
    }
    pD = f.GetDockFromView(pContainer->GetActiveContainer());
    const DWORD act = pD ? (DWORD)pD->GetDockID() : 0u;
    if (act && savedDockIds.count(static_cast<int>(act)) == 0) {
        err = "container active dock not serialized: " + std::to_string(act);
        return false;
    }
    json        tabs  = json::array();
    for (size_t u2 = 0; u2 < pContainer->GetAllContainers().size(); ++u2) {
        CDockContainer* pTab = pContainer->GetContainerFromIndex((int)u2);
        if (!pTab) {
            err = "ContainerFromIndex";
            return false;
        }
        pD = f.GetDockFromView(pTab);
        if (!pD) {
            err = "GetDockFromView tab";
            return false;
        }
        const DWORD tabId = (DWORD)pD->GetDockID();
        if (savedDockIds.count(static_cast<int>(tabId)) == 0) {
            err = "container tab dock not serialized: " + std::to_string(tabId);
            return false;
        }
        tabs.push_back(tabId);
    }
    json c;
    c["parent_dock_id"]  = parentId;
    c["active_dock_id"]  = act;
    c["tabs"]            = std::move(tabs);
    out_arr.push_back(std::move(c));
    return true;
}

nlohmann::json serialize_dock_to_json(CMainFrame& f)
{
    json doc;
    try {
        if (!f.VerifyDockers())
            return json::object();
    } catch (...) {
        return json::object();
    }
    const std::vector<CDocker*>      sorted = f.GetSortedDockersForSave();
    auto is_visible_layout_member = [&f](CDocker* p) {
        return p && p->IsWindow() && f.IsPanelVisible(p);
    };
    std::vector<DockInfo>            all;
    std::set<int>                    savedDockIds;
    for (CDocker* p : sorted) {
        if (!is_visible_layout_member(p))
            continue;
        DockInfo di{};
        di.dockID    = p->GetDockID();
        if (savedDockIds.count(di.dockID) != 0) {
            pmui::ui_log_file_eventf(
                "serialize_dock_to_json: duplicate live docker id=%d ignored", di.dockID);
            continue;
        }
        di.dockStyle = p->GetDockStyle();
        di.dockSize  = p->GetDockSize();
        di.rect      = p->GetWindowRect();
        if (p->GetDockParent())
            di.dockParentID = p->GetDockParent()->GetDockID();
        di.isInAncestor = (p->GetDockParent() == f.GetDockAncestor());
        // A saved visible-layout member can have a hidden HWND when it is an
        // inactive tab. Do not mark it hidden here: restore skips hidden dockers.
        di.isHidden     = false;
        all.push_back(di);
        savedDockIds.insert(di.dockID);
    }
    doc["v"]   = kJsonVersion;
    doc["ancestor_style"] = f.GetDockStyle();
    json  children = json::array();
    for (const auto& di : all)
        children.push_back(dock_info_to_json(di));
    doc["children"] = std::move(children);
    std::string              cerr;
    json                     cont  = json::array();
    std::set<CDockContainer*> seen;
    if (f.GetContainer() && seen.insert(f.GetContainer()).second) {
        if (!append_one_container(f, f.GetContainer(), savedDockIds, cont, cerr) && !cerr.empty())
            pmui::ui_log_file_eventf("serialize_dock_to_json: skipped root container: %s", cerr.c_str());
    }
    for (CDocker* pDocker : sorted) {
        if (!is_visible_layout_member(pDocker))
            continue;
        if (CDockContainer* pC = pDocker->GetContainer()) {
            if (!((pDocker->GetDockStyle() & Win32xx::DS_DOCKED_CONTAINER)) && seen.insert(pC).second) {
                if (!append_one_container(f, pC, savedDockIds, cont, cerr) && !cerr.empty())
                    pmui::ui_log_file_eventf(
                        "serialize_dock_to_json: skipped container for dock id=%d: %s",
                        pDocker->GetDockID(), cerr.c_str());
            }
        }
    }
    doc["containers"] = std::move(cont);
    return doc;
}

} // namespace pm::win32_dock
