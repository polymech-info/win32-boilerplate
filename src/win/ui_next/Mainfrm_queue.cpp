// Queue commands: add files/folder, clear, save-as, selection helpers, startup enqueue.
#include "stdafx.h"
#include "Mainfrm.h"
#include "constants.hpp"
#include "queue_path_enumeration.hpp"
#include "helpers/text_conv.hpp"
#include "file_extensions.hpp"
#include "helpers/chat_context_attach.hpp"
#include "ui_log_file.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <shlobj.h>
#include <vector>

namespace fs = std::filesystem;

using pmui::is_image_ext;
using pmui::is_browser_image_ext;
using pmui::is_viewer_3d_ext;
using pmui::is_viewer_pdf_ext;
using pmui::is_viewer_spreadsheet_ext;
using pmui::is_video_preview_eligible_for_path;
using pmui::utf8_to_wide;
using pmui::wide_to_utf8;

namespace {

void trim_inplace_w(std::wstring& s)
{
    const auto n = s.size();
    size_t       a = 0;
    while (a < n && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n'))
        ++a;
    size_t b = n;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n'))
        --b;
    if (a > 0 || b < n)
        s = s.substr(a, b - a);
}

/** Explorer IExecute seeds one argv: `--src "a;b;c"`. `;` is not a valid path char on Windows. */
bool path_string_has_wildcards(const std::wstring& s) noexcept
{
    return s.find(L'*') != std::wstring::npos || s.find(L'?') != std::wstring::npos;
}

void split_semicolon_path_args(const std::wstring& w, std::vector<std::wstring>* out)
{
    if (w.find(L';') == std::wstring::npos) {
        if (!w.empty()) {
            std::wstring u = w;
            trim_inplace_w(u);
            if (!u.empty()) out->push_back(std::move(u));
        }
        return;
    }
    size_t i = 0;
    for (;;) {
        const size_t j = w.find(L';', i);
        if (j == std::wstring::npos) {
            std::wstring t = w.substr(i);
            trim_inplace_w(t);
            if (!t.empty()) out->push_back(std::move(t));
            return;
        }
        std::wstring t = w.substr(i, j - i);
        trim_inplace_w(t);
        if (!t.empty()) out->push_back(std::move(t));
        i = j + 1;
    }
}

/** True when every path is an existing directory — "Open in app" on a folder should browse
 *  the Explorer panel only (no CQueueListView rows; see FileQueue.h). */
bool all_startup_parts_are_directories(const std::vector<std::wstring>& raw_paths)
{
    if (raw_paths.empty()) return false;
    std::vector<std::wstring> parts;
    for (const auto& w : raw_paths) split_semicolon_path_args(w, &parts);
    if (parts.empty()) return false;
    for (const auto& part : parts) {
        std::error_code ec;
        if (!fs::is_directory(fs::path(part), ec) || ec) return false;
    }
    return true;
}

/** Parent of the first existing regular file in @p raw_paths (semicolon-split like `--src`). */
std::wstring first_regular_file_parent_for_cli_src(const std::vector<std::wstring>& raw_paths)
{
    for (const auto& w : raw_paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(w, &parts);
        for (const auto& part : parts) {
            std::error_code ec;
            const fs::path p(part);
            if (fs::is_regular_file(p, ec)) {
                const fs::path abs = fs::absolute(p, ec);
                const fs::path par = abs.parent_path();
                if (!par.empty())
                    return par.wstring();
                return abs.wstring();
            }
        }
    }
    return {};
}

/** Folder to open in Explorer for startup `--src` (file parent or first directory arg). */
std::wstring startup_filetree_seed_folder(const std::vector<std::wstring>& raw_paths)
{
    const std::wstring parent = first_regular_file_parent_for_cli_src(raw_paths);
    if (!parent.empty())
        return parent;
    for (const auto& w : raw_paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(w, &parts);
        for (const auto& part : parts) {
            std::error_code ec;
            const fs::path  p(part);
            if (fs::is_directory(p, ec) && !ec)
                return fs::absolute(p, ec).wstring();
        }
    }
    return {};
}

/** First existing regular file from @p raw_paths (semicolon-split like `--src`). */
std::wstring first_regular_file_for_cli_src(const std::vector<std::wstring>& raw_paths)
{
    for (const auto& w : raw_paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(w, &parts);
        for (const auto& part : parts) {
            std::error_code ec;
            const fs::path p(part);
            if (fs::is_regular_file(p, ec) && !ec)
                return fs::absolute(p, ec).wstring();
        }
    }
    return {};
}

} // namespace

// ── File / queue operations ───────────────────────────────────────────────────

void CMainFrame::OnAddFiles()
{
    CFileDialog dlg(TRUE, nullptr, nullptr,
                    OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER,
                    pmui::image_dialog_filter());
    dlg.SetTitle(L"Add images to queue");

    if (dlg.DoModal(*this) != IDOK) return;

    std::vector<std::wstring> files;
    int pos = 0;
    CString path = dlg.GetNextPathName(pos);
    while (!path.IsEmpty()) {
        files.push_back(std::wstring(path.c_str()));
        if (pos < 0) break;
        path = dlg.GetNextPathName(pos);
    }
    PushRecentFilesFromUserAdd(files);
    AddFilesToQueue(files);
}

void CMainFrame::OnAddFolder()
{
    BROWSEINFOW bi{};
    bi.hwndOwner   = GetHwnd();
    bi.lpszTitle   = L"Select folder with images";
    bi.ulFlags     = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    wchar_t dn[MAX_PATH]{};
    bi.pszDisplayName = dn;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH * 4]{};
    if (SHGetPathFromIDListW(pidl, path)) {
        PushRecentFolderExplicit(std::wstring(path));
        AddFilesToQueue({ path });
    }
    CoTaskMemFree(pidl);
}

void CMainFrame::AddFilesToQueue(const std::vector<std::wstring>& paths, bool openSingleFilePreview)
{
    if (!m_pDockQueue) return;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();

    const bool firstEmpty  = (lv.QueueCount() == 0);
    const int  firstNewIdx = lv.QueueCount();

    for (const auto& w : paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(w, &parts);
        for (const auto& part : parts) {
            std::error_code ec;
            for (const auto& fp : pmui::queue_paths::paths_to_enqueue(fs::path(part), true, ec))
                lv.AddFile(CString(fp.c_str()));
        }
    }

    CString status;
    status.Format(L"%d file(s) in queue", lv.QueueCount());
    SetStatusBarPartText(0, status);

    // Only auto-open the preview when a *single* file was added. Recursing a folder
    // often produces many rows: loading the first (esp. RAW → vips on the UI thread)
    // while a batch op also uses libvips can assert/crash. Batch commands call with
    // openSingleFilePreview=false so a lone image in the current folder does not load
    // before Resize/Compress runs. Bulk adds: update count only.
    {
        const int nAdded = lv.QueueCount() - firstNewIdx;
        if (openSingleFilePreview && firstEmpty && nAdded == 1 && !IsChatWorkbench()) {
            CString firstPath = lv.GetItemPath(firstNewIdx);
            m_viewerManager.ActiveView().LoadPicture(firstPath.c_str());
        }
    }

    // Re-enable Save Session as soon as there are files in the queue.
    InvalidateBatchUi();
}

void CMainFrame::SetPendingStartupEnqueue(const std::vector<std::wstring>& paths, bool open_chat)
{
    m_pendingStartupPaths = paths;
    m_pendingOpenChat     = open_chat;
    if (!paths.empty())
        m_startupFileTreePaths = paths;
    else
        m_startupFileTreePaths.clear();
    // `ApplyWindowLayoutData` restores `filetree_folder` (often a stale subfolder).
    // `WM_EB_INIT` runs after this; `NavigateToFolder` while `!m_peb` replaces `m_initialFolder` so
    // Explorer opens beside the `--src` file instead of a stale folder whose selection overwrites
    // startup selection/preview state.
    if (!paths.empty() && m_pDockFileTree) {
        const std::wstring seedParent = startup_filetree_seed_folder(paths);
        if (!seedParent.empty()) {
            // Called before WM_EB_INIT so m_peb is null; returns E_POINTER which
            // means the path was queued as m_initialFolder — that is expected.
            (void)m_pDockFileTree->GetFileTreeContainer().GetBrowserView().NavigateToFolder(seedParent);
            pmui::ui_log_file_eventf(
                "SetPendingStartupEnqueue: NavigateToFolder (--src parent seed)=\"%s\"",
                wide_to_utf8(seedParent).c_str());
        }
    }
}

void CMainFrame::FlushPendingStartupIfAny()
{
    if (m_pendingStartupPaths.empty() && !m_pendingOpenChat)
        return;
    // Folder-only seeds (e.g. Explorer IExecute) → navigate file tree in OnDeferredPostLayoutInit
    // only; do not add rows to the queue (CQueueListView / FileQueue.h).
    // §7a: delegate startup file handling to the active workbench.
    // Default: AddFilesToQueue.  Viewer: LoadFirstStartupPreviewFromPaths + latch.
    // Viewer workbench: still run startup when every `--src` part is a directory so we can open
    // the first previewable file inside (see `LoadFirstStartupPreviewFromPaths`).
    if (!m_pendingStartupPaths.empty()) {
        const bool all_dirs = all_startup_parts_are_directories(m_pendingStartupPaths);
        if (!all_dirs || IsViewerWorkbench())
            m_workbench->startupHandler().ApplyStartupFilePaths(*this, m_pendingStartupPaths);
    }
    // §7a: populate explorer selection using the workbench's filter policy
    // (main: images+text+pdf+3d; chat: chat_context_path_allowed — files + folder paths).
    if (!m_pendingStartupPaths.empty()) {
        std::vector<std::wstring> allParts;
        for (const auto& p : m_pendingStartupPaths) {
            std::vector<std::wstring> parts;
            split_semicolon_path_args(p, &parts);
            allParts.insert(allParts.end(),
                            std::make_move_iterator(parts.begin()),
                            std::make_move_iterator(parts.end()));
        }
        m_selRouter.SetFromStartup(m_workbench->previewPolicy().FilterExplorerSelection(allParts));
    }
    if (m_pendingOpenChat)
        OnChat();
    m_pendingStartupPaths.clear();
    m_pendingOpenChat = false;
}

void CMainFrame::OnAppOpenChat(const std::vector<std::wstring>& paths)
{
    if (!paths.empty() && !all_startup_parts_are_directories(paths))
        AddFilesToQueue(paths);
    m_explorerSelectionPaths.clear();
    for (const auto& p : paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(p, &parts);
        for (const auto& part : parts) {
            if (pmui::chat_context_path_allowed(part)) m_explorerSelectionPaths.push_back(part);
        }
    }
    OnChat();
}

void CMainFrame::OnAppBrowseToPaths(const std::vector<std::wstring>& paths)
{
    if (paths.empty()) return;
    m_explorerSelectionPaths.clear();
    for (const auto& p : paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(p, &parts);
        for (const auto& part : parts) {
            std::wstring ext = fs::path(part).extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
            std::error_code ec_r;
            bool            ok = false;
            if (IsChatWorkbench())
                ok = pmui::chat_context_path_allowed(part);
            else {
                ok = is_image_ext(ext) || is_browser_image_ext(ext)
                    || (pmui::is_text_preview_eligible_for_path(part) || is_viewer_3d_ext(ext)
                        || is_viewer_pdf_ext(ext) || is_viewer_spreadsheet_ext(ext)
                        || is_video_preview_eligible_for_path(part));
                if (ok) {
                    const fs::path pp(part);
                    ok = fs::is_regular_file(pp, ec_r) && !ec_r;
                }
            }
            if (ok) m_explorerSelectionPaths.push_back(part);
        }
    }
    if (m_pDockFileTree) {
        // Do not force-show Explorer: `--src` / bridge browse must respect saved `panels.filetree`.
        ScheduleFileTreeBrowse(paths);
    } else
        RefreshChatContext();
    // One previewable file: centre file viewer (no queue add)
    if (paths.size() == 1) {
        std::error_code ec;
        const fs::path  one(paths[0]);
        if (fs::is_regular_file(one, ec)) {
            std::wstring ext = one.extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
            if (!IsChatWorkbench()
                && (is_image_ext(ext) || is_browser_image_ext(ext)
                    || pmui::is_text_preview_eligible_for_path(one.wstring())
                    || is_viewer_3d_ext(ext) || is_viewer_pdf_ext(ext) || is_viewer_spreadsheet_ext(ext)
                    || is_video_preview_eligible_for_path(one.wstring())))
                m_previewCoord.Request(pmui::PreviewSource::AppBrowse, paths,
                                       m_viewerManager.ActiveView(), *m_workbench, *this);
        }
    }
}

void CMainFrame::ApplyFileTreeBrowseForPaths(const std::vector<std::wstring>& paths)
{
    if (paths.empty() || !m_pDockFileTree) return;
    // Navigation + chat context only — visibility follows `LoadLayout` / user toggles, not browse seeds
    // (`--src`, IExecute, deferred startup). Callers that must reveal Explorer use `EnsurePanelVisible` /
    // `TogglePanelView` themselves (e.g. Find → Reveal in Explorer).
    std::vector<std::wstring> headToken;
    split_semicolon_path_args(paths.front(), &headToken);
    if (headToken.empty()) return;
    std::error_code  ec;
    const fs::path   first(headToken.front());
    std::wstring     folder;
    if (fs::is_directory(first, ec))
        folder = fs::absolute(first, ec).wstring();
    else {
        const fs::path par = first.parent_path();
        if (!par.empty()) folder = fs::absolute(par, ec).wstring();
    }
    if (!folder.empty()) {
        // PostMessage to the view so navigation runs after IExplorerBrowser is ready (layout / shell
        // frames reinit) without blocking the main thread on the shell.
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().RequestNavigateToFolder(folder);
    }
    RefreshChatContext();
}

void CMainFrame::ScheduleFileTreeBrowse(const std::vector<std::wstring>& paths)
{
    if (paths.empty() || !GetHwnd()) return;
    // §11: push onto the browse-request queue instead of overwriting a single slot.
    const uint32_t seq = ++m_fileTreeNavSeq;
    m_deferredFileTreeNavQueue.push(paths);
    pmui::ui_log_file_eventf("ScheduleFileTreeBrowse seq=%u depth=%zu paths=%zu",
                              seq, m_deferredFileTreeNavQueue.size(), paths.size());
    ::SetTimer(GetHwnd(), kFileTreeNavTimerId, pm::ui::k_file_tree_nav_delay_ms, nullptr);
}

void CMainFrame::SelectFirstStartupFileInFileTree(const std::vector<std::wstring>& raw_paths)
{
    const std::wstring revealFile = first_regular_file_for_cli_src(raw_paths);
    if (!revealFile.empty())
        SelectPathInFileTree(revealFile);
}

std::wstring CMainFrame::StartupFileTreeSeedFolder() const
{
    return startup_filetree_seed_folder(m_startupFileTreePaths);
}

void CMainFrame::ApplyPendingStartupFileTreeIfAny()
{
    if (m_startupFileTreePaths.empty() || !m_pDockFileTree || !IsPanelVisible(m_pDockFileTree))
        return;

    const std::vector<std::wstring> paths = std::move(m_startupFileTreePaths);
    m_startupFileTreePaths.clear();

    auto& bv = m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
    const std::wstring seed = startup_filetree_seed_folder(paths);
    if (!seed.empty()) {
        bv.SetInitialFolder(seed);
        pmui::ui_log_file_eventf(
            "ApplyPendingStartupFileTreeIfAny: SetInitialFolder=\"%s\" paths=%zu",
            wide_to_utf8(seed).c_str(), paths.size());
    }

    if (all_startup_parts_are_directories(paths))
        ScheduleFileTreeBrowse(paths);
    else
        SelectFirstStartupFileInFileTree(paths);
}

#if defined(_WIN32)
void CMainFrame::OnDeferredFileTreeNavTimer()
{
    if (GetHwnd()) ::KillTimer(GetHwnd(), kFileTreeNavTimerId);
    // §11: drain the browse-request queue. Only the *last* entry navigates
    // the Shell (Explorer can only show one folder); earlier entries are
    // logged and dropped. This preserves the "most recent caller wins"
    // semantics of the old single-slot but does not silently lose payloads.
    std::vector<std::wstring> last;
    size_t dropped = 0;
    while (!m_deferredFileTreeNavQueue.empty()) {
        last = std::move(m_deferredFileTreeNavQueue.front());
        m_deferredFileTreeNavQueue.pop();
        if (!m_deferredFileTreeNavQueue.empty())
            ++dropped;
    }
    if (dropped > 0)
        pmui::ui_log_file_eventf("OnDeferredFileTreeNavTimer: drained %zu superseded browse requests", dropped);
    if (!last.empty()) {
        // If there's a file to reveal, set it as pending so OnNavigationComplete selects it.
        if (!m_pendingFileToSelect.empty() && m_pDockFileTree) {
            m_pDockFileTree->GetFileTreeContainer().GetBrowserView()
                .SetPendingSelectFile(m_pendingFileToSelect);
        }
        ApplyFileTreeBrowseForPaths(last);
        // Selection happens automatically in OnNavigationComplete via the pending file mechanism.
        m_pendingFileToSelect.clear();
    }
}
#endif

void CMainFrame::OnClearQueue()
{
    if (m_pDockQueue) {
        m_pDockQueue->GetQueueContainer().GetListView().ClearAll();
        if (!IsChatWorkbench()) {
            // Match ApplyCentralFileViewerPreviewFromPaths({}): drop PDF/XLS/3D/WebView2 vhost state too.
            m_viewerManager.ActiveView().ClearPicture();
            m_viewerManager.ActiveView().ClearText();
            m_viewerManager.ActiveView().ClearMarkdown();
#if defined(FEATURE_VIEWER_WEB)
            m_viewerManager.ActiveView().ReleaseViewerWebFolderMapping();
#endif
        }
        SetStatusBarPartText(0, L"Queue cleared.");
        InvalidateBatchUi();
    }
}

void CMainFrame::OnSaveAs()
{
    if (!m_pDockQueue) return;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();

    auto sel = GetSelectedQueueItems();
    int idx  = sel.empty() ? 0 : sel.front();
    if (idx < 0 || idx >= lv.QueueCount()) {
        ::MessageBoxW(GetHwnd(), L"No file selected in the queue.", L"Save As", MB_ICONINFORMATION);
        return;
    }

    CString srcPath = lv.GetItemPath(idx);
    if (srcPath.IsEmpty()) return;

    fs::path src   = srcPath.c_str();
    std::wstring ext = src.extension().wstring();
    if (ext.empty()) ext = L".*";

    std::wstring extNoDot = ext.size() > 1 ? ext.substr(1) : L"*";
    std::wstring extUpper = extNoDot;
    for (auto& c : extUpper) c = towupper(c);

    wchar_t filter[256]{};
    int fpos = 0;
    auto appendStr = [&](const std::wstring& s) {
        for (wchar_t c : s) filter[fpos++] = c;
        filter[fpos++] = L'\0';
    };
    appendStr(extUpper + L" Files (*." + extNoDot + L")");
    appendStr(L"*." + extNoDot);
    appendStr(L"All Files (*.*)");
    appendStr(L"*.*");

    wchar_t destPath[MAX_PATH]{};
    wcscpy_s(destPath, src.filename().c_str());

    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = GetHwnd();
    ofn.lpstrFilter  = filter;
    ofn.lpstrFile    = destPath;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"Save As";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt  = extNoDot.c_str();

    if (!::GetSaveFileNameW(&ofn)) return;

    std::error_code ec;
    fs::copy_file(src, fs::path(destPath), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::wstring msg = L"Copy failed: " + utf8_to_wide(ec.message());
        ::MessageBoxW(GetHwnd(), msg.c_str(), L"Save As", MB_ICONERROR);
    } else {
        CString log;
        log.Format(L"Saved: %s", destPath);
        LogMessage(log);
        SetStatusBarPartText(0, log);
    }
}

std::vector<int> CMainFrame::GetSelectedQueueItems()
{
    std::vector<int> sel;
    if (!m_pDockQueue) return sel;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    int idx = -1;
    while ((idx = lv.GetNextItem(idx, LVNI_SELECTED)) != -1)
        sel.push_back(idx);
    if (sel.empty()) {
        int total = lv.QueueCount();
        for (int i = 0; i < total; ++i) sel.push_back(i);
    }
    return sel;
}

bool CMainFrame::TryEnqueueCurrentExplorerFolderIfEmpty()
{
    if (!m_pDockQueue) return false;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    if (lv.QueueCount() > 0) return true;
    if (!m_pDockFileTree) return false;
    const std::wstring& folder =
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().GetCurrentFolder();
    if (folder.empty()) return false;
    std::error_code ec;
    if (!fs::is_directory(fs::path(folder), ec)) return false;
    AddFilesToQueue({ folder }, false);
    return lv.QueueCount() > 0;
}

namespace {

constexpr int kRecentMruCap = 15;

std::wstring try_canonical_path_w(const std::wstring& w, bool asDirectory)
{
    std::error_code ec;
    fs::path      p(w);
    if (asDirectory && !fs::is_directory(p, ec))
        return w;
    if (!asDirectory && !fs::is_regular_file(p, ec))
        return w;
    fs::path c = fs::weakly_canonical(p, ec);
    if (ec)
        return w;
    return c.wstring();
}

bool relative_path_is_descendant_only(const fs::path& rel)
{
    if (rel.empty())
        return false;
    for (const auto& comp : rel) {
        const std::wstring s = comp.wstring();
        if (s == L"..")
            return false;
    }
    return true;
}

bool folders_same_lineage_fs(const fs::path& aIn, const fs::path& bIn)
{
    if (aIn.empty() || bIn.empty())
        return false;
    std::error_code ec;
    fs::path        a = fs::weakly_canonical(aIn, ec);
    if (ec)
        a = aIn.lexically_normal();
    ec.clear();
    fs::path b = fs::weakly_canonical(bIn, ec);
    if (ec)
        b = bIn.lexically_normal();
    if (a == b)
        return true;
    ec.clear();
    fs::path relDown = fs::relative(b, a, ec);
    if (!ec && relative_path_is_descendant_only(relDown))
        return true;
    ec.clear();
    fs::path relUp = fs::relative(a, b, ec);
    if (!ec && relative_path_is_descendant_only(relUp))
        return true;
    return false;
}

bool paths_equal_icase_w(const std::wstring& a, const std::wstring& b)
{
    return a.size() == b.size() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

void remove_icase_from_vec(std::vector<std::wstring>& v, const std::wstring& item)
{
    const auto it = std::remove_if(v.begin(), v.end(),
                                   [&](const std::wstring& x) { return paths_equal_icase_w(x, item); });
    v.erase(it, v.end());
}

} // namespace

void CMainFrame::PushRecentFilesFromUserAdd(const std::vector<std::wstring>& files)
{
    for (const auto& raw : files) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(raw, &parts);
        for (const auto& part : parts) {
            std::error_code ec;
            if (!fs::is_regular_file(fs::path(part), ec) || ec)
                continue;
            std::wstring canon = try_canonical_path_w(part, false);
            remove_icase_from_vec(m_recentFiles, canon);
            m_recentFiles.insert(m_recentFiles.begin(), std::move(canon));
            if (static_cast<int>(m_recentFiles.size()) > kRecentMruCap)
                m_recentFiles.resize(kRecentMruCap);
        }
    }
}

void CMainFrame::PushRecentFolderExplicit(const std::wstring& folder)
{
    if (folder.empty())
        return;
    std::error_code ec;
    if (!fs::is_directory(fs::path(folder), ec) || ec)
        return;
    const std::wstring canon = try_canonical_path_w(folder, true);
    remove_icase_from_vec(m_recentFolders, canon);
    m_recentFolders.insert(m_recentFolders.begin(), canon);
    if (static_cast<int>(m_recentFolders.size()) > kRecentMruCap)
        m_recentFolders.resize(kRecentMruCap);
    m_explorerLastFolderPathW = canon;
}

void CMainFrame::RestoreExplorerRecentMruBaselineFromLayout(const std::string& filetree_folder_utf8)
{
    m_explorerLastFolderPathW.clear();
    if (filetree_folder_utf8.empty())
        return;
    const std::wstring w = utf8_to_wide(filetree_folder_utf8);
    std::error_code    ec;
    if (fs::is_directory(fs::path(w), ec) && !ec)
        m_explorerLastFolderPathW = try_canonical_path_w(w, true);
    else
        m_explorerLastFolderPathW = w;
}

void CMainFrame::PushRecentFolderFromShellPath(const std::wstring& folder)
{
    if (folder.empty())
        return;
    std::error_code ec;
    if (!fs::is_directory(fs::path(folder), ec) || ec)
        return;
    const std::wstring canon = try_canonical_path_w(folder, true);
    if (!m_explorerLastFolderPathW.empty()
        && folders_same_lineage_fs(fs::path(m_explorerLastFolderPathW), fs::path(canon))) {
        m_explorerLastFolderPathW = canon;
        return;
    }
    m_explorerLastFolderPathW = canon;
    PushRecentFolderExplicit(canon);
}

void CMainFrame::OnRecentFileMenu(UINT id)
{
    const int idx = static_cast<int>(id - IDM_RECENT_FILE_FIRST);
    if (idx < 0 || idx >= static_cast<int>(m_recentFiles.size()))
        return;
    const std::wstring path = m_recentFiles[static_cast<size_t>(idx)];
    PushRecentFilesFromUserAdd({ path });
    if (IsViewerWorkbench() && !m_pDockQueue) {
        m_previewCoord.Request(pmui::PreviewSource::RecentFile, { path },
                               m_viewerManager.ActiveView(), *m_workbench, *this);
        SelectPathInFileTree(path);
    } else {
        AddFilesToQueue({ path }, true);
    }
}

void CMainFrame::OnRecentFolderMenu(UINT id)
{
    const int idx = static_cast<int>(id - IDM_RECENT_FOLDER_FIRST);
    if (idx < 0 || idx >= static_cast<int>(m_recentFolders.size()))
        return;
    const std::wstring folder = m_recentFolders[static_cast<size_t>(idx)];
    PushRecentFolderExplicit(folder);
    if (m_pDockFileTree) {
        EnsurePanelVisible(m_pDockFileTree, DS_DOCKED_LEFT, GetDockAncestor(), DpiScaleInt(240),
            IDC_CMD_VIEW_FILETREE);
        ApplyFileTreeBrowseForPaths({ folder });
        return;
    }
    if (IsViewerWorkbench() && !m_pDockQueue)
        LoadFirstStartupPreviewFromPaths({ folder });
    else
        AddFilesToQueue({ folder }, true);
}

void CMainFrame::LoadFirstStartupPreviewFromPaths(const std::vector<std::wstring>& raw_paths)
{
    std::vector<std::wstring> flat;
    for (const auto& w : raw_paths) {
        std::vector<std::wstring> parts;
        split_semicolon_path_args(w, &parts);
        for (const auto& part : parts) {
            std::error_code ec;
            const fs::path  p(part);
            if (fs::is_directory(p, ec) && !ec) {
                const std::vector<fs::path> prev = pmui::queue_paths::previewable_files_in_directory(p, ec);
                if (!prev.empty())
                    flat.push_back(fs::absolute(prev.front(), ec).wstring());
            } else if (fs::is_regular_file(p, ec) && !ec) {
                flat.push_back(fs::absolute(p, ec).wstring());
            } else if (path_string_has_wildcards(part)) {
                std::error_code gec;
                const std::vector<fs::path> globs =
                    pmui::queue_paths::previewable_paths_matching_filename_wildcard(p, gec);
                if (!globs.empty())
                    flat.push_back(fs::absolute(globs.front(), gec).wstring());
            }
        }
    }
    m_previewCoord.Request(pmui::PreviewSource::CliStartup, flat,
                           m_viewerManager.ActiveView(), *m_workbench, *this);

    // Reveal the first file in the FileTreePanel so it is highlighted after
    // startup (same retry chain as openPathInternal / UWM_OPEN_PATH_INTERNAL).
    // Uses the same SelectPathInFileTree → ScheduleFileTreeBrowse → deferred
    // NavigateToFolder + SelectFile path; the retry timer handles the race
    // with shell view enumeration.
    if (!flat.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(flat.front(), ec)) {
            if (m_pDockFileTree)
                m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetInitialFiles(flat);
            SelectPathInFileTree(flat.front());
        }
    }
}

void CMainFrame::PreviewFirstFileInFolder(const std::wstring& folder)
{
    std::error_code ec;
    const fs::path  p(folder);
    if (!fs::is_directory(p, ec) || ec)
        return;
    const std::vector<fs::path> prev = pmui::queue_paths::previewable_files_in_directory(p, ec);
    if (prev.empty())
        return;
    ec = {};
    const std::wstring first = fs::absolute(prev.front(), ec).wstring();
    if (first.empty())
        return;
    m_previewCoord.Request(pmui::PreviewSource::Explorer, { first },
                           m_viewerManager.ActiveView(), *m_workbench, *this);
}
