#include "stdafx.h"
#include "FileTreePanel.h"
#include "Resource.h"
#include "constants.hpp"
#include "win/settings_store.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "helpers/theme.hpp"
#include "file_extensions.hpp"
#include <vector>

#include <KnownFolders.h>
#include <ShlGuid.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>           // PathMatchSpecW, StrRetToBufW
#include <uxtheme.h>
#include <commctrl.h>          // SetWindowSubclass / RemoveWindowSubclass
#include <windowsx.h>          // GET_XBUTTON_WPARAM
#include <cwchar>
#include <filesystem>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace {

// Unique id for our SysListView32 keyboard subclass (one per process is fine).
constexpr UINT_PTR kExplorerListSubclassId = 0xE7B0;

// Subclass proc — intercepts Backspace (= navigate up) and Delete (= recycle).
// `ref` carries the host CExplorerBrowserView* so we can call back into it.
LRESULT CALLBACK ExplorerListKeySubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR /*subclassId*/, DWORD_PTR ref)
{
    if (msg == WM_XBUTTONDOWN) {
        auto* view = reinterpret_cast<CExplorerBrowserView*>(ref);
        if (view) {
            const UINT x = GET_XBUTTON_WPARAM(wp);
            if (x == XBUTTON1) { view->NavigateBack();     return 0; }
            if (x == XBUTTON2) { view->NavigateForward(); return 0; }
        }
    }
    if (msg == WM_KEYDOWN) {
        auto* view = reinterpret_cast<CExplorerBrowserView*>(ref);
        if (view) {
            const bool ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
            const bool alt   = (::GetKeyState(VK_MENU)    & 0x8000) != 0;
            // Plain Backspace → up one level. Don't fight Alt+Left etc.
            if (wp == VK_BACK && !ctrl && !alt) {
                view->NavigateUp();
                return 0;
            }
            // Plain Delete or Shift+Delete → recycle / permanent delete via
            // IFileOperation (gives a confirmation dialog by default).
            if (wp == VK_DELETE && !ctrl && !alt) {
                view->DeleteSelectionToRecycleBin();
                if (shift) {
                    // Permanent-delete chord — let Shell handle it too if our
                    // dialog was cancelled? Simpler: always swallow here so we
                    // don't trigger two dialogs.
                }
                return 0;
            }
        }
    }
    if (msg == WM_NCDESTROY) {
        // Auto-cleanup: drop the subclass before the listview goes away.
        ::RemoveWindowSubclass(hwnd, ExplorerListKeySubclassProc, kExplorerListSubclassId);
    }
    return ::DefSubclassProc(hwnd, msg, wp, lp);
}

// Recursively find the first descendant window whose class equals `target`.
struct FindClassCtx { const wchar_t* target; HWND found; };
BOOL CALLBACK FindClassEnumProc(HWND hwnd, LPARAM lparam)
{
    auto* ctx = reinterpret_cast<FindClassCtx*>(lparam);
    wchar_t cls[64]{};
    if (::GetClassNameW(hwnd, cls, 64) > 0 && std::wcscmp(cls, ctx->target) == 0) {
        ctx->found = hwnd;
        return FALSE; // stop
    }
    // Recurse into children
    ::EnumChildWindows(hwnd, FindClassEnumProc, lparam);
    return ctx->found == nullptr;
}

HWND find_descendant_by_class(HWND root, const wchar_t* cls)
{
    FindClassCtx ctx{cls, nullptr};
    ::EnumChildWindows(root, FindClassEnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

/** Absolute path + native separators — SHParseDisplayName is picky about format. */
std::wstring NormalizeFolderForShellW(const std::wstring& folder)
{
    if (folder.empty()) return folder;
    wchar_t buf[32768]{};
    const DWORD n = ::GetFullPathNameW(folder.c_str(), static_cast<DWORD>(std::size(buf)), buf, nullptr);
    if (n == 0 || n >= static_cast<DWORD>(std::size(buf)))
        return folder;
    return std::wstring(buf, n);
}

} // namespace

// ── CShellGlobFilter ─────────────────────────────────────────────────────────

STDMETHODIMP CShellGlobFilter::ShouldShow(IShellFolder* psf,
                                           PCIDLIST_ABSOLUTE /*pidlFolder*/,
                                           PCUITEMID_CHILD   pidlItem)
{
    // Fast path: passthrough masks — no work needed.
    if (m_mask.empty() || m_mask == L"*.*" || m_mask == L"*")
        return S_OK;

    // Always show folders so navigation remains unaffected.
    SFGAOF attr = SFGAO_FOLDER | SFGAO_STREAM;
    if (SUCCEEDED(psf->GetAttributesOf(1, &pidlItem, &attr))
        && (attr & SFGAO_FOLDER) && !(attr & SFGAO_STREAM))
        return S_OK;

    // Resolve the item's display name (filename only, no path prefix).
    STRRET sr{};
    if (FAILED(psf->GetDisplayNameOf(pidlItem, SHGDN_INFOLDER, &sr)))
        return S_OK;    // can't determine → show to be safe

    wchar_t name[MAX_PATH]{};
    StrRetToBufW(&sr, pidlItem, name, MAX_PATH);

    // PathMatchSpecW handles semicolon-separated patterns natively,
    // e.g. "*.jpg;*.png;*.tif".
    return ::PathMatchSpecW(name, m_mask.c_str()) ? S_OK : S_FALSE;
}

// ── CShellEventSink callbacks ────────────────────────────────────────────────

STDMETHODIMP CShellEventSink::OnViewCreated(IShellView* /*psv*/)
{
    // The SysListView32 may have just been (re)created. Re-attach our hook.
    if (m_pView) m_pView->RebindListKeySubclass();
    return S_OK;
}

STDMETHODIMP CShellEventSink::OnNavigationComplete(PCIDLIST_ABSOLUTE pidl)
{
    std::wstring folder;
    if (m_pCurrentFolder && pidl) {
        wchar_t path[MAX_PATH]{};
        if (::SHGetPathFromIDListW(pidl, path)) {
            *m_pCurrentFolder = path;
            folder = path;
        }
    }
    // Mirror the filesystem folder to the main window status bar (dark-themed).
    if (!folder.empty() && m_pView && m_pView->IsWindow()) {
        if (HWND root = ::GetAncestor(m_pView->GetHwnd(), GA_ROOT)) {
            auto* heap = new std::wstring(folder);
            if (!::PostMessageW(root, UWM_EXPLORER_FOLDER_PATH,
                                reinterpret_cast<WPARAM>(heap), 0))
                delete heap;
        }
    }
    // New folder, no selection yet — drop the dedup cache.
    if (m_pLastSelected) m_pLastSelected->clear();
    // Select pending file if requested (shift+click reveal feature).
    // OnNavigationComplete fires when the PIDL resolves, but the Shell view
    // may still be enumerating items — SelectFile can fail silently.  Start
    // the retry loop so we keep trying until the view is populated.
    if (m_pView) {
        std::wstring pending = m_pView->TakePendingSelectFile();
        if (!pending.empty()) {
            if (!m_pView->SelectFile(pending))
                m_pView->ScheduleSelectRetry(pending);
        }
    }
    // Belt-and-braces — make sure the listview is still subclassed after nav.
    if (m_pView) {
        m_pView->RebindListKeySubclass();
    }
    // Restore column visibility + widths and view mode (no-ops when not set).
    if (m_pView) {
        m_pView->ApplyViewMode();
        m_pView->ApplyColumnSettings();
    }
    return S_OK;
}

// ── CExplorerBrowserView ─────────────────────────────────────────────────────

CExplorerBrowserView::~CExplorerBrowserView()
{
    DestroyBrowser();
}

void CExplorerBrowserView::DestroyBrowser()
{
    // Capture view state before the COM object goes away so it survives
    // across Destroy/rebuild cycles (shell-frame toggle, re-dock, etc.).
    SnapshotViewMode();
    SnapshotColumnSettings();

    if (IsWindow()) {
        ::KillTimer(*this, TIMER_SELECTION);
        ::KillTimer(*this, TIMER_SELECT_RETRY);
    }
    m_selectRetryFile.clear();
    m_selectRetryCount = 0;

    if (m_peb && m_pSink) {
        m_peb->Unadvise(m_adviseCookie);
        m_adviseCookie = 0;
    }
    if (m_pSink) {
        m_pSink->Release();
        m_pSink = nullptr;
    }
    if (m_peb) {
        m_peb->Destroy();
        m_peb->Release();
        m_peb = nullptr;
    }
    m_browserState = BrowserState::Destroyed;
    // The SysListView32 we hooked is gone (or going) — drop the cache; the
    // subclass itself self-removes in WM_NCDESTROY.
    m_hSubclassedList = nullptr;
}

void CExplorerBrowserView::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    cs.style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
}

int CExplorerBrowserView::OnCreate(CREATESTRUCT&)
{
    // Defer IExplorerBrowser creation — see header comment.
    PostMessage(WM_EB_INIT);
    return 0;
}

void CExplorerBrowserView::InitBrowser()
{
    if (m_browserState == BrowserState::Ready)
        return;
    m_browserState = BrowserState::Initialising;
    ++m_navGeneration;  // §12: new navigation epoch

    CRect rcBrowser = GetClientRect();

    FOLDERSETTINGS fs{};
    // Use the restored view mode as the initial hint so the first navigation
    // opens in the right mode without a flash-to-Details then switch.
    fs.ViewMode = (m_viewMode > 0)
                    ? static_cast<UINT>(m_viewMode)
                    : static_cast<UINT>(FVM_DETAILS);
    fs.fFlags   = FWF_AUTOARRANGE;

    HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&m_peb));
    if (FAILED(hr) || !m_peb)
        return;

    hr = m_peb->Initialize(*this, rcBrowser, &fs);
    if (FAILED(hr)) {
        m_peb->Release();
        m_peb = nullptr;
        return;
    }
    m_browserState = BrowserState::Ready;

    // Optional Explorer frames (address bar, nav tree, command rows). Off by default —
    // that chrome often stays light on many builds (bad in dark UI). When enabled,
    // the Shell provides nav/Layout UI; persisted as `window.filetree_show_shell_frames`.
    // Always: EBO_NOWRAPPERWINDOW + EBO_NOBORDER — host draws the dock; no extra Shell
    // wrapper HWND and no inner border around the view (see shobjidl_core.h). Contrast
    // with app-owned settings controls: `helpers/theme.cpp` (SetWindowTheme on COMBO/EDIT;
    // we do not run that pass on this subtree — see CMainFrame::ApplyAppearance).
    EXPLORER_BROWSER_OPTIONS ebo = static_cast<EXPLORER_BROWSER_OPTIONS>(0);
    if (SUCCEEDED(m_peb->GetOptions(&ebo))) {
        ebo = static_cast<EXPLORER_BROWSER_OPTIONS>(
            ebo | EBO_NOWRAPPERWINDOW | EBO_NOBORDER);
        if (m_showShellFrames)
            ebo = static_cast<EXPLORER_BROWSER_OPTIONS>(ebo | EBO_SHOWFRAMES);
        else
            ebo = static_cast<EXPLORER_BROWSER_OPTIONS>(ebo & ~EBO_SHOWFRAMES);
        m_peb->SetOptions(ebo);
    }

    // Glob filter — install before the first BrowseToIDList so the initial
    // enumeration already respects the mask.  Passthrough masks ("*.*" / empty)
    // pass nullptr and impose zero overhead on the Shell's enumeration.
    m_filter.SetMask(m_filterMask);
    InstallFilter();

    // Navigation + selection-cache-clear sink (also rebinds our key hook).
    m_pSink = new CShellEventSink(&m_currentFolder, &m_lastSelectedPath, this);
    m_peb->Advise(m_pSink, &m_adviseCookie);

    // Navigate: restore last folder if available, else fall back to Desktop.
    bool             navigated = false;
    PIDLIST_ABSOLUTE pidl      = nullptr;
    if (!m_initialFolder.empty()) {
        const std::wstring norm = NormalizeFolderForShellW(m_initialFolder);
        SFGAOF             attr{};
        hr = ::SHParseDisplayName(norm.c_str(), nullptr, &pidl, 0, &attr);
        if (SUCCEEDED(hr) && pidl) {
            m_peb->BrowseToIDList(pidl, SBSP_ABSOLUTE);
            ::CoTaskMemFree(pidl);
            pidl = nullptr;
            navigated = true;
        } else {
            pidl = ::ILCreateFromPathW(norm.c_str());
            if (pidl) {
                m_peb->BrowseToIDList(pidl, SBSP_ABSOLUTE);
                ::ILFree(pidl);
                pidl = nullptr;
                navigated = true;
            }
        }
    }
    if (!navigated) {
        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &pidl)) && pidl) {
            m_peb->BrowseToIDList(pidl, SBSP_ABSOLUTE);
            ::CoTaskMemFree(pidl);
        }
    }

    // Start polling for file-selection changes.
    ::SetTimer(*this, TIMER_SELECTION, pm::ui::k_explorer_selection_poll_ms, nullptr);
}

void CExplorerBrowserView::SetShowShellFrames(bool show)
{
    if (m_showShellFrames == show)
        return;
    m_showShellFrames = show;
    if (!IsWindow() || m_browserState != BrowserState::Ready)
        return;

    const std::wstring restore = !m_currentFolder.empty() ? m_currentFolder : m_initialFolder;
    DestroyBrowser();
    InitBrowser();
    if (!restore.empty())
        RequestNavigateToFolder(restore);
}

// ── Column visibility / width ────────────────────────────────────────────────

// Helper: obtain IColumnManager from the live browser view.
static IColumnManager* get_column_manager(IExplorerBrowser* peb)
{
    if (!peb) return nullptr;
    IFolderView2* pFV2 = nullptr;
    if (FAILED(peb->GetCurrentView(IID_PPV_ARGS(&pFV2))) || !pFV2)
        return nullptr;
    IColumnManager* pCM = nullptr;
    pFV2->QueryInterface(IID_PPV_ARGS(&pCM));
    pFV2->Release();
    return pCM;   // caller must Release
}

void CExplorerBrowserView::SnapshotColumnSettings()
{
    IColumnManager* pCM = get_column_manager(m_peb);
    if (!pCM) return;

    UINT count = 0;
    if (FAILED(pCM->GetColumnCount(CM_ENUM_VISIBLE, &count)) || count == 0) {
        pCM->Release();
        return;
    }
    std::vector<PROPERTYKEY> keys(count);
    if (FAILED(pCM->GetColumns(CM_ENUM_VISIBLE, keys.data(), count))) {
        pCM->Release();
        return;
    }

    m_columnSpecs.clear();
    m_columnSpecs.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        CM_COLUMNINFO ci{};
        ci.cbSize = sizeof(ci);
        ci.dwMask = CM_MASK_WIDTH;
        if (SUCCEEDED(pCM->GetColumnInfo(keys[i], &ci))) {
            ExplorerColumnSpec spec;
            spec.fmtid = keys[i].fmtid;
            spec.pid   = keys[i].pid;
            spec.width = ci.uWidth;
            m_columnSpecs.push_back(spec);
        }
    }
    pCM->Release();
}

void CExplorerBrowserView::ApplyColumnSettings()
{
    if (m_columnSpecs.empty()) return;
    IColumnManager* pCM = get_column_manager(m_peb);
    if (!pCM) return;

    // Build ordered PROPERTYKEY array and set the visible column list.
    std::vector<PROPERTYKEY> keys;
    keys.reserve(m_columnSpecs.size());
    for (const auto& s : m_columnSpecs) {
        PROPERTYKEY pk;
        pk.fmtid = s.fmtid;
        pk.pid   = s.pid;
        keys.push_back(pk);
    }
    pCM->SetColumns(keys.data(), static_cast<UINT>(keys.size()));

    // Restore per-column widths.
    for (std::size_t i = 0; i < m_columnSpecs.size(); ++i) {
        CM_COLUMNINFO ci{};
        ci.cbSize = sizeof(ci);
        ci.dwMask = CM_MASK_WIDTH;
        ci.uWidth = m_columnSpecs[i].width;
        pCM->SetColumnInfo(keys[i], &ci);
    }
    pCM->Release();
}

// ── View mode (FVM_*) ────────────────────────────────────────────────────────

void CExplorerBrowserView::SnapshotViewMode()
{
    if (!m_peb) return;
    IFolderView2* pFV2 = nullptr;
    if (FAILED(m_peb->GetCurrentView(IID_PPV_ARGS(&pFV2))) || !pFV2) return;

    FOLDERVIEWMODE fvm{};
    int            sz = 0;
    if (SUCCEEDED(pFV2->GetViewModeAndIconSize(&fvm, &sz))) {
        // FVM_AUTO (-1) means Shell hasn't settled on a mode — don't persist.
        if (static_cast<int>(fvm) > 0) {
            m_viewMode = static_cast<int>(fvm);
            m_iconSize = sz > 0 ? sz : 0;
        }
    }
    pFV2->Release();
}

void CExplorerBrowserView::ApplyViewMode()
{
    // 0 or FVM_AUTO (-1): leave Shell to its own devices.
    if (m_viewMode <= 0) return;
    if (!m_peb) return;

    IFolderView2* pFV2 = nullptr;
    if (FAILED(m_peb->GetCurrentView(IID_PPV_ARGS(&pFV2))) || !pFV2) return;

    pFV2->SetViewModeAndIconSize(static_cast<FOLDERVIEWMODE>(m_viewMode),
                                  m_iconSize > 0 ? m_iconSize : -1);
    pFV2->Release();
}

// ── Glob filter ──────────────────────────────────────────────────────────────

void CExplorerBrowserView::InstallFilter()
{
    if (!m_peb) return;
    IFolderFilterSite* pFFS = nullptr;
    if (FAILED(m_peb->QueryInterface(IID_PPV_ARGS(&pFFS))) || !pFFS)
        return;

    const bool passthrough = m_filterMask.empty()
                          || m_filterMask == L"*.*"
                          || m_filterMask == L"*";
    // Pass nullptr to remove the filter when mask is a passthrough pattern.
    pFFS->SetFilter(passthrough ? nullptr : static_cast<IFolderFilter*>(&m_filter));
    pFFS->Release();
}

void CExplorerBrowserView::SetFilterMask(const std::wstring& mask)
{
    if (m_filterMask == mask)
        return;
    m_filterMask = mask;
    m_filter.SetMask(mask);

    if (!m_peb || m_browserState != BrowserState::Ready)
        return;     // filter stored; InitBrowser will call InstallFilter on next init

    InstallFilter();

    // Re-navigate to force the Shell to re-enumerate with the new filter.
    const std::wstring target = !m_currentFolder.empty() ? m_currentFolder : m_initialFolder;
    if (!target.empty())
        RequestNavigateToFolder(target);
}

void CExplorerBrowserView::RequestNavigateToFolder(const std::wstring& folder)
{
    if (folder.empty() || !::IsWindow(*this))
        return;
    auto* p = new (std::nothrow) std::wstring(folder);
    if (!p)
        return;
    if (!::PostMessageW(*this, WM_EB_DEFERRED_NAV, 0, reinterpret_cast<LPARAM>(p)))
        delete p;
}

HRESULT CExplorerBrowserView::NavigateToFolder(const std::wstring& folder)
{
    const std::wstring norm = NormalizeFolderForShellW(folder);
    // If the browser hasn't been initialised yet (early call during startup),
    // queue the path as the initial folder so InitBrowser picks it up.
    if (!m_peb) {
        m_initialFolder = norm;
        return E_POINTER;   // not a hard error — path is queued
    }
    if (norm.empty()) return E_INVALIDARG;

    PIDLIST_ABSOLUTE pidl  = nullptr;
    SFGAOF            attr = 0;
    HRESULT hr = ::SHParseDisplayName(norm.c_str(), nullptr, &pidl, 0, &attr);
    if (SUCCEEDED(hr) && pidl) {
        hr = m_peb->BrowseToIDList(pidl, SBSP_ABSOLUTE);
        ::CoTaskMemFree(pidl);
        return hr;
    }
    // SHParseDisplayName can fail on plain drive-letter paths in some
    // environments; fall back to the older ILCreateFromPath API.
    pidl = ::ILCreateFromPathW(norm.c_str());
    if (!pidl) return E_FAIL;
    hr = m_peb->BrowseToIDList(pidl, SBSP_ABSOLUTE);
    ::ILFree(pidl);
    return hr;
}

// ── Mouse back / forward = history (X1 / X2); Backspace = parent folder ──────
void CExplorerBrowserView::NavigateBack()
{
    if (!m_peb) return;
    m_peb->BrowseToIDList(nullptr, SBSP_NAVIGATEBACK);
}

void CExplorerBrowserView::NavigateForward()
{
    if (!m_peb) return;
    m_peb->BrowseToIDList(nullptr, SBSP_NAVIGATEFORWARD);
}

// ── Backspace = navigate up ──────────────────────────────────────────────────
void CExplorerBrowserView::NavigateUp()
{
    if (!m_peb) return;
    // SBSP_PARENT navigates to the parent of the current location; the pidl
    // arg is ignored. Silently no-ops at the root (e.g. "This PC").
    m_peb->BrowseToIDList(nullptr, SBSP_PARENT);
}

// ── Delete = move selected items to the Recycle Bin ─────────────────────────
void CExplorerBrowserView::DeleteSelectionToRecycleBin()
{
    if (!m_peb) return;

    IFolderView2* pFV2 = nullptr;
    HRESULT hr = m_peb->GetCurrentView(IID_PPV_ARGS(&pFV2));
    if (FAILED(hr) || !pFV2) return;

    IShellItemArray* pSel = nullptr;
    hr = pFV2->GetSelection(FALSE, &pSel);
    pFV2->Release();
    if (FAILED(hr) || !pSel) return;

    DWORD count = 0;
    if (FAILED(pSel->GetCount(&count)) || count == 0) {
        pSel->Release();
        return;
    }

    HWND owner = ::GetAncestor(*this, GA_ROOT);

    // Pull every filesystem path out of the selection up front. We need them
    // (a) for the lock-release SendMessage below and (b) optionally for any
    // future per-path logging. SIGDN_FILESYSPATH skips virtual / library
    // items that have no on-disk representation — those can't be recycled
    // anyway. The IShellItemArray is still passed to IFileOperation as-is
    // so the Shell handles all the edge cases (junctions, libraries, …).
    std::vector<std::wstring> paths;
    paths.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(pSel->GetItemAt(i, &pItem))) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz))) {
                paths.emplace_back(psz);
                ::CoTaskMemFree(psz);
            }
            pItem->Release();
        }
    }

    // Tell the frame to drop any preview that holds a file handle on these
    // items. Gdiplus::Image::FromFile keeps the file memory-mapped for the
    // lifetime of the Image*, so without this the Shell op silently fails
    // with a sharing violation on the previewed file. SendMessage (NOT Post)
    // because we need the preview gone *before* IFileOperation runs.
    if (owner && !paths.empty()) {
        // lparam: non-zero = also prune m_explorerSelectionPaths (paths will be removed).
        ::SendMessageW(owner, UWM_RELEASE_PREVIEW_FOR_PATHS,
                       reinterpret_cast<WPARAM>(&paths), 1);
    }

    IFileOperation* pFO = nullptr;
    hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                            IID_PPV_ARGS(&pFO));
    if (SUCCEEDED(hr) && pFO) {
        // Owner = top-level frame so the Shell confirmation dialog is modal
        // to the app, not to the dock view.
        pFO->SetOwnerWindow(owner);
        // FOFX_RECYCLEONDELETE is the Vista+ "send to Recycle Bin" hint.
        // FOF_ALLOWUNDO is the legacy equivalent — set both for safety.
        // We deliberately keep the Shell confirmation prompt (no FOF_NOCONFIRMATION).
        pFO->SetOperationFlags(FOFX_RECYCLEONDELETE | FOF_ALLOWUNDO);
        pFO->DeleteItems(pSel);
        pFO->PerformOperations();
        pFO->Release();
    }
    pSel->Release();

    // Drop the dedup fingerprint so the next selection notification fires
    // even if the user re-selects something with the same path string.
    m_lastSelectedPath.clear();
}

// ── Re-dock: IExplorerBrowser can keep bad host state until the next full creation ─

void CExplorerBrowserView::SyncShellHostLayout()
{
    if (!IsWindow() || !m_peb || m_browserState != BrowserState::Ready)
        return;
    const CRect rc = GetClientRect();
    if (rc.IsRectEmpty())
        return;
    m_peb->SetRect(nullptr, rc);
    (void)::RedrawWindow(*this, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void CExplorerBrowserView::RefreshThemeChrome()
{
    if (!IsWindow())
        return;

    const bool dark = pmui::theme_palette().dark;
    pmui::allow_dark_mode_for_window_tree(GetHwnd(), dark);
    ::SetWindowTheme(GetHwnd(), dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);

    const bool changed = m_hasThemeSnapshot && m_lastThemeDark != dark;
    m_hasThemeSnapshot = true;
    m_lastThemeDark = dark;

    if (changed && m_browserState == BrowserState::Ready) {
        const std::wstring restore = !m_currentFolder.empty() ? m_currentFolder : m_initialFolder;
        DestroyBrowser();
        InitBrowser();
        if (!restore.empty())
            RequestNavigateToFolder(restore);
    } else {
        SyncShellHostLayout();
    }
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
}

void CExplorerBrowserView::RequestRebuildAfterReDock()
{
    if (!::IsWindow(*this))
        return;
    (void)PostMessage(WM_EB_REDOCK, 0, 0);
}

void CExplorerBrowserView::RebuildShellAfterReDock()
{
    if (!IsWindow())
        return;
    // §10 state machine: transition through Rebuilding so CheckSelection is
    // suppressed during the destroy/create window.  Timer is killed in
    // DestroyBrowser and restarted when InitBrowser reaches Ready.
    m_browserState = BrowserState::Rebuilding;
    const std::wstring restore = !m_currentFolder.empty() ? m_currentFolder : m_initialFolder;
    DestroyBrowser();
    InitBrowser();
    if (!restore.empty())
        RequestNavigateToFolder(restore);
}

// ── (Re)attach the keyboard hook to the inner SysListView32 ─────────────────

void CExplorerBrowserView::RebindListKeySubclass()
{
    if (!IsWindow()) return;
    HWND list = find_descendant_by_class(*this, L"SysListView32");
    if (!list) return;
    if (list == m_hSubclassedList) return;  // already bound to this hwnd
    // The previous subclassed listview, if any, gets cleaned up by its own
    // WM_NCDESTROY handler in ExplorerListKeySubclassProc — no manual remove
    // needed (and the old HWND may already be destroyed at this point).
    if (::SetWindowSubclass(list, ExplorerListKeySubclassProc,
                            kExplorerListSubclassId,
                            reinterpret_cast<DWORD_PTR>(this))) {
        m_hSubclassedList = list;
    }
}

// ── Select a file by name ─────────────────────────────────────────────────────
// Uses IShellView::SelectItem (PIDL-based) so the Shell fires selection-change
// notifications and CheckSelection() updates m_lastSelectedPath correctly.
// Returns true on success, false if the view was not ready (items still
// enumerating) so the caller can schedule a retry.
bool CExplorerBrowserView::SelectFile(const std::wstring& filename, bool initialReveal)
{
    if (filename.empty() || !m_peb) return false;

    // Get IShellView for the PIDL-based selection API
    IShellView* pSV = nullptr;
    HRESULT hr = m_peb->GetCurrentView(IID_PPV_ARGS(&pSV));
    if (FAILED(hr) || !pSV) return false;

    // Get the folder's IShellFolder to parse the display name into a PIDL
    IFolderView* pFV = nullptr;
    hr = m_peb->GetCurrentView(IID_PPV_ARGS(&pFV));
    if (FAILED(hr) || !pFV) {
        pSV->Release();
        return false;
    }

    IShellFolder* pFolder = nullptr;
    hr = pFV->GetFolder(IID_PPV_ARGS(&pFolder));
    pFV->Release();
    if (FAILED(hr) || !pFolder) {
        pSV->Release();
        return false;
    }

    // Parse the filename to get its child PIDL within the folder.
    // ParseDisplayName fails when the Shell view hasn't enumerated items yet.
    PITEMID_CHILD pidlChild = nullptr;
    ULONG eaten = 0;
    hr = pFolder->ParseDisplayName(nullptr, nullptr, const_cast<LPWSTR>(filename.c_str()), &eaten, &pidlChild, nullptr);
    pFolder->Release();
    if (FAILED(hr) || !pidlChild) {
        pSV->Release();
        return false;
    }

    // Initial reveal: clear any prior multi-selection and move keyboard focus.
    // Retry re-assertions: only re-add the selection — don't stomp user clicks
    // or grab focus, since the user may have interacted with the tree by now.
    const UINT flags = initialReveal
        ? (SVSI_DESELECTOTHERS | SVSI_SELECT | SVSI_FOCUSED | SVSI_ENSUREVISIBLE)
        : (SVSI_SELECT | SVSI_ENSUREVISIBLE);
    hr = pSV->SelectItem(pidlChild, flags);

    ::ILFree(pidlChild);
    pSV->Release();
    return SUCCEEDED(hr);
}

// ── Retry selection after navigation ─────────────────────────────────────────
// Called when SelectFile fails immediately after OnNavigationComplete because
// the Shell view is still enumerating folder items.  Stores the filename and
// starts TIMER_SELECT_RETRY (80 ms interval, up to k_select_retry_max ticks).
void CExplorerBrowserView::ScheduleSelectRetry(const std::wstring& filename)
{
    if (!IsWindow()) return;
    m_selectRetryFile  = filename;
    m_selectRetryCount = k_select_retry_max;
    ::SetTimer(*this, TIMER_SELECT_RETRY, k_select_retry_ms, nullptr);
}

// ── Selection polling ─────────────────────────────────────────────────────────
// IExplorerBrowserEvents provides no file-selection callback, so we query
// IFolderView2::GetSelection on each timer tick. This is the standard approach.
void CExplorerBrowserView::CheckSelection()
{
    // §10: only poll when the browser is fully alive — not during rebuild or init.
    if (!m_peb || m_browserState != BrowserState::Ready) return;

    IFolderView2* pFV2 = nullptr;
    HRESULT hr = m_peb->GetCurrentView(IID_PPV_ARGS(&pFV2));
    if (FAILED(hr) || !pFV2) return;

    std::vector<std::wstring> paths;
    IShellItemArray* pSel = nullptr;
    hr = pFV2->GetSelection(FALSE, &pSel);
    pFV2->Release();

    // retryFileInRawSel: our reveal file is somewhere in the raw (unfiltered)
    // shell selection.  Set during the item loop below.
    bool retryFileInRawSel = false;

    if (SUCCEEDED(hr) && pSel) {
        DWORD count = 0;
        if (SUCCEEDED(pSel->GetCount(&count)) && count > 0) {
            paths.reserve(count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* pItem = nullptr;
                if (SUCCEEDED(pSel->GetItemAt(i, &pItem))) {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        // Filesystem dirs (batch commands expand via AddFilesToQueue), plus
                        // images, text/markdown, and 3D mesh extensions (preview). The frame splits uses.
                        std::wstring p = pszPath;
                        CoTaskMemFree(pszPath);
                        // Check against retry file in the raw (unfiltered) loop so we
                        // catch non-previewable types (e.g. .cpp) that would never appear
                        // in the filtered `paths` vector.
                        if (!m_selectRetryFile.empty() &&
                            std::filesystem::path(p).filename().wstring() == m_selectRetryFile)
                            retryFileInRawSel = true;
                        std::error_code ec;
                        if (std::filesystem::is_directory(std::filesystem::path(p), ec)) {
                            paths.push_back(std::move(p));
                        } else {
                            std::wstring ext = std::filesystem::path(p).extension().wstring();
                            for (auto& c : ext) c = towlower(c);
                            if (pmui::is_image_ext(ext) || pmui::is_browser_image_ext(ext)
                                || pmui::is_text_preview_eligible_for_path(p)
                                || pmui::is_viewer_3d_ext(ext) || pmui::is_viewer_pdf_ext(ext)
                                || pmui::is_viewer_spreadsheet_ext(ext)
                                || pmui::is_video_preview_eligible_for_path(p))
                                paths.push_back(std::move(p));
                        }
                    }
                    pItem->Release();
                }
            }
        }
        pSel->Release();
    }

    if (!m_selectRetryFile.empty()) {
        if (retryFileInRawSel) {
            // Our file is already selected — cancel the retry, no more work needed.
            ::KillTimer(*this, TIMER_SELECT_RETRY);
            m_selectRetryFile.clear();
            m_selectRetryCount = 0;
        } else if (!paths.empty()) {
            // Non-empty selection that does NOT contain our file → user clicked
            // something different.  Abort the retry so we don't fight them.
            ::KillTimer(*this, TIMER_SELECT_RETRY);
            m_selectRetryFile.clear();
            m_selectRetryCount = 0;
        } else {
            // Selection is empty — shell background refresh cleared it.
            // Suppress the ClearFromEmptyExplorerPick notification this cycle;
            // the retry timer will re-assert the selection on the next tick.
            return;
        }
    }

    // Dedup vs the previous notification: NUL-joined fingerprint of all paths.
    std::wstring fp;
    fp.reserve(paths.size() * 64);
    for (const auto& p : paths) { fp.append(p); fp.push_back(L'\1'); }
    if (fp == m_lastSelectedPath) return;
    m_lastSelectedPath = std::move(fp);

    // Heap-allocate; freed by CMainFrame::OnExplorerSelection.
    // Post on empty selection too so the status bar can clear count/size.
    // Must target GA_ROOT (same as UWM_EXPLORER_FOLDER_PATH above): Win32++ `GetAncestor()` defaults to
    // GA_ROOTOWNER, which is not the dock frame — selection never reached the main WndProc and Pixlwiz Share
    // saw an empty m_explorerSelectionPaths (no share dialog).
    // §12: pack paths + navigation generation + Ctrl state into a single struct.
    auto* msg = new ExplorerSelectionMsg{
        std::move(paths),
        m_navGeneration,
        (::GetKeyState(VK_CONTROL) & 0x8000) != 0
    };
    if (HWND root = ::GetAncestor(GetHwnd(), GA_ROOT)) {
        if (!::PostMessageW(root, UWM_EXPLORER_SELECTION, reinterpret_cast<WPARAM>(msg), 0))
            delete msg;
    } else {
        delete msg;
    }
}

BOOL CExplorerBrowserView::PreTranslateMessage(MSG& msg)
{
    if (msg.message == WM_KEYDOWN) {
        const bool ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt   = (::GetKeyState(VK_MENU)    & 0x8000) != 0;

        // Don't hijack keys when an EDIT-like control inside the Shell view
        // owns focus (e.g. inline rename, search box, address bar). Backspace
        // there means "delete a character", which the user will rightly expect.
        wchar_t cls[64]{};
        if (msg.hwnd && ::GetClassNameW(msg.hwnd, cls, 64) > 0) {
            if (std::wcscmp(cls, L"Edit")     == 0 ||
                std::wcscmp(cls, L"RICHEDIT") == 0 ||
                std::wcscmp(cls, L"RICHEDIT_CLASS") == 0 ||
                std::wcsncmp(cls, L"RichEdit", 8) == 0) {
                return CWnd::PreTranslateMessage(msg);
            }
        }

        if (msg.wParam == VK_BACK && !ctrl && !alt) {
            NavigateUp();
            return TRUE;
        }
        if (msg.wParam == VK_DELETE && !ctrl && !alt) {
            DeleteSelectionToRecycleBin();
            return TRUE;
        }
    }
    if (msg.message == WM_XBUTTONDOWN && msg.hwnd &&
        (::IsChild(*this, msg.hwnd) || msg.hwnd == *this)) {
        const UINT x = GET_XBUTTON_WPARAM(msg.wParam);
        if (x == XBUTTON1) {
            NavigateBack();
            return TRUE;
        }
        if (x == XBUTTON2) {
            NavigateForward();
            return TRUE;
        }
    }
    return CWnd::PreTranslateMessage(msg);
}

LRESULT CExplorerBrowserView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_EB_INIT) {
        InitBrowser();
        return 0;
    }
    if (msg == WM_EB_REDOCK) {
        RebuildShellAfterReDock();
        return 0;
    }
    if (msg == WM_EB_DEFERRED_NAV) {
        auto* p = reinterpret_cast<std::wstring*>(lparam);
        if (p) {
            const std::wstring s = *p;
            delete p;
            const HRESULT hr = NavigateToFolder(s);
            if (FAILED(hr) && hr != E_POINTER) {
                // Navigation initiation failed — OnNavigationComplete will never
                // fire, so SelectFile would never run.  Cancel the pending retry
                // now rather than burning the full 640 ms budget for nothing.
                ::KillTimer(*this, TIMER_SELECT_RETRY);
                m_pendingSelectFile.clear();
                m_selectRetryFile.clear();
                m_selectRetryCount = 0;
            }
        }
        return 0;
    }

    switch (msg) {
    case WM_TIMER:
        if (wparam == TIMER_SELECTION) {
            CheckSelection();
            return 0;
        }
        if (wparam == TIMER_SELECT_RETRY) {
            if (!m_selectRetryFile.empty()) {
                // Re-assert without SVSI_DESELECTOTHERS / SVSI_FOCUSED so we
                // don't stomp any selection the user may have made since the
                // initial reveal.  The shell background-refresh window is
                // typically < 400 ms; we keep asserting for the full budget.
                SelectFile(m_selectRetryFile, false /*re-assert*/);
            }
            if (--m_selectRetryCount <= 0) {
                ::KillTimer(*this, TIMER_SELECT_RETRY);
                m_selectRetryFile.clear();
                m_selectRetryCount = 0;
            }
            return 0;
        }
        break;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            CRect rc = GetClientRect();
            if (m_peb)
                m_peb->SetRect(nullptr, rc);
        }
        return 0;
    case WM_DESTROY:
        DestroyBrowser();
        break;
    default:
        break;
    }
    return WndProcDefault(msg, wparam, lparam);
}

// ── CFileTreeContainer ───────────────────────────────────────────────────────

CFileTreeContainer::CFileTreeContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.files_tab);
    SetDockCaption(d.explorer_caption);
    SetView(m_view);
}

// ── CDockFileTree ────────────────────────────────────────────────────────────

CDockFileTree::CDockFileTree()
{
    SetView(m_container);
}
