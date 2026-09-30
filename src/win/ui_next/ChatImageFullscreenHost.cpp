// ChatImageFullscreenHost.cpp — full-screen chat image viewer built on CFileViewer (FileViewer).
#include "stdafx.h"
#include "ChatImageFullscreenHost.h"
#include "FileViewer.h"
#include "helpers/text_conv.hpp"
#include "logger/logger.h"

#include <cstddef>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

static constexpr UINT WM_PM_CHAT_FS_NAV = WM_APP + 0x5D10;

static bool chat_fs_image_path_exists(const std::wstring& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    const fs::path fp(path);
    return fs::exists(fp, ec) && fs::is_regular_file(fp, ec);
}

/// Starting at @p start (inclusive), walk forward with wrap; set @p out_index and return true if any path exists.
static bool chat_fs_find_first_existing(const std::vector<std::wstring>& paths, size_t start, size_t& out_index)
{
    const size_t n = paths.size();
    for (size_t k = 0; k < n; ++k) {
        const size_t i = (start + k) % n;
        if (chat_fs_image_path_exists(paths[i])) {
            out_index = i;
            return true;
        }
    }
    return false;
}

/// Forwards Esc / arrows to the host so navigation works while the preview has focus.
class CChatFsImagePreview : public CFileViewer {
public:
    CChatFsImagePreview() { SetFullButtonTogglesFrameFullscreen(false); }

protected:
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override
    {
        if (msg == WM_KEYDOWN) {
            const UINT vk = static_cast<UINT>(wparam);
            if (vk == VK_ESCAPE || vk == VK_LEFT || vk == VK_RIGHT) {
                if (HWND p = ::GetParent(*this))
                    ::PostMessageW(p, WM_PM_CHAT_FS_NAV, vk, 0);
                return 0;
            }
        }
        return CFileViewer::WndProc(msg, wparam, lparam);
    }
};

class CChatImageFullscreenHost : public CWnd {
public:
    void ShowForImages(HWND owner, std::vector<std::wstring> paths, size_t index, bool constrainToMainFrame);

protected:
    int OnCreate(CREATESTRUCT& cs) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    std::vector<std::wstring> m_paths;
    size_t                    m_index = 0;
    CChatFsImagePreview       m_preview;

    void ReloadCurrent();
    void StepGallery(int delta);
    bool EnsureCurrentPathExistsOnDisk();
};

void CChatImageFullscreenHost::ShowForImages(HWND owner, std::vector<std::wstring> paths, size_t index,
                                             bool constrainToMainFrame)
{
    if (paths.empty()) return;
    if (index >= paths.size()) index = 0;

    if (IsWindow()) {
        if (m_preview.IsWindow())
            m_preview.Destroy();
        Destroy();
    }

    m_paths = std::move(paths);
    m_index = index;
    if (m_index >= m_paths.size()) m_index = 0;

    {
        size_t found = 0;
        if (!chat_fs_find_first_existing(m_paths, m_index, found)) {
            logger::warn("[chat-fs] no image paths exist on disk (transcript may reference deleted files)");
            m_paths.clear();
            return;
        }
        m_index = found;
    }

    RECT r{};
    int  x = 0;
    int  y = 0;
    int  cw = 0;
    int  ch = 0;

    if (constrainToMainFrame && owner) {
        if (HWND frame = ::GetAncestor(owner, GA_ROOT); frame && ::IsWindow(frame) && ::GetWindowRect(frame, &r)) {
            x  = r.left;
            y  = r.top;
            cw = r.right - r.left;
            ch = r.bottom - r.top;
        }
    }

    if (cw < 80 || ch < 80) {
        HMONITOR hMon = ::MonitorFromWindow(owner ? owner : ::GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (!hMon || !::GetMonitorInfoW(hMon, &mi))
            return;
        r    = mi.rcMonitor;
        x    = r.left;
        y    = r.top;
        cw   = r.right - r.left;
        ch   = r.bottom - r.top;
    }

    try {
        CreateEx(0, _T("Win32++ Window"), L"", WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE, x, y, cw, ch,
                 owner ? owner : nullptr, nullptr, nullptr);
    } catch (const CException& e) {
        const std::wstring werr = std::wstring(e.GetText()) + L"\n" + std::wstring(e.GetErrorString());
        logger::warn(std::string("[chat-fs] CreateEx: ") + pmui::wide_to_utf8(werr));
        return;
    }

    if (IsWindow()) {
        (void)::SetForegroundWindow(*this);
        (void)::BringWindowToTop(*this);
    }
}

int CChatImageFullscreenHost::OnCreate(CREATESTRUCT&)
{
    const CRect rc = GetClientRect();
    if (!m_preview.Create(*this)) {
        logger::warn("[chat-fs] CFileViewer::Create failed");
        PostMessage(WM_CLOSE);
        return -1;
    }
    m_preview.SetWindowPos(nullptr, 0, 0, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_SHOWWINDOW);
    ReloadCurrent();
    m_preview.SetFocus();
    return 0;
}

bool CChatImageFullscreenHost::EnsureCurrentPathExistsOnDisk()
{
    if (m_paths.empty() || m_index >= m_paths.size()) return false;
    if (chat_fs_image_path_exists(m_paths[m_index])) return true;
    size_t found = 0;
    if (chat_fs_find_first_existing(m_paths, m_index, found)) {
        m_index = found;
        return true;
    }
    return false;
}

void CChatImageFullscreenHost::ReloadCurrent()
{
    if (m_paths.empty() || !m_preview.IsWindow()) return;
    if (m_index >= m_paths.size()) return;
    if (!EnsureCurrentPathExistsOnDisk()) {
        logger::warn("[chat-fs] current image missing on disk and no alternatives in list");
        PostMessage(WM_CLOSE);
        return;
    }
    m_preview.LoadPicture(m_paths[m_index].c_str());
    m_preview.SetFileInfoFromPath(m_paths[m_index].c_str());
}

void CChatImageFullscreenHost::StepGallery(int delta)
{
    const size_t n = m_paths.size();
    if (n == 0 || delta == 0) return;

    if (n == 1) {
        if (chat_fs_image_path_exists(m_paths[0])) {
            m_index = 0;
            ReloadCurrent();
            if (m_preview.IsWindow()) m_preview.SetFocus();
        }
        return;
    }

    const int dir = (delta > 0) ? 1 : -1;
    size_t      pos = m_index;
    for (size_t tries = 0; tries < n; ++tries) {
        if (dir > 0)
            pos = (pos + 1) % n;
        else
            pos = (pos + n - 1) % n;
        if (chat_fs_image_path_exists(m_paths[pos])) {
            m_index = pos;
            ReloadCurrent();
            if (m_preview.IsWindow()) m_preview.SetFocus();
            return;
        }
    }
    logger::warn("[chat-fs] arrow navigation: no existing files in gallery list");
}

LRESULT CChatImageFullscreenHost::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        if (msg == WM_PM_CHAT_FS_NAV) {
            const UINT vk = static_cast<UINT>(wparam);
            if (vk == VK_ESCAPE) {
                Destroy();
                return 0;
            }
            if (vk == VK_LEFT) {
                StepGallery(-1);
                return 0;
            }
            if (vk == VK_RIGHT) {
                StepGallery(1);
                return 0;
            }
            return 0;
        }
        if (msg == WM_ERASEBKGND) {
            if (HDC hdc = reinterpret_cast<HDC>(wparam); hdc && IsWindow()) {
                CRect r = GetClientRect();
                ::FillRect(hdc, &r, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
            }
            return 1;
        }
        if (msg == WM_SIZE && m_preview.IsWindow()) {
            const int cx = LOWORD(lparam);
            const int cy = HIWORD(lparam);
            if (cx > 0 && cy > 0)
                m_preview.SetWindowPos(nullptr, 0, 0, cx, cy, SWP_NOZORDER);
            return 0;
        }
        if (msg == WM_CLOSE) {
            Destroy();
            return 0;
        }
        if (msg == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE && m_preview.IsWindow())
            m_preview.SetFocus();
    } catch (const CException& e) {
        const std::wstring werr = std::wstring(e.GetText()) + L"\n" + std::wstring(e.GetErrorString());
        logger::warn(std::string("[chat-fs] WndProc: ") + pmui::wide_to_utf8(werr));
    }
    return CWnd::WndProc(msg, wparam, lparam);
}

namespace pmui {

void chat_image_fullscreen_show(HWND owner, std::vector<std::wstring> paths, size_t index,
                                  bool constrainToMainFrame)
{
    // Not named `s_host` — Windows headers define `s_host` as a Winsock in_addr macro.
    static CChatImageFullscreenHost s_chatImageFs;
    s_chatImageFs.ShowForImages(owner, std::move(paths), index, constrainToMainFrame);
}

} // namespace pmui
