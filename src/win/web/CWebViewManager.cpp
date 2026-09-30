#include "stdafx.h"
#include "win/web/CWebViewManager.h"
#include "helpers/theme.hpp"
#ifndef FEATURE_HOME_LLM_TOOLS
#define FEATURE_HOME_LLM_TOOLS 1
#endif
#if FEATURE_HOME_LLM_TOOLS
#include "llm/path_tool_catalog.hpp"
#endif

#include <nlohmann/json.hpp>
#include <shobjidl.h>

namespace {
static constexpr wchar_t kWin32xxClass[] = L"Win32++ Window";

std::string BuildThemeMessage()
{
    const auto& pal = pmui::theme_palette();
    nlohmann::json j;
    j["t"] = "cweb_theme";
    j["theme"] = pal.dark ? "dark" : "light";
    return j.dump();
}

std::string BuildProviderRpcReply(const std::string& rpcId, bool ok, nlohmann::json data, std::string error = {})
{
    nlohmann::json out;
    out["kind"] = "hostProviderRpc";
    out["id"] = rpcId;
    out["ok"] = ok;
    if (ok)
        out["data"] = std::move(data);
    else
        out["error"] = std::move(error);
    return out.dump();
}
}

void CWebViewManager::ConfigureHost(HWND hostHwnd, HostCallbacks callbacks)
{
    m_hostHwnd = hostHwnd;
    m_callbacks = std::move(callbacks);
}

bool CWebViewManager::PickNativeFile(HWND owner, std::wstring& out_path)
{
    out_path.clear();
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg)
        return false;
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts)))
        (void)dlg->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    (void)dlg->SetTitle(L"Select file");
    hr = dlg->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dlg->GetResult(&item);
        if (SUCCEEDED(hr) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                out_path.assign(path);
                ::CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return !out_path.empty();
}

bool CWebViewManager::PickNativeFolder(HWND owner, std::wstring& out_path)
{
    out_path.clear();
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg)
        return false;
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts)))
        (void)dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    (void)dlg->SetTitle(L"Select folder");
    hr = dlg->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dlg->GetResult(&item);
        if (SUCCEEDED(hr) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                out_path.assign(path);
                ::CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return !out_path.empty();
}

void CWebViewManager::RegisterExternal(const std::string& id, CWebView* view)
{
    if (id.empty() || !view)
        return;
    RegisterExternal(id, view,
        [view]() {
            return view && view->GetHwnd() && ::IsWindow(view->GetHwnd());
        },
        [view](const std::string& json_utf8) {
            if (view)
                view->PostToWeb(json_utf8);
        });
}

void CWebViewManager::RegisterExternal(
    const std::string& id,
    void* ownerToken,
    std::function<bool()> isAlive,
    std::function<void(const std::string& json_utf8)> postToWeb)
{
    if (id.empty() || !ownerToken || !isAlive || !postToWeb)
        return;
    ExternalEntry e;
    e.id = id;
    e.ownerToken = ownerToken;
    e.isAlive = std::move(isAlive);
    e.postToWeb = std::move(postToWeb);
    m_externalEntries[id] = std::move(e);
    if (IsAlive(m_externalEntries[id]))
        m_externalEntries[id].postToWeb(BuildThemeMessage());
}

void CWebViewManager::UnregisterExternal(const std::string& id)
{
    if (id.empty())
        return;
    m_externalEntries.erase(id);
}

void CWebViewManager::UnregisterExternal(const std::string& id, void* ownerToken)
{
    if (id.empty() || !ownerToken)
        return;
    auto it = m_externalEntries.find(id);
    if (it != m_externalEntries.end() && it->second.ownerToken == ownerToken)
        m_externalEntries.erase(it);
}

void CWebViewManager::HandleExternalMessage(const std::string& fromId, const std::string& json_utf8)
{
    if (fromId.empty())
        return;
    if (HandleBuiltinMessage(fromId, json_utf8))
        return;
    HandleBusMessage(fromId, json_utf8);
    if (m_callbacks.onHostMessage)
        m_callbacks.onHostMessage(fromId, json_utf8);
}

void CWebViewManager::ApplyHostIcons(HWND targetHwnd) const
{
    if (!targetHwnd || !::IsWindow(targetHwnd))
        return;

    HWND iconSource = m_hostHwnd;
    if (!iconSource || !::IsWindow(iconSource))
        iconSource = ::GetAncestor(targetHwnd, GA_ROOTOWNER);
    if (!iconSource || !::IsWindow(iconSource))
        iconSource = ::GetAncestor(targetHwnd, GA_ROOT);
    if (!iconSource || !::IsWindow(iconSource))
        return;

    HICON big = reinterpret_cast<HICON>(::SendMessageW(iconSource, WM_GETICON, ICON_BIG, 0));
    if (!big)
        big = reinterpret_cast<HICON>(::GetClassLongPtrW(iconSource, GCLP_HICON));
    if (big)
        (void)::SendMessageW(targetHwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));

    HICON hSmall = reinterpret_cast<HICON>(::SendMessageW(iconSource, WM_GETICON, ICON_SMALL, 0));
    if (!hSmall)
        hSmall = reinterpret_cast<HICON>(::SendMessageW(iconSource, WM_GETICON, ICON_SMALL2, 0));
    if (!hSmall)
        hSmall = reinterpret_cast<HICON>(::GetClassLongPtrW(iconSource, GCLP_HICONSM));
    if (hSmall)
        (void)::SendMessageW(targetHwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
}

void CWebViewManager::ApplyHostTheme(Entry& e) const
{
    if (!IsAlive(e))
        return;
    const auto& pal = pmui::theme_palette();
    pmui::apply_window_theme_recursive(e.view->GetHwnd(), pal.dark);
    e.view->RefreshChromeForTheme();
}

void CWebViewManager::RefreshThemeFromHost()
{
    const auto& pal = pmui::theme_palette();
    for (auto& kv : m_entries) {
        auto& e = *kv.second;
        if (!IsAlive(e))
            continue;
        ApplyHostTheme(e);
    }

    Broadcast(BuildThemeMessage());
}

bool CWebViewManager::IsAlive(const Entry& e)
{
    return e.view && e.view->GetHwnd() && ::IsWindow(e.view->GetHwnd());
}

bool CWebViewManager::IsAlive(const ExternalEntry& e)
{
    return e.isAlive && e.isAlive();
}

std::string CWebViewManager::BuildBusEnvelope(const std::string& fromId, const std::string& json_utf8)
{
    nlohmann::json out;
    out["t"] = "cweb_bus";
    out["from"] = fromId;
    try {
        out["payload"] = nlohmann::json::parse(json_utf8);
    } catch (...) {
        out["payload_raw"] = json_utf8;
    }
    return out.dump();
}

void CWebViewManager::HandleBusMessage(const std::string& fromId, const std::string& json_utf8)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_utf8);
    } catch (...) {
        return;
    }
    if (j.value("t", std::string{}) != "cweb_bus")
        return;

    const std::string to = j.value("to", std::string{"*"});
    const std::string out = BuildBusEnvelope(fromId, json_utf8);

    for (auto& kv : m_entries) {
        auto& id = kv.first;
        auto& e = *kv.second;
        if (!IsAlive(e) || id == fromId)
            continue;
        if (to != "*" && to != id)
            continue;
        e.view->PostToWeb(out);
    }

    for (auto it = m_externalEntries.begin(); it != m_externalEntries.end();) {
        auto& id = it->first;
        auto& e = it->second;
        if (!IsAlive(e)) {
            it = m_externalEntries.erase(it);
            continue;
        }
        if (id != fromId && (to == "*" || to == id) && e.postToWeb)
            e.postToWeb(out);
        ++it;
    }
}

bool CWebViewManager::HandleBuiltinMessage(const std::string& fromId, const std::string& json_utf8)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_utf8);
    } catch (...) {
        return false;
    }

    const std::string t = j.value("t", std::string{});
    if (t == "cweb_open_popup") {
        auto it = m_entries.find(fromId);
        if (it == m_entries.end() || !IsAlive(*it->second))
            return true;

        ShowRequest req = it->second->showReq;
        req.id = fromId + "-" + std::to_string(++m_popupSeq);
        req.opts.modal = false; // avoid stacking modal disables on the host window

        const int dx = 20 * static_cast<int>(m_popupSeq % 6);
        const int dy = 20 * static_cast<int>(m_popupSeq % 6);
        ::OffsetRect(&req.startupRect, dx, dy);
        ShowOrFocus(req);

        auto childIt = m_entries.find(req.id);
        if (childIt != m_entries.end() && IsAlive(*childIt->second)) {
            nlohmann::json ack;
            ack["t"] = "cweb_open_popup_result";
            ack["requested_by"] = fromId;
            ack["id"] = req.id;
            const auto hwndValue = static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(childIt->second->view->GetHwnd()));
            ack["hwnd"] = hwndValue;
            it->second->view->PostToWeb(ack.dump());
        }
        return true;
    }

    const std::string kind = j.value("kind", std::string{});
    const std::string method = j.value("method", std::string{});
    if (kind == "providerRpc" && method == "hostPathToolsCatalogGet") {
        const std::string rpcId = j.value("rpcId", std::string{});
#if FEATURE_HOME_LLM_TOOLS
        try {
            PostTo(fromId, BuildProviderRpcReply(rpcId, true, media::llm::path::tool_catalog_json()));
        } catch (const std::exception& e) {
            PostTo(fromId, BuildProviderRpcReply(rpcId, false, nlohmann::json::object(), e.what()));
        } catch (...) {
            PostTo(fromId, BuildProviderRpcReply(rpcId, false, nlohmann::json::object(), "hostPathToolsCatalogGet failed"));
        }
#else
        PostTo(fromId, BuildProviderRpcReply(rpcId, true, nlohmann::json{
            {"disabled", true},
            {"note", "Home LLM tools are disabled in this build."},
            {"tools", nlohmann::json::array()},
        }));
#endif
        return true;
    }

    return false;
}

void CWebViewManager::ShowOrFocus(const ShowRequest& req)
{
    if (req.id.empty())
        return;

    auto it = m_entries.find(req.id);
    if (it == m_entries.end()) {
        auto entry = std::make_unique<Entry>();
        entry->id = req.id;
        entry->showReq = req;
        entry->view = std::make_unique<CWebView>();

        CWebViewOptions opts = req.opts;
        const std::string id = req.id;
        const auto userOnConsole = opts.onConsole;
        const auto userOnMessage = opts.onMessage;
        opts.onConsole = [this, id, userOnConsole](const std::string& lvl, const std::string& msg) {
            if (m_callbacks.onConsole)
                m_callbacks.onConsole(id, lvl, msg);
            if (userOnConsole)
                userOnConsole(lvl, msg);
        };
        opts.onMessage = [this, id, userOnMessage](const std::string& json) {
            if (HandleBuiltinMessage(id, json))
                return;
            HandleBusMessage(id, json);
            if (m_callbacks.onHostMessage)
                m_callbacks.onHostMessage(id, json);
            if (userOnMessage)
                userOnMessage(json);
        };
        entry->view->SetOptions(std::move(opts));

        const int x = req.startupRect.left;
        const int y = req.startupRect.top;
        const int w = req.startupRect.right - req.startupRect.left;
        const int h = req.startupRect.bottom - req.startupRect.top;
        const HWND owner = (req.opts.modal && m_hostHwnd && ::IsWindow(m_hostHwnd)) ? m_hostHwnd : nullptr;
        entry->view->CreateEx(req.exStyle, kWin32xxClass, req.title.c_str(), req.style,
                              x, y, w, h, owner, nullptr, nullptr);
        ApplyHostIcons(entry->view->GetHwnd());
        ApplyHostTheme(*entry);
        m_entries.emplace(req.id, std::move(entry));
        return;
    }

    Entry& e = *it->second;
    if (!IsAlive(e)) {
        m_entries.erase(it);
        ShowOrFocus(req);
        return;
    }
    const HWND hwnd = e.view->GetHwnd();
    if (!::IsWindowVisible(hwnd)) {
        ::ShowWindow(hwnd, SW_SHOW);
    }
    if (req.opts.modal)
        ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    ::SetForegroundWindow(hwnd);
    ::BringWindowToTop(hwnd);
}

bool CWebViewManager::Exists(const std::string& id) const
{
    auto it = m_entries.find(id);
    return it != m_entries.end() && IsAlive(*it->second);
}

bool CWebViewManager::IsVisible(const std::string& id) const
{
    auto it = m_entries.find(id);
    if (it == m_entries.end() || !IsAlive(*it->second))
        return false;
    return ::IsWindowVisible(it->second->view->GetHwnd()) != FALSE;
}

void CWebViewManager::PostTo(const std::string& id, const std::string& json_utf8)
{
    auto it = m_entries.find(id);
    if (it != m_entries.end() && IsAlive(*it->second)) {
        it->second->view->PostToWeb(json_utf8);
        return;
    }
    auto ext = m_externalEntries.find(id);
    if (ext != m_externalEntries.end() && IsAlive(ext->second) && ext->second.postToWeb)
        ext->second.postToWeb(json_utf8);
}

void CWebViewManager::PostToFromAnyThread(const std::string& id, std::string json_utf8)
{
    auto it = m_entries.find(id);
    if (it != m_entries.end() && IsAlive(*it->second)) {
        it->second->view->PostToWebFromAnyThread(std::move(json_utf8));
        return;
    }
    auto ext = m_externalEntries.find(id);
    if (ext != m_externalEntries.end() && IsAlive(ext->second) && ext->second.postToWeb)
        ext->second.postToWeb(json_utf8);
}

void CWebViewManager::Broadcast(const std::string& json_utf8)
{
    for (auto& kv : m_entries) {
        if (IsAlive(*kv.second))
            kv.second->view->PostToWeb(json_utf8);
    }
    for (auto it = m_externalEntries.begin(); it != m_externalEntries.end();) {
        if (!IsAlive(it->second)) {
            it = m_externalEntries.erase(it);
            continue;
        }
        if (it->second.postToWeb)
            it->second.postToWeb(json_utf8);
        ++it;
    }
}

void CWebViewManager::Close(const std::string& id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end())
        return;
    if (IsAlive(*it->second))
        ::PostMessageW(it->second->view->GetHwnd(), WM_CLOSE, 0, 0);
}

void CWebViewManager::CloseAll()
{
    for (auto& kv : m_entries) {
        if (IsAlive(*kv.second))
            ::PostMessageW(kv.second->view->GetHwnd(), WM_CLOSE, 0, 0);
    }
}
