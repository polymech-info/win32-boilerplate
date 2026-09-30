#ifndef PM_WIN_WEB_CWEBVIEW_MANAGER_H
#define PM_WIN_WEB_CWEBVIEW_MANAGER_H

#include "win/web/CWebView.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class CWebViewManager {
public:
    struct ShowRequest {
        std::string     id;        // stable logical id (e.g. "viewer", "browser")
        std::wstring    title;
        CWebViewOptions opts;
        RECT            startupRect{100, 100, 900, 640}; // screen coords
        DWORD           exStyle = WS_EX_APPWINDOW;
        DWORD           style   = WS_POPUP | WS_VISIBLE;
    };

    struct HostCallbacks {
        std::function<void(const std::string& id,
                           const std::string& level,
                           const std::string& msg)> onConsole;
        std::function<void(const std::string& id,
                           const std::string& json_utf8)> onHostMessage;
    };

    CWebViewManager() = default;
    ~CWebViewManager() = default;

    void ConfigureHost(HWND hostHwnd, HostCallbacks callbacks);
    void RegisterExternal(const std::string& id, CWebView* view);
    void RegisterExternal(const std::string& id,
                          void* ownerToken,
                          std::function<bool()> isAlive,
                          std::function<void(const std::string& json_utf8)> postToWeb);
    void UnregisterExternal(const std::string& id);
    void UnregisterExternal(const std::string& id, void* ownerToken);
    void HandleExternalMessage(const std::string& fromId, const std::string& json_utf8);
    void ShowOrFocus(const ShowRequest& req);
    void RefreshThemeFromHost();
    bool IsVisible(const std::string& id) const;
    bool Exists(const std::string& id) const;
    void PostTo(const std::string& id, const std::string& json_utf8);
    void PostToFromAnyThread(const std::string& id, std::string json_utf8);
    void Broadcast(const std::string& json_utf8);
    void Close(const std::string& id);
    void CloseAll();
    static bool PickNativeFile(HWND owner, std::wstring& out_path);
    static bool PickNativeFolder(HWND owner, std::wstring& out_path);

private:
    struct Entry {
        std::string             id;
        std::unique_ptr<CWebView> view;
        ShowRequest             showReq{};
    };

    struct ExternalEntry {
        std::string id;
        void* ownerToken = nullptr; // non-owning: docked/embedded panels keep their normal lifetime.
        std::function<bool()> isAlive;
        std::function<void(const std::string& json_utf8)> postToWeb;
    };

    static std::string BuildBusEnvelope(const std::string& fromId,
                                        const std::string& json_utf8);
    void ApplyHostTheme(Entry& e) const;
    void ApplyHostIcons(HWND targetHwnd) const;
    void HandleBusMessage(const std::string& fromId, const std::string& json_utf8);
    bool HandleBuiltinMessage(const std::string& fromId, const std::string& json_utf8);
    static bool IsAlive(const Entry& e);
    static bool IsAlive(const ExternalEntry& e);

    HWND m_hostHwnd = nullptr;
    HostCallbacks m_callbacks{};
    std::unordered_map<std::string, std::unique_ptr<Entry>> m_entries;
    std::unordered_map<std::string, ExternalEntry> m_externalEntries;
    unsigned m_popupSeq = 1;
};

#endif // PM_WIN_WEB_CWEBVIEW_MANAGER_H
