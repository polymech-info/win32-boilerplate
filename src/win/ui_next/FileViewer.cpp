// FileViewer.cpp — CFileViewer: centre scroll view, image paint, drop target, shared chrome.
#include "stdafx.h"
#include "FileViewer.h"
#include "Mainfrm.h"
#include "Resource.h"
#include "core/glob_paths.hpp"
#include "file_extensions.hpp"
#include "helpers/default_shell.hpp"
#include "helpers/markdown_preview_engine.hpp"
#include "helpers/theme.hpp"
#include "helpers/ui_font.hpp"
#include "win/settings_store.hpp"
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
#include "CBlockView.hpp"
#endif
#include <wxx_webbrowser.h>     // CWebBrowser (IWebBrowser2 OLE wrapper)
#if defined(FEATURE_VIEWER_WEB)
#include "win/viewers/text/ViewerWebPanel.h"
#include "win/viewers/viewer_web_contract.hpp"
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
#include "win/web/CWebView.h"
#include "win/web/CWebViewManager.h"
#endif
#include <shellapi.h>
#include <shlwapi.h>            // SHCreateMemStream (RAW preview handlers)
#include <windowsx.h>           // GET_X_LPARAM / GET_Y_LPARAM
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <cwctype>
#include <system_error>
#include <nlohmann/json.hpp>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace {

/// Layout scale: +15% vs baseline px at 96 DPI reference.
inline int Ui115(int basePx96, int dpi)
{
    return ::MulDiv(::MulDiv(basePx96, 115, 100), dpi, 96);
}

std::wstring full_path_normalize_w(const std::wstring& p)
{
    wchar_t buf[MAX_PATH * 4]{};
    const DWORD n = ::GetFullPathNameW(p.c_str(), static_cast<DWORD>(std::size(buf)), buf, nullptr);
    if (n == 0 || n >= static_cast<DWORD>(std::size(buf)))
        return p;
    return std::wstring(buf, n);
}

#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
std::wstring home_web_shared_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0)
        return {};
    buf.resize(n);
    std::filesystem::path exeDir = std::filesystem::path(buf).parent_path();
    if (exeDir.filename() == L"win-x64")
        return (exeDir.parent_path() / L"shared").wstring();
    return (exeDir / L"shared").wstring();
}
#endif

} // namespace

namespace fs = std::filesystem;

namespace {

/// Same priority as @c CFileViewer::OpenFile (image → browser image → markdown → video → PDF → sheet → 3D → html → text).
int centre_preview_kind_for_path_ws(const std::wstring& p)
{
    std::wstring e = fs::path(p).extension().wstring();
    for (auto& c : e)
        c = static_cast<wchar_t>(towlower(c));
    if (pmui::is_image_ext(e))
        return 1;
    if (pmui::is_browser_image_ext(e))
        return 11;
    if (e == L".md" || e == L".markdown")
        return 2;
    if (pmui::is_video_preview_eligible_for_path(p))
        return 7;
    if (pmui::is_viewer_pdf_ext(e))
        return 5;
    if (pmui::is_viewer_spreadsheet_ext(e))
        return 6;
    if (pmui::is_viewer_3d_ext(e))
        return 4;
    if (pmui::is_html_ext(e))
        return 8;
#if defined(FEATURE_XBLOX) && FEATURE_XBLOX
    if (e == L".xblox")
        return 12;
#endif
    if (pmui::is_text_preview_eligible_for_path(p))
        return 3;
    return 0;
}

} // namespace

bool CFileViewer::PathMatchesCentreBrowseMask(const std::wstring& path, BROWSE_MASK mask) noexcept
{
    if (mask == BROWSE_MASK_NONE)
        return false;
    const int k = centre_preview_kind_for_path_ws(path);
    if (k == 0)
        return false;
    unsigned bit = 0;
    switch (k) {
    case 1: bit = static_cast<unsigned>(BROWSE_MASK_PICTURES); break;
    case 11: bit = static_cast<unsigned>(BROWSE_MASK_PICTURES); break;
    case 2: bit = static_cast<unsigned>(BROWSE_MASK_MARKDOWN); break;
    case 7: bit = static_cast<unsigned>(BROWSE_MASK_VIDEO); break;
    case 5: bit = static_cast<unsigned>(BROWSE_MASK_PDF); break;
    case 6: bit = static_cast<unsigned>(BROWSE_MASK_SPREADSHEET); break;
    case 4: bit = static_cast<unsigned>(BROWSE_MASK_THREE_D); break;
    case 8: bit = static_cast<unsigned>(BROWSE_MASK_TEXT_CODE); break; // html
#if defined(FEATURE_XBLOX) && FEATURE_XBLOX
    case 12: bit = static_cast<unsigned>(BROWSE_MASK_TEXT_CODE); break;
#endif
    case 3: bit = static_cast<unsigned>(BROWSE_MASK_TEXT_CODE); break;
    default: return false;
    }
    return (static_cast<unsigned>(mask) & bit) != 0u;
}

#if defined(FEATURE_D2D_GALLERY)
namespace {

bool PathMatchesFilmstripGalleryGlob(const std::wstring& path)
{
    switch (filmstrip::options::galleryGlob) {
    case filmstrip::options::GalleryGlob::IMAGES:
        return media::path_matches_gallery_glob(fs::path(path), media::GalleryGlob::IMAGES);
    }
    return false;
}

int FindNormalizedPathIndex(const std::vector<std::wstring>& paths, const std::wstring& needle)
{
    const std::wstring cur = full_path_normalize_w(needle);
    for (int i = 0; i < static_cast<int>(paths.size()); ++i) {
        if (::CompareStringOrdinal(full_path_normalize_w(paths[static_cast<size_t>(i)]).c_str(),
                                   -1, cur.c_str(), -1, true) == CSTR_EQUAL) {
            return i;
        }
    }
    return -1;
}

std::vector<std::wstring> BuildFilmstripGalleryPaths(const std::vector<std::wstring>& paths,
                                                     const std::wstring& loadedPath,
                                                     int& selectedIndex)
{
    selectedIndex = -1;
    std::vector<std::wstring> out;
    out.reserve(paths.size());
    const std::wstring cur = full_path_normalize_w(loadedPath);
    for (const auto& path : paths) {
        if (!PathMatchesFilmstripGalleryGlob(path))
            continue;
        if (::CompareStringOrdinal(full_path_normalize_w(path).c_str(), -1, cur.c_str(), -1, true)
            == CSTR_EQUAL) {
            selectedIndex = static_cast<int>(out.size());
        }
        out.push_back(path);
    }
    return out;
}

} // namespace
#endif

void CFileViewer::ClearImageNav() noexcept
{
    m_imageNavPaths.clear();
    m_imageNavIndex = -1;
    m_imagePill.SetShowImageNav(false);
}

bool CFileViewer::ImageNavHasNeighbors() const noexcept
{
    return m_imageNavPaths.size() > 1u;
}

void CFileViewer::RefreshImageNavFromParentOf(const std::wstring& loadedPath)
{
    std::error_code ec;
    const fs::path p = loadedPath;
    if (!fs::is_regular_file(p, ec) || ec)
        return;
    fs::path parent = p.parent_path();
    if (parent.empty())
        return;
    std::vector<std::wstring> found;
    for (const auto& ent : fs::directory_iterator(parent, fs::directory_options::skip_permission_denied, ec)) {
        if (ec)
            break;
        if (!ent.is_regular_file())
            continue;
        std::wstring ext = ent.path().extension().wstring();
        for (auto& c : ext)
            c = static_cast<wchar_t>(towlower(c));
        if (!PathMatchesCentreBrowseMask(ent.path().wstring(), m_centreBrowseMask))
            continue;
        found.push_back(ent.path().wstring());
    }
    if (found.size() <= 1u)
        return;
    const auto cmp_name = [](const std::wstring& a, const std::wstring& b) {
        const fs::path pa(a), pb(b);
        return ::CompareStringOrdinal(pa.filename().c_str(), -1, pb.filename().c_str(), -1, true) == CSTR_LESS_THAN;
    };
    std::sort(found.begin(), found.end(), cmp_name);
    const std::wstring cur = full_path_normalize_w(loadedPath);
    int idx = -1;
    for (int i = 0; i < static_cast<int>(found.size()); ++i) {
        if (::CompareStringOrdinal(full_path_normalize_w(found[i]).c_str(), -1, cur.c_str(), -1, true) == CSTR_EQUAL) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        const fs::path curp(loadedPath);
        for (int i = 0; i < static_cast<int>(found.size()); ++i) {
            std::error_code eqec;
            if (fs::equivalent(curp, fs::path(found[i]), eqec) && !eqec) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0)
        return;
    m_imageNavPaths = std::move(found);
    m_imageNavIndex = idx;
}

void CFileViewer::RebuildImageNavFromOpen(const std::vector<std::wstring>& paths, const std::wstring& loadedPath)
{
    m_imageNavPaths.clear();
    m_imageNavIndex = -1;
    m_imageNavPaths.reserve(paths.size());
    for (const auto& pw : paths) {
        std::wstring ext = fs::path(pw).extension().wstring();
        for (auto& c : ext)
            c = static_cast<wchar_t>(towlower(c));
        if (PathMatchesCentreBrowseMask(pw, m_centreBrowseMask))
            m_imageNavPaths.push_back(pw);
    }
    const std::wstring cur = full_path_normalize_w(loadedPath);
    for (int i = 0; i < static_cast<int>(m_imageNavPaths.size()); ++i) {
        if (::CompareStringOrdinal(full_path_normalize_w(m_imageNavPaths[i]).c_str(), -1, cur.c_str(), -1, true)
            == CSTR_EQUAL) {
            m_imageNavIndex = i;
            break;
        }
    }
    if (m_imageNavPaths.size() <= 1u)
        RefreshImageNavFromParentOf(loadedPath);
    if (m_imageNavIndex < 0 && !m_imageNavPaths.empty())
        m_imageNavIndex = 0;
    m_imagePill.SetShowImageNav(ImageNavHasNeighbors());
    if (ImageNavHasNeighbors())
        m_imagePill.SetVisible(true);
#if defined(FEATURE_D2D_GALLERY)
    int filmstripIndex = -1;
    std::vector<std::wstring> filmstripPaths =
        BuildFilmstripGalleryPaths(m_imageNavPaths, loadedPath, filmstripIndex);
    m_filmstrip.SetPaths(filmstripPaths);
    if (filmstripIndex >= 0)
        m_filmstrip.SetSelectionInitial(static_cast<size_t>(filmstripIndex));
#endif
}

void CFileViewer::SyncPreviewCoordinatorAfterNav(const std::wstring& path)
{
    CWnd* pAnc = GetCWndPtr(::GetAncestor(GetHwnd(), GA_ROOT));
    auto*   mf = dynamic_cast<CMainFrame*>(pAnc);
    if (mf)
        mf->PreviewCoordinator().AdoptNavigatedImage(path);
}

void CFileViewer::NavigateImageDelta(int delta)
{
    if (!ImageNavHasNeighbors() || m_imageNavIndex < 0 || delta == 0)
        return;
    const int n = static_cast<int>(m_imageNavPaths.size());
    int       j = (m_imageNavIndex + delta) % n;
    if (j < 0)
        j += n;
    if (j == m_imageNavIndex)
        return;
    const std::wstring& next = m_imageNavPaths[static_cast<size_t>(j)];
    if (OpenFile({next}, pmui::PreviewSource::AppBrowse) != pmui::PreviewStatus::Ok)
        return;
#if defined(FEATURE_D2D_GALLERY)
    int filmstripIndex = -1;
    m_filmstrip.SetPaths(BuildFilmstripGalleryPaths(m_imageNavPaths, next, filmstripIndex));
    if (filmstripIndex >= 0)
        m_filmstrip.SetSelectionCentered(static_cast<size_t>(filmstripIndex));
#endif
    SyncPreviewCoordinatorAfterNav(next);
}

void CFileViewer::NavigateImageFirstLast(bool first)
{
    if (!ImageNavHasNeighbors() || m_imageNavIndex < 0)
        return;
    const int n = static_cast<int>(m_imageNavPaths.size());
    const int target = first ? 0 : (n - 1);
    if (target == m_imageNavIndex)
        return;
    const std::wstring& p = m_imageNavPaths[static_cast<size_t>(target)];
    if (OpenFile({p}, pmui::PreviewSource::AppBrowse) != pmui::PreviewStatus::Ok)
        return;
#if defined(FEATURE_D2D_GALLERY)
    int filmstripIndex = -1;
    m_filmstrip.SetPaths(BuildFilmstripGalleryPaths(m_imageNavPaths, p, filmstripIndex));
    if (filmstripIndex >= 0)
        m_filmstrip.SetSelectionCentered(static_cast<size_t>(filmstripIndex));
#endif
    SyncPreviewCoordinatorAfterNav(p);
}

bool CFileViewer::TryConsumeImageNavKeys(WPARAM vk, bool ctrlDown, bool altDown)
{
    if (ctrlDown || altDown)
        return false;
    if (!(HasLoadedImagePreview() || m_textMode || m_mdMode))
        return false;
    if (m_imageActiveTool && m_imageActiveTool->IsActive())
        return false;
#if defined(FEATURE_D2D_GALLERY)
    if (!filmstrip::options::allowKeyboardNav)
        return false;
#endif
    if (!ImageNavHasNeighbors())
        return false;
    if (vk == VK_LEFT) {
        NavigateImageDelta(-1);
        return true;
    }
    if (vk == VK_RIGHT) {
        NavigateImageDelta(1);
        return true;
    }
    if (vk == VK_HOME) {
        NavigateImageFirstLast(true);
        return true;
    }
    if (vk == VK_END) {
        NavigateImageFirstLast(false);
        return true;
    }
    return false;
}

bool CFileViewer::TryConsumeImageNavXButtons(unsigned xbtn) noexcept
{
    if (!(HasLoadedImagePreview() || m_textMode || m_mdMode))
        return false;
    if (m_imageActiveTool && m_imageActiveTool->IsActive())
        return false;
    if (!ImageNavHasNeighbors())
        return false;
    if (xbtn == XBUTTON1) {
        NavigateImageDelta(-1);
        return true;
    }
    if (xbtn == XBUTTON2) {
        NavigateImageDelta(1);
        return true;
    }
    return false;
}

// ── Construction / destruction ────────────────────────────────────────────────

CFileViewer::CFileViewer()
{
    m_label = L"Drop images here or use Add Files";
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&m_gdipToken, &si, nullptr);
}

void CFileViewer::PreCreate(CREATESTRUCT& cs)
{
    CScrollView::PreCreate(cs);
    // Match chat WebView host: ScrollView + embedded WebView2 otherwise pick up a sunken client edge.
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    cs.style &= ~(WS_BORDER | WS_DLGFRAME);
}

void CFileViewer::SetFullButtonTogglesFrameFullscreen(bool enable) noexcept
{
    m_fullBtnTogglesFrameFs = enable;
}

bool CFileViewer::FullButtonTogglesFrameFullscreen() const noexcept
{
    return m_fullBtnTogglesFrameFs;
}

CFileViewer::~CFileViewer()
{
    m_imageActiveTool.reset();
    m_imagePill.ReleaseResources();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    // wait_for_join=true is critical: workers no longer touch GDI+, but
    // they DO call into libvips. We must let any in-flight Stage 2 finish
    // (or at least reach its post-decode cancel check) BEFORE the GDI+
    // token below is shut down — and before the OS thread is yanked at
    // process exit, which can leave libvips' GLib type system in an
    // inconsistent state for the next launch in the same process.
    CancelRawThreads(/*wait_for_join=*/true);
#endif
    DropImageAndStream();
    if (m_textFont)  ::DeleteObject(m_textFont);
    if (GetHwnd()) {
        ::KillTimer(GetHwnd(), TIMER_MD_CHROME);
        ::KillTimer(GetHwnd(), TIMER_IMAGE_INTERACTION_SETTLE);
    }
#if defined(FEATURE_VIEWER_WEB)
    ReleaseViewerWebFolderMapping();
    delete static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    m_viewerMdPanel = nullptr;
#endif
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    DestroyXbloxWebView();
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    DestroyHomeWebView();
#endif
    ClearOpenSCADTempPreviewFile();
    delete static_cast<CWebBrowser*>(m_browser);
    if (!m_mdTempHtml.empty()) {
        std::error_code ec; std::filesystem::remove(m_mdTempHtml, ec);
    }
    // m_hTextEdit is a child window — destroyed automatically with the parent.
#if defined(FEATURE_D2D_GALLERY)
    m_filmstrip.Shutdown();
#endif
    if (m_gdipToken) Gdiplus::GdiplusShutdown(m_gdipToken);
}

void CFileViewer::ClearOpenSCADTempPreviewFile() noexcept
{
    if (m_openScadTempStlPath.empty())
        return;
    std::error_code ec;
    std::filesystem::remove(m_openScadTempStlPath, ec);
    m_openScadTempStlPath.clear();
    m_openScadSourcePath.clear();
}

void CFileViewer::PushOpenSCADPreviewToWeb(const std::wstring& sourcePathW,
                                           const std::wstring& previewPathW,
                                           std::uintmax_t previewSize,
                                           std::string_view err_utf8)
{
#if !defined(FEATURE_VIEWER_WEB)
    (void)sourcePathW;
    (void)previewPathW;
    (void)previewSize;
    (void)err_utf8;
#else
    using pmui::viewer_web::k_viewer_kind_openscad;
    if (!pmui::markdown_preview_engine_is_webview2()) {
        ClearMarkdown();
        m_label = L"OpenSCAD preview requires WebView2";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return;
    }
    EnsureViewerMdPanel();
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    if (!pv || !pv->IsWindow()) {
        DestroyViewerMdPanel();
        m_label = L"Could not create OpenSCAD preview";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return;
    }

    std::filesystem::path scadPath(sourcePathW);
    std::wstring folderW = scadPath.parent_path().wstring();
    if (folderW.empty())
        folderW = L".";
    std::vector<std::wstring> sel = {sourcePathW};
    pv->SetContext(sel, folderW, k_viewer_kind_openscad);
    pv->SetThreeClientPreview(previewPathW, previewSize, err_utf8);
    pv->SetOpenSCADSourceText(m_openScadSourceUtf8);
    pv->RefreshChromeForTheme();
    LayoutViewerMdPanel();
    ::ShowWindow(*pv, SW_SHOW);
    if (m_browser) {
        auto* wb = static_cast<CWebBrowser*>(m_browser);
        if (wb->IsWindow()) ::ShowWindow(*wb, SW_HIDE);
    }
    m_mdMode = true;
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
#endif
}

LRESULT CFileViewer::OnOpenSCADCompileDone(WPARAM wparam, LPARAM lparam)
{
    auto* result = reinterpret_cast<OpenScadCompileResult*>(wparam);
    if (!result)
        return 0;
    std::unique_ptr<OpenScadCompileResult> hold(result);

    const unsigned expectedToken = static_cast<unsigned>(lparam);
    if (result->token != expectedToken || result->token != m_openScadCompileToken.load()) {
        if (result->success && !result->previewPathW.empty()) {
            std::error_code ec_rm;
            std::filesystem::remove(result->previewPathW, ec_rm);
        }
        return 0;
    }

    m_openScadCompileInFlight.store(false);
    if (result->success) {
        ClearOpenSCADTempPreviewFile();
        m_openScadTempStlPath = result->previewPathW;
        m_openScadSourcePath = result->sourcePathW;
        m_openScadSourceUtf8 = result->sourceUtf8;
    }

    const std::wstring previewPath = result->success ? result->previewPathW : result->sourcePathW;
    const std::string err = result->success ? std::string{} : result->errUtf8;
    PushOpenSCADPreviewToWeb(result->sourcePathW, previewPath, result->previewSize, err);
    if (auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel); pv && pv->IsWindow()) {
        nlohmann::json o;
        o["t"] = "vw_openscad_preview_result";
        o["ok"] = result->success;
        if (!result->success && !result->errUtf8.empty())
            o["err"] = result->errUtf8;
        pv->PostRawJson(o.dump());
    }
    return 0;
}

// Image load / zoom / `DropImageAndStream`: FileViewer_Images.cpp.
// Default `IFileViewerImageTool` (crop) + encoders / chrome: FileViewerImageCropTool.cpp.

void CFileViewer::EnsureImageTool()
{
    if (!m_imageActiveTool || m_imageActiveTool->Kind() != PmImageToolKind::Crop)
        m_imageActiveTool = PmCreateFileViewerImageTool(PmImageToolKind::Crop);
}

bool CFileViewer::CanSaveImagePreviewOverwrite() const noexcept
{
    return HasLoadedImagePreview() && !m_previewPath.empty();
}

void CFileViewer::OnImageToolFinishRequest()
{
    if (!m_imageActiveTool || !m_imageActiveTool->IsActive() || !m_pImage)
        return;
    Gdiplus::Bitmap* const b = m_imageActiveTool->TryCommitInMemoryEdit(*this);
    const PmImageToolKind  k = m_imageActiveTool->Kind();
    m_imageActiveTool->SetActive(*this, false);
    OnImageToolFinish(k, b);
}

void CFileViewer::OnImageToolFinish(PmImageToolKind /*finishedTool*/, Gdiplus::Bitmap* newImageOrNull)
{
    if (!newImageOrNull) {
        Invalidate(FALSE);
        return;
    }
    delete m_pImage;
    m_pImage = newImageOrNull;
    if (m_imgStream) {
        m_imgStream->Release();
        m_imgStream = nullptr;
    }
    if (m_imageActiveTool)
        m_imageActiveTool->OnHostReplacedPreviewImage(*this,
            static_cast<int>(m_pImage->GetWidth()), static_cast<int>(m_pImage->GetHeight()));
    ResetView();
    ApplyDefaultImageZoom();
    Invalidate(FALSE);
}

bool CFileViewer::SaveImagePreviewOverwrite(CString& errOut)
{
    EnsureImageTool();
    return m_imageActiveTool && m_imageActiveTool->SaveOverwrite(*this, errOut);
}

bool CFileViewer::SaveImagePreviewSaveAs(HWND owner, CString& errOut)
{
    EnsureImageTool();
    return m_imageActiveTool && m_imageActiveTool->SaveAs(*this, owner, errOut);
}

int CFileViewer::OnCreate(CREATESTRUCT&)
{
    const auto& pal0 = pmui::theme_palette();
    m_bgBrush.CreateSolidBrush(pal0.window_bg);
    SetClassLongPtr(GCLP_HBRBACKGROUND, (LONG_PTR)m_bgBrush.GetHandle());
    SetScrollBkgnd(m_bgBrush);
    DragAcceptFiles(TRUE);

    GESTURECONFIG gc[] = {
        { GID_ZOOM,   GC_ZOOM, 0 },
        { GID_PAN,    GC_PAN,  0 },
        { GID_ROTATE, 0, GC_ROTATE },
    };
    ::SetGestureConfig(GetHwnd(), 0, _countof(gc), gc, sizeof(GESTURECONFIG));

#if defined(FEATURE_D2D_GALLERY)
    m_filmstrip.Initialize(GetHwnd(), {});
    m_filmstrip.OnSelectionChanged([this](size_t index, const std::wstring& path) {
        // User clicked a filmstrip thumb: load the image and update nav index.
        if (OpenFile({path}, pmui::PreviewSource::AppBrowse) == pmui::PreviewStatus::Ok) {
            const int navIndex = FindNormalizedPathIndex(m_imageNavPaths, path);
            m_imageNavIndex = (navIndex >= 0) ? navIndex : static_cast<int>(index);
            SyncPreviewCoordinatorAfterNav(path);
        }
    });
#endif
    return 0;
}

// ── §8 OpenFile — unified preview dispatch ──────────────────────────────────────

pmui::PreviewStatus CFileViewer::OpenFile(const std::vector<std::wstring>& paths,
                                           pmui::PreviewSource /*source*/)
{
    ClearImageNav();
    ClearOpenSCADTempPreviewFile();

    // Check CLI --app override (e.g., --app agent-flow forces agent flow viewer)
    std::string viewer_app_override;
    bool has_override = media::settings::peek_ui_viewer_app_cli_override(viewer_app_override);

    // Priority: native image > browser image > markdown > video > PDF > spreadsheet > 3D > html > agent-flow > text.
    auto ext_of = [](const std::wstring& p) {
        std::wstring e = std::filesystem::path(p).extension().wstring();
        for (auto& c : e) c = static_cast<wchar_t>(towlower(c));
        return e;
    };
    auto is_markdown_ext = [](const std::wstring& e) {
        return e == L".md" || e == L".markdown";
    };

    const std::wstring* pick = nullptr;
    // 0=none 1=image 2=md 3=text 4=3d 5=pdf 6=spreadsheet 7=video 8=html 9=agent-flow 10=openscad 11=browser-image 12=xblox
    int kind = 0;

    // If --app agent-flow is specified, open any .json file as agent flow
    if (has_override && viewer_app_override == "agent-flow") {
        for (const auto& p : paths) {
            if (ext_of(p) == L".json") {
                pick = &p;
                kind = 9;
                break;
            }
        }
    }

    for (const auto& p : paths) { auto e = ext_of(p); if (pmui::is_image_ext(e))              { pick = &p; kind = 1; break; } }
    if (!pick) for (const auto& p : paths) { auto e = ext_of(p); if (pmui::is_browser_image_ext(e)) { pick = &p; kind = 11; break; } }
    if (!pick) for (const auto& p : paths) { auto e = ext_of(p); if (is_markdown_ext(e))       { pick = &p; kind = 2; break; } }
    if (!pick) for (const auto& p : paths) {
        if (pmui::is_video_preview_eligible_for_path(p)) {
            pick = &p;
            kind = 7;
            break;
        }
    }
    if (!pick) for (const auto& p : paths) { auto e = ext_of(p); if (pmui::is_viewer_pdf_ext(e))         { pick = &p; kind = 5; break; } }
    if (!pick) for (const auto& p : paths) { auto e = ext_of(p); if (pmui::is_viewer_spreadsheet_ext(e)) { pick = &p; kind = 6; break; } }
    if (!pick) for (const auto& p : paths) {
        auto e = ext_of(p);
        if (e == L".scad") { pick = &p; kind = 10; break; }
        if (pmui::is_viewer_3d_ext(e)) { pick = &p; kind = 4; break; }
    }
    if (!pick) for (const auto& p : paths) { auto e = ext_of(p); if (pmui::is_html_ext(e))               { pick = &p; kind = 8; break; } }
#if defined(FEATURE_XBLOX) && FEATURE_XBLOX
    if (!pick) for (const auto& p : paths) { auto e = ext_of(p); if (e == L".xblox")                     { pick = &p; kind = 12; break; } }
#endif
    // agent-flow requires explicit --app agent-flow (no auto-detection)
    if (!pick) for (const auto& p : paths) { if (pmui::is_text_preview_eligible_for_path(p))              { pick = &p; kind = 3; break; } }

    if (!pick) {
        // Nothing previewable — clear everything.
        ClearPicture();
        ClearText();
        ClearMarkdown();
#if defined(FEATURE_VIEWER_WEB)
        ReleaseViewerWebFolderMapping();
#endif
#if defined(FEATURE_D2D_GALLERY)
        m_filmstrip.SetPaths({});
#endif
        return pmui::PreviewStatus::NothingPreviewable;
    }

    bool ok = false;
    switch (kind) {
    case 1: ok = LoadPicture(pick->c_str());     break;
    case 11: ok = LoadBrowserImage(pick->c_str()); break;
    case 2: ok = LoadMarkdown(pick->c_str());    break;
    case 7: ok = LoadVideo(pick->c_str());       break;
    case 5: ok = LoadPdf(pick->c_str());         break;
    case 6: ok = LoadSpreadsheet(pick->c_str()); break;
    case 4: ok = LoadThreeD(pick->c_str());      break;
    case 10: ok = LoadOpenSCAD(pick->c_str());   break;
    case 8: ok = LoadHtml(pick->c_str());        break;
    case 9: ok = LoadAgentFlow(pick->c_str());   break;
#if defined(FEATURE_XBLOX) && FEATURE_XBLOX && defined(FEATURE_BROWSER)
    case 12: ok = LoadXblox(pick->c_str());       break;
#elif defined(FEATURE_XBLOX) && FEATURE_XBLOX
    case 12: ok = LoadText(pick->c_str());        break;
#endif
    case 3: ok = LoadText(pick->c_str());        break;
    default: break;
    }

    if (ok)
        RebuildImageNavFromOpen(paths, *pick);

    return ok ? pmui::PreviewStatus::Ok : pmui::PreviewStatus::Failed;
}

#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
bool CFileViewer::LoadXblox(LPCWSTR path)
{
    if (!path || !path[0])
        return false;

    ClearPicture();
    ClearText();
    ClearMarkdown();
#if defined(FEATURE_VIEWER_WEB)
    ReleaseViewerWebFolderMapping();
#endif
    SetFileInfoFromPath(path);

    EnsureXbloxWebView();
    auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
    if (!xblox || !xblox->IsWindow()) {
        DestroyXbloxWebView();
        m_label = L"Could not create XBlox view";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    const fs::path xblox_path(path);
    xblox->SetContext({std::wstring(path)}, xblox_path.parent_path().wstring());
    if (!xblox->OpenXbloxFile(path, m_busManager)) {
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }
    LayoutXbloxWebView();
    ::ShowWindow(*xblox, SW_SHOW);
    m_mdMode = true;
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
    return true;
}
#endif

// ── Text preview ─────────────────────────────────────────────────────────────

void CFileViewer::EnsureTextEdit()
{
    if (m_hTextEdit) return;

    // Lazy-create a child read-only EDIT control.  Standard Win32 EDIT handles
    // ~ tens of MB on Win10/11 and supports vertical / horizontal scroll bars,
    // mouse-wheel scrolling, selection, and Ctrl+C / Ctrl+A out of the box.
    m_hTextEdit = ::CreateWindowExW(
        0,
        L"EDIT", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | ES_NOHIDESEL,
        0, 0, 0, 0,
        GetHwnd(), nullptr, ::GetModuleHandleW(nullptr), nullptr);

    // Monospace font: prefer Cascadia Mono (Win10 22H2+), fall back to Consolas.
    // Base size = 10pt + the user's "App Settings → Font size" extra pts so
    // the text preview scales together with every other panel.
    LOGFONTW lf{};
    {
        const int dpi = ::GetDeviceCaps(::GetDC(nullptr), LOGPIXELSY);
        const int base_pt  = 10 + pmui::ui_font_extra_pt();
        lf.lfHeight  = -::MulDiv(base_pt, dpi ? dpi : 96, 72);
    }
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, L"Cascadia Mono", LF_FACESIZE);
    m_textFont = ::CreateFontIndirectW(&lf);
    if (!m_textFont) {
        lstrcpynW(lf.lfFaceName, L"Consolas", LF_FACESIZE);
        m_textFont = ::CreateFontIndirectW(&lf);
    }
    if (m_textFont)
        ::SendMessageW(m_hTextEdit, WM_SETFONT, (WPARAM)m_textFont, TRUE);
    ::SendMessageW(
        m_hTextEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));

    // Lift the default 32 KB Win9x-era limit so 5 MB files load cleanly.
    ::SendMessageW(m_hTextEdit, EM_SETLIMITTEXT,
                   static_cast<WPARAM>(kTextLimitBytes * 2), 0);
    LayoutTextEdit();
    ApplyPreviewChildTheming();
}

void CFileViewer::LayoutTextEdit()
{
    if (!m_hTextEdit) return;
    CRect rc = GetClientRect();
    ::SetWindowPos(m_hTextEdit, nullptr,
                   0, 0, rc.Width(), rc.Height(),
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
void CFileViewer::EnsureXbloxWebView()
{
    if (m_xbloxWebView) {
        auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
        if (xblox->IsWindow())
            return;
        delete xblox;
        m_xbloxWebView = nullptr;
    }

    auto* xblox = new pmui::CBlockView();
    try {
        xblox->Create(GetHwnd());
    } catch (...) {
        delete xblox;
        return;
    }
    m_xbloxWebView = xblox;
    xblox->SetBusManager(m_busManager);
    LayoutXbloxWebView();
    ApplyPreviewChildTheming();
}

void CFileViewer::LayoutXbloxWebView()
{
    if (!m_xbloxWebView)
        return;
    auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
    if (!xblox->IsWindow())
        return;
    const CRect rc = GetClientRect();
    ::SetWindowPos(*xblox, nullptr, 0, 0, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
}

void CFileViewer::SetXbloxContext(const std::vector<std::wstring>& selection, const std::wstring& folder)
{
    if (!m_xbloxWebView)
        return;
    auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
    if (xblox && xblox->IsWindow())
        xblox->SetContext(selection, folder);
}

void CFileViewer::DestroyXbloxWebView()
{
    if (!m_xbloxWebView)
        return;
    auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
    xblox->SetBusManager(nullptr);
    delete xblox;
    m_xbloxWebView = nullptr;
}
#endif

#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
void CFileViewer::EnsureHomeWebView()
{
    if (m_homeWebView) {
        auto* home = static_cast<CWebView*>(m_homeWebView);
        if (home->IsWindow())
            return;
        delete home;
        m_homeWebView = nullptr;
    }

    auto* home = new CWebView();
    CWebViewOptions opts = CWebViewOptions::ForDocked(/*devTools=*/true);
    opts.vhostName = L"pm-home.invalid";
    opts.localFolder = home_web_shared_dir();
    opts.url = L"https://pm-home.invalid/home.html";
    opts.mapFixedDriveFileHosts = true;
    opts.onMessage = [this](const std::string& json_utf8) {
        if (m_busManager)
            m_busManager->HandleExternalMessage("home", json_utf8);
    };
    home->SetOptions(std::move(opts));
    home->Create(GetHwnd());
    m_homeWebView = home;
    if (m_busManager)
        m_busManager->RegisterExternal("home", home);
    LayoutHomeWebView();
    ApplyPreviewChildTheming();
}

void CFileViewer::LayoutHomeWebView()
{
    if (!m_homeWebView)
        return;
    auto* home = static_cast<CWebView*>(m_homeWebView);
    if (!home->IsWindow())
        return;
    const CRect rc = GetClientRect();
    ::SetWindowPos(*home, nullptr, 0, 0, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
}

void CFileViewer::DestroyHomeWebView()
{
    if (!m_homeWebView)
        return;
    auto* home = static_cast<CWebView*>(m_homeWebView);
    if (m_busManager)
        m_busManager->UnregisterExternal("home", home);
    delete home;
    m_homeWebView = nullptr;
}

bool CFileViewer::LoadHome()
{
    ClearText();
    ClearMarkdown();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label.Empty();
    m_previewPath.clear();
    ClearFileInfo();
#if defined(FEATURE_VIEWER_WEB)
    ReleaseViewerWebFolderMapping();
#endif

    EnsureHomeWebView();
    auto* home = static_cast<CWebView*>(m_homeWebView);
    if (!home || !home->IsWindow()) {
        m_label = L"Could not create Home view";
        SetScrollSizes(CSize(0, 0));
        Invalidate(FALSE);
        return false;
    }

    home->Navigate(L"https://pm-home.invalid/home.html");
    LayoutHomeWebView();
    ::ShowWindow(*home, SW_SHOW);
    m_mdMode = true;
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
    return true;
}
#endif

void CFileViewer::ApplyPreviewChildTheming()
{
    if (!GetHwnd()) return;
    pmui::apply_window_theme_recursive(
        GetHwnd(), pmui::theme_palette().dark);
    if (m_browser) {
        auto* wb = static_cast<CWebBrowser*>(m_browser);
        if (wb->IsWindow()) {
            pmui::nuke_webview2_host_chrome(
                wb->GetHwnd(), pmui::theme_palette().window_bg);
        }
    }
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    if (m_xbloxWebView) {
        auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
        if (xblox->IsWindow()) {
            xblox->RefreshChromeForTheme();
            pmui::nuke_webview2_host_chrome(GetHwnd(), pmui::theme_palette().web_surface_bg);
            pmui::flatten_webview_host_parent_chain(GetHwnd(), pmui::theme_palette().web_surface_bg, 16);
        }
    }
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    if (m_homeWebView) {
        auto* home = static_cast<CWebView*>(m_homeWebView);
        if (home->IsWindow()) {
            home->RefreshChromeForTheme();
        }
    }
#endif
#if defined(FEATURE_VIEWER_WEB)
    if (m_viewerMdPanel) {
        auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
        if (pv->IsWindow()) {
            pv->RefreshChromeForTheme();
            // Flatten the scroll-view client (parent of `CViewerWebPanel`) so Win32++ / theme
            // does not leave a bright halo around the WebView2 island.
            pmui::nuke_webview2_host_chrome(GetHwnd(), pmui::theme_palette().web_surface_bg);
            pmui::flatten_webview_host_parent_chain(GetHwnd(), pmui::theme_palette().web_surface_bg, 16);
        }
    }
#endif
}

void CFileViewer::RefreshThemeChrome()
{
    if (!GetHwnd()) return;
    const auto& pal = pmui::theme_palette();
    m_bgBrush.Destroy();
    m_bgBrush.CreateSolidBrush(pal.window_bg);
    ::SetClassLongPtr(
        GetHwnd(), GCLP_HBRBACKGROUND, (LONG_PTR)m_bgBrush.GetHandle());
    SetScrollBkgnd(m_bgBrush);
    ApplyPreviewChildTheming();
    m_imagePill.ReleaseResources();
    if (m_imageActiveTool)
        m_imageActiveTool->OnDpiOrThemeChromeChange(*this);
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
    if (m_hTextEdit)
        ::InvalidateRect(m_hTextEdit, nullptr, TRUE);
}

// LoadText: FileViewer_Text.cpp — markdown/browser: FileViewer_Markdown.cpp — PDF/video/3D: FileViewer_PDF.cpp /
// FileViewer_Video.cpp / FileViewer_3D.cpp

void CFileViewer::ClearText()
{
    m_textMode = false;
    if (m_hTextEdit) {
        ::SetWindowTextW(m_hTextEdit, L"");
        ::ShowWindow(m_hTextEdit, SW_HIDE);
    }
}

void CFileViewer::ClearMarkdown()
{
    m_mdMode = false;
    if (GetHwnd()) ::KillTimer(GetHwnd(), TIMER_MD_CHROME);
    if (m_browser) {
        auto* wb = static_cast<CWebBrowser*>(m_browser);
        if (wb->IsWindow()) ::ShowWindow(*wb, SW_HIDE);
    }
#if defined(FEATURE_VIEWER_WEB)
    if (m_viewerMdPanel) {
        auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
        if (pv->IsWindow()) ::ShowWindow(*pv, SW_HIDE);
    }
#endif
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    DestroyXbloxWebView();
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    if (m_homeWebView) {
        auto* home = static_cast<CWebView*>(m_homeWebView);
        if (home->IsWindow()) ::ShowWindow(*home, SW_HIDE);
    }
#endif
    if (!m_mdTempHtml.empty()) {
        std::error_code ec; std::filesystem::remove(m_mdTempHtml, ec);
        m_mdTempHtml.clear();
    }
}

#if defined(FEATURE_VIEWER_WEB)
void CFileViewer::ReleaseViewerWebFolderMapping()
{
    if (!m_viewerMdPanel) return;
    auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
    pv->SetContext({}, L"", pmui::viewer_web::k_viewer_kind_markdown);
}
#endif

#if defined(FEATURE_VIEWER_WEB) || defined(FEATURE_BROWSER) || (defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER))
void CFileViewer::SetBusManager(CWebViewManager* manager)
{
#if defined(FEATURE_BROWSER) && defined(FEATURE_HOME_PAGE)
    CWebViewManager* oldManager = m_busManager;
#endif
    m_busManager = manager;
#if defined(FEATURE_VIEWER_WEB)
    if (m_viewerMdPanel) {
        auto* pv = static_cast<CViewerWebPanel*>(m_viewerMdPanel);
        pv->SetBusManager(manager);
    }
#endif
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    if (m_xbloxWebView) {
        auto* xblox = static_cast<pmui::CBlockView*>(m_xbloxWebView);
        xblox->SetBusManager(manager);
    }
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    if (m_homeWebView) {
        auto* home = static_cast<CWebView*>(m_homeWebView);
        if (oldManager && oldManager != manager)
            oldManager->UnregisterExternal("home", home);
        if (manager)
            manager->RegisterExternal("home", home);
    }
#endif
}
#endif

// ── OnDraw — double-buffered (`DrawSpinner`: FileViewer_Images.cpp) ────────────

void CFileViewer::OnDraw(CDC& dc)
{
    // In text / markdown mode the child control paints itself over our client
    // area; nothing for the image renderer to do.
    if ((m_textMode && m_hTextEdit) || (m_mdMode && (m_browser || m_viewerMdPanel
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
        || m_xbloxWebView
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
        || m_homeWebView
#endif
        ))) return;

    CRect rc = GetClientRect();
    const int w = rc.Width(), h = rc.Height();
    if (w <= 0 || h <= 0) return;

    // ── Chrome heights — hoisted early so every path uses consistent values ──
    // Layout (bottom → top): [filename bar | barH] [filmstrip | dockH] [image]
    const int dpi  = (int)::GetDpiForWindow(GetHwnd());
    const int barH = Ui115(30, dpi);
#if defined(FEATURE_D2D_GALLERY)
    const int dockH = m_filmstrip.HasContent()
                      ? filmstrip::FilmStripWidget::DockHeightPx() : 0;
    const int filmstripPaintH = m_filmstrip.HasContent()
                      ? filmstrip::FilmStripWidget::PaintExtentPx() : 0;
#else
    constexpr int dockH = 0;
    constexpr int filmstripPaintH = 0;
#endif
    // Height of the image canvas (everything above filmstrip + filename bar).
    const int imageAreaH = (std::max)(0, h - dockH - barH);

#if defined(FEATURE_D2D_GALLERY)
    // If the filmstrip is the only dirty region (animation tick), skip the
    // expensive GDI+ image blit and paint the strip directly into the screen DC.
    if (m_filmstrip.HasContent() && m_pImage) {
        RECT upd{};
        if (::GetUpdateRect(GetHwnd(), &upd, FALSE)) {
            // Only bypass the image repaint when the dirty region is wholly
            // inside the reserved strip band. ScaleUp may paint above that band;
            // those overlay pixels need the main preview redrawn underneath.
            const int dockTop = h - barH - dockH;
            if (upd.top >= dockTop) {
                m_filmstrip.PaintOverGdi(dc.GetHDC(), w, h - barH,
                                         pmui::theme_palette().dark);
                return;
            }
        }
    }
#endif

    HDC     hMem = ::CreateCompatibleDC(dc.GetHDC());
    HBITMAP hBmp = ::CreateCompatibleBitmap(dc.GetHDC(), w, h);
    HBITMAP hOld = (HBITMAP)::SelectObject(hMem, hBmp);

    RECT bgRc = {0, 0, w, h};
    if (!m_pImage)
        ::FillRect(hMem, &bgRc, (HBRUSH)m_bgBrush.GetHandle());
    else {
        const auto&    pal      = pmui::theme_palette();
        const COLORREF canvasBg = pal.dark ? RGB(10, 10, 10) : RGB(255, 255, 255);
        HBRUSH         brCanvas = ::CreateSolidBrush(canvasBg);
        ::FillRect(hMem, &bgRc, brCanvas);
        ::DeleteObject(brCanvas);
    }

    if (m_spinnerActive && !m_pImage) {
        // ── Loading spinner (no image yet) ───────────────────────────────────
        DrawSpinner(hMem, w / 2, h / 2, 22);
        HFONT hPrev = (HFONT)::SelectObject(hMem,
            (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
        ::SetBkMode(hMem,    TRANSPARENT);
        ::SetTextColor(hMem, RGB(120, 120, 120));
        RECT tr = {w / 2 - 80, h / 2 + 34, w / 2 + 80, h / 2 + 52};
        ::DrawTextW(hMem, L"Loading RAW\u2026", -1, &tr, DT_CENTER | DT_SINGLELINE);
        ::SelectObject(hMem, hPrev);

    } else if (m_pImage) {
        // ── Image ─────────────────────────────────────────────────────────────
        const UINT iw = m_pImage->GetWidth(), ih = m_pImage->GetHeight();
        if (iw > 0 && ih > 0) {
            const double zoom = (m_zoom > 0) ? m_zoom : GetFitZoom();
            const double vcx  = (m_zoom > 0) ? m_viewCx : (iw * 0.5);
            const double vcy  = (m_zoom > 0) ? m_viewCy : (ih * 0.5);
            const int dw = (int)std::round(iw * zoom);
            const int dh = (int)std::round(ih * zoom);
            const int dx = (int)std::round(w * 0.5 - vcx * zoom);
            // Centre vertically inside the image canvas (above filmstrip + bar).
            const int dy = (int)std::round(imageAreaH * 0.5 - vcy * zoom);

            Gdiplus::Graphics gfx(hMem);
            const bool fastPaint = m_fastImageInteractionPaint || m_dragging;
            gfx.SetInterpolationMode(zoom > 3.0
                ? Gdiplus::InterpolationModeNearestNeighbor
                : (fastPaint ? Gdiplus::InterpolationModeLowQuality
                             : Gdiplus::InterpolationModeHighQualityBicubic));
            gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            gfx.SetCompositingQuality(fastPaint
                ? Gdiplus::CompositingQualityHighSpeed
                : Gdiplus::CompositingQualityHighQuality);
            if (zoom > 1e-12 && dw != 0 && dh != 0) {
                const double dstL = (std::max)(0.0, static_cast<double>(dx));
                const double dstT = (std::max)(0.0, static_cast<double>(dy));
                const double dstR = (std::min)(static_cast<double>(w), static_cast<double>(dx + dw));
                const double dstB = (std::min)(static_cast<double>(imageAreaH), static_cast<double>(dy + dh));
                if (dstR > dstL && dstB > dstT) {
                    const double srcX = (std::max)(0.0, (dstL - dx) / zoom);
                    const double srcY = (std::max)(0.0, (dstT - dy) / zoom);
                    const double srcW = (std::min)(static_cast<double>(iw) - srcX, (dstR - dstL) / zoom);
                    const double srcH = (std::min)(static_cast<double>(ih) - srcY, (dstB - dstT) / zoom);
                    if (srcW > 0.0 && srcH > 0.0) {
                        Gdiplus::ImageAttributes ia;
                        ia.SetWrapMode(Gdiplus::WrapModeClamp);
                        const Gdiplus::RectF imDst((Gdiplus::REAL)dstL, (Gdiplus::REAL)dstT,
                            (Gdiplus::REAL)(dstR - dstL), (Gdiplus::REAL)(dstB - dstT));
                        gfx.DrawImage(m_pImage, imDst,
                            (Gdiplus::REAL)srcX, (Gdiplus::REAL)srcY,
                            (Gdiplus::REAL)srcW, (Gdiplus::REAL)srcH,
                            Gdiplus::UnitPixel, &ia);
                    }
                }
            }

            if (m_imageActiveTool && m_imageActiveTool->IsActive()) {
                m_imageActiveTool->SetViewTransform(w, h, zoom, vcx, vcy);
                m_imageActiveTool->PaintOverlay(*this, hMem, GetHwnd());
                const int dpiChrome = (int)::GetDpiForWindow(GetHwnd());
                const int barHForOk = Ui115(30, dpiChrome);
                m_imageActiveTool->PaintChrome(*this, hMem, GetHwnd(), w, h, barHForOk, m_hoveredBtn);
            }
        }

        // ── Bottom bar: file name + size; click → default app ───────────────────
        // Layout: [image canvas] [filmstrip dockH] [filename bar barH]
        // dpi / barH / dockH already computed at the top of OnDraw.
        const auto& pal = pmui::theme_palette();
        m_rcFileNameBar.SetRect(0, h - barH, w, h);
        {
            const COLORREF barBg = pal.dark ? RGB(0, 0, 0) : RGB(255, 255, 255);
            HBRUSH         bBar  = ::CreateSolidBrush(barBg);
            RECT           rBar  = {m_rcFileNameBar.left, m_rcFileNameBar.top, m_rcFileNameBar.right,
                           m_rcFileNameBar.bottom};
            ::FillRect(hMem, &rBar, bBar);
            ::DeleteObject(bBar);
        }
        if (!m_fileInfoText.IsEmpty()) {
            ::SetBkMode(hMem, TRANSPARENT);
            const COLORREF fgName = pal.dark
                ? ((m_hoveredBtn == BTN_FILENAME) ? RGB(255, 255, 255) : RGB(210, 210, 210))
                : ((m_hoveredBtn == BTN_FILENAME) ? pal.window_fg : pal.caption_fg);
            ::SetTextColor(hMem, fgName);
            HFONT hSm = ::CreateFontW(
                -Ui115(11, dpi), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
            HFONT hPrevF = (HFONT)::SelectObject(hMem, hSm);
            RECT tr{m_rcFileNameBar.left + Ui115(14, dpi), m_rcFileNameBar.top,
                    m_rcFileNameBar.right - Ui115(14, dpi), m_rcFileNameBar.bottom};
            ::DrawTextW(hMem, m_fileInfoText, -1, &tr,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            ::SelectObject(hMem, hPrevF);
            ::DeleteObject(hSm);
        }

        const double fitZ = GetFitZoom();

        m_imagePill.Layout(w, imageAreaH, dpi);
        m_imagePill.EnsureIconBitmaps(GetHwnd());
        const int pillBottomForRaw = m_imagePill.PillBottomY();
        int       pillHover          = m_hoveredBtn;
        if (pillHover == BTN_IMG_PREV)
            pillHover = static_cast<int>(CFileViewerImagePillToolbar::kPrevImg);
        else if (pillHover == BTN_IMG_NEXT)
            pillHover = static_cast<int>(CFileViewerImagePillToolbar::kNextImg);
        else if (pillHover != BTN_ZOOM_OUT && pillHover != BTN_FIT && pillHover != BTN_FULL
                 && pillHover != BTN_CROP && pillHover != BTN_ZOOM_IN)
            pillHover = -1;
        m_imagePill.Paint(hMem, GetHwnd(), fitZ, m_zoom, pillHover);

#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
        if (!m_rawStatusLabel.IsEmpty()) {
            HFONT hPrev = (HFONT)::SelectObject(hMem, (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
            ::SetBkColor(hMem, pal.dark ? RGB(16, 16, 18) : RGB(245, 245, 245));
            ::SetBkMode(hMem, OPAQUE);
            ::SetTextColor(hMem, pal.dark ? RGB(200, 200, 200) : pal.caption_fg);
            RECT br = {Ui115(10, dpi), pillBottomForRaw + Ui115(6, dpi), (std::min)(w - 8, 360),
                pillBottomForRaw + Ui115(28, dpi)};
            ::DrawTextW(hMem, m_rawStatusLabel, -1, &br, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            ::SelectObject(hMem, hPrev);
            ::SetBkMode(hMem, TRANSPARENT);
        }
#endif

#if defined(FEATURE_RAW_PREVIEW) && defined(FEATURE_RAW_VIEW)
        if (m_spinnerActive && m_pImage) {
            DrawSpinner(hMem, w - 28, h - 28, 12);
        }
#endif

#if defined(FEATURE_D2D_GALLERY)
        if (dockH > 0) {
            // 1 px separator between the image canvas and the filmstrip top.
            const COLORREF sepClr = pal.dark ? RGB(35, 35, 35) : RGB(195, 195, 195);
            HBRUSH hSep = ::CreateSolidBrush(sepClr);
            RECT sepRc  = {0, imageAreaH, w, imageAreaH + 1};
            ::FillRect(hMem, &sepRc, hSep);
            ::DeleteObject(hSep);
        }
        // PaintOverGdi receives the height above the filename bar so the strip
        // occupies [imageAreaH+1 .. h-barH] and the filename bar sits below.
        if (dockH > 0)
            m_filmstrip.PaintOverGdi(hMem, w, h - barH, pal.dark);
#endif

    } else if (!m_label.IsEmpty()) {
        HFONT hPrev = (HFONT)::SelectObject(hMem,
            (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
        ::SetBkMode(hMem,    TRANSPARENT);
        ::SetTextColor(hMem, RGB(100, 100, 100));
        RECT rw = {0, 0, w, h};
        ::DrawTextW(hMem, m_label, -1, &rw, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::SelectObject(hMem, hPrev);
    }

    ::BitBlt(dc.GetHDC(), 0, 0, w, h, hMem, 0, 0, SRCCOPY);
    ::SelectObject(hMem, hOld);
    ::DeleteObject(hBmp);
    ::DeleteDC(hMem);
}

// DecodeVips*, CancelRawThreads, TryLoadPictureRawPipeline: FileViewer_Images.cpp

// ── WndProc ─────────────────────────────────────────────────────────────────────

LRESULT CFileViewer::OnDropFiles(WPARAM wparam)
{
    return GetAncestor().SendMessage(WM_DROPFILES, wparam, 0);
}

LRESULT CFileViewer::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {

        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE && m_pImage && m_imageActiveTool && m_imageActiveTool->IsActive()) {
                m_imageActiveTool->SetActive(*this, false);
                Invalidate(FALSE);
                return 0;
            }
            if (TryConsumeImageNavKeys(wparam, (::GetKeyState(VK_CONTROL) & 0x8000) != 0,
                    (::GetKeyState(VK_MENU) & 0x8000) != 0))
                return 0;
            break;

        case WM_XBUTTONDOWN:
            if (TryConsumeImageNavXButtons(GET_XBUTTON_WPARAM(wparam)))
                return 0;
            break;

        // ── Suppress background erase — we paint everything in OnDraw ─────────
        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            if (reinterpret_cast<HWND>(lparam) != m_hTextEdit) break;
            const auto& pal = pmui::theme_palette();
            HDC hdc = reinterpret_cast<HDC>(wparam);
            ::SetTextColor(hdc, pal.control_fg);
            ::SetBkColor(hdc,   pal.control_bg);
            static HBRUSH s_br  = nullptr;
            static COLORREF s_c = 0xFFFFFFFFu;
            if (!s_br || s_c != pal.control_bg) {
                if (s_br) ::DeleteObject(s_br);
                s_br = ::CreateSolidBrush(pal.control_bg);
                s_c  = pal.control_bg;
            }
            return reinterpret_cast<LRESULT>(s_br);
        }

        // ── Keep the embedded child controls sized to the client rect ────────
        case WM_SIZE:
            if (m_hTextEdit) LayoutTextEdit();
            if (m_browser)   LayoutBrowser();
            LayoutViewerMdPanel();
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
            LayoutXbloxWebView();
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
            LayoutHomeWebView();
#endif
            if (m_pImage && m_zoom > 0) {
                const double nf = GetFitZoom();
                if (m_fitZoomAtLayout > 1e-15)
                    m_zoom *= nf / m_fitZoomAtLayout;
                m_fitZoomAtLayout = nf;
                Invalidate(FALSE);
            }
            break;

        case WM_DPICHANGED:
            m_imagePill.ReleaseResources();
            if (m_imageActiveTool)
                m_imageActiveTool->OnDpiOrThemeChromeChange(*this);
            Invalidate(FALSE);
            break;

        case WM_DROPFILES:
            return OnDropFiles(wparam);

        // ── Filmstrip async thumb-ready ───────────────────────────────────────
#if defined(FEATURE_D2D_GALLERY)
        case filmstrip::FilmStripWidget::WM_THUMB_READY:
            m_filmstrip.FlushPendingUploads();
            return 0;
#endif

        // ── Spinner timer ─────────────────────────────────────────────────────
        case WM_TIMER:
#if defined(FEATURE_D2D_GALLERY)
            if (m_filmstrip.OnTimer(wparam)) return 0;
#endif
            if (wparam == TIMER_SPINNER && m_spinnerActive) {
                m_spinnerAngle = (m_spinnerAngle + 45) % 360;
                Invalidate(FALSE);
                return 0;
            }
            if (wparam == TIMER_MD_CHROME) {
                ::KillTimer(GetHwnd(), TIMER_MD_CHROME);
                if (m_browser) {
                    auto* wbm = static_cast<CWebBrowser*>(m_browser);
                    if (wbm->IsWindow())
                        ApplyPreviewChildTheming();
                }
                return 0;
            }
            if (wparam == TIMER_IMAGE_INTERACTION_SETTLE) {
                EndImageInteractionPaint(true);
                return 0;
            }
            break;

        case UWM_OPENSCAD_COMPILE_DONE:
            return OnOpenSCADCompileDone(wparam, lparam);

        // ── Stage 1: fast RAW preview arrived (raw JPEG bytes) ───────────────
#ifdef FEATURE_RAW_PREVIEW
        case UWM_RAW_PREVIEW_READY: {
            auto* bytes = reinterpret_cast<std::vector<unsigned char>*>(wparam);
            const int gen = static_cast<int>(lparam);

            // Stale generation OR window closing: drop bytes and bail.
            // The owning std::vector cleans itself up via std::unique_ptr
            // semantics — we always delete here so the worker side can
            // simply PostMessage(...) without worrying about stale-ness.
            if (gen != m_rawFastGen.load()) { delete bytes; return 0; }

            // Stop spinner — Stage 1 is in (success or failure).
            m_spinnerActive = false;
            ::KillTimer(GetHwnd(), TIMER_SPINNER);

            // Build the GDI+ Image on the UI thread, where GdiplusShutdown
            // can't race us. JPEG bytes are eagerly decoded by GDI+ via WIC,
            // so we don't need to keep the stream alive — pStream->Release()
            // immediately.
            DropImageAndStream();
            if (bytes && !bytes->empty()) {
                IStream* pStream = ::SHCreateMemStream(
                    bytes->data(), static_cast<UINT>(bytes->size()));
                if (pStream) {
                    auto* img = Gdiplus::Image::FromStream(pStream);
                    pStream->Release();
                    if (img && img->GetLastStatus() == static_cast<Gdiplus::Status>(0)) {
                        m_pImage = img;
                        m_label.Empty();
                        ApplyDefaultImageZoom();
                        SyncImageToolFromHostPicture();
                        m_imagePill.SetVisible(false);
                    } else {
                        delete img;
                        m_label = L"RAW preview decode failed";
                    }
                } else {
                    m_label = L"RAW preview: SHCreateMemStream failed";
                }
            } else {
                m_label = L"RAW preview failed";
            }
            delete bytes;

#ifdef FEATURE_RAW_VIEW
            if (m_pImage) {
                m_rawStatusLabel = L"RAW \u00B7 decoding\u2026";
                // Keep the mini spinner going while Stage 2 runs.
                m_spinnerActive = true;
                m_spinnerAngle  = 0;
                ::SetTimer(GetHwnd(), TIMER_SPINNER, 40, nullptr);
            }
#endif
            Invalidate(FALSE);
            return 0;
        }
#endif

        // ── Stage 2: quality decode arrived (raw JPEG bytes) ─────────────────
#ifdef FEATURE_RAW_VIEW
        case UWM_RAW_DECODED: {
            auto* bytes = reinterpret_cast<std::vector<unsigned char>*>(wparam);
            const int gen = static_cast<int>(lparam);

            if (gen != m_rawGeneration.load()) { delete bytes; return 0; }

            m_spinnerActive = false;
            ::KillTimer(GetHwnd(), TIMER_SPINNER);

            DropImageAndStream();
            if (bytes && !bytes->empty()) {
                IStream* pStream = ::SHCreateMemStream(
                    bytes->data(), static_cast<UINT>(bytes->size()));
                if (pStream) {
                    auto* img = Gdiplus::Image::FromStream(pStream);
                    pStream->Release();
                    if (img && img->GetLastStatus() == static_cast<Gdiplus::Status>(0)) {
                        m_pImage = img;
                        m_label.Empty();
                        m_rawStatusLabel.Empty();
                        SyncImageToolFromHostPicture();
                        m_imagePill.SetVisible(false);
                    } else {
                        delete img;
                        m_label = L"RAW decode failed";
                    }
                }
            }
            delete bytes;
            Invalidate(FALSE);
            return 0;
        }
#endif

        // ── Mouse wheel zoom ──────────────────────────────────────────────────
        case WM_MOUSEWHEEL: {
#if defined(FEATURE_D2D_GALLERY)
            {
                const int    wd    = GET_WHEEL_DELTA_WPARAM(wparam);
                CRect        rcCl  = GetClientRect();
                POINT        ptW   = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(ptW);
                const int dpiWheel = (int)::GetDpiForWindow(GetHwnd());
                const int hWheel = (std::max)(0, rcCl.Height() - Ui115(30, dpiWheel));
                if (m_filmstrip.OnMouseWheel(wd, ptW.x, ptW.y,
                                             rcCl.Width(), hWheel))
                    return 0;
            }
#endif
            if (!m_pImage) break;
            const int    delta  = GET_WHEEL_DELTA_WPARAM(wparam);
            const double factor = (delta > 0) ? 1.15 : (1.0 / 1.15);
            CRect        rc     = GetClientRect();
            POINT        pt     = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(pt);
            if (delta < 0) {
                pt.x = rc.Width() / 2;
                pt.y = rc.Height() / 2;
            }
            if (rc.PtInRect(pt)) {
                if (!m_imagePill.Visible())
                    Invalidate(FALSE);
                m_imagePill.SetVisible(true);
            }
            ApplyZoom(factor, pt.x, pt.y);
            return 0;
        }

        // ── Left button: overlay buttons first, then pan ──────────────────────
        case WM_LBUTTONDOWN: {
            const int sx = GET_X_LPARAM(lparam), sy = GET_Y_LPARAM(lparam);
#if defined(FEATURE_D2D_GALLERY)
            {
                CRect rcCl = GetClientRect();
                const int dpiClick = (int)::GetDpiForWindow(GetHwnd());
                const int hClick = (std::max)(0, rcCl.Height() - Ui115(30, dpiClick));
                if (m_filmstrip.OnLButtonDown(sx, sy, rcCl.Width(), hClick))
                    return 0;
            }
#endif
            const POINT pt{sx, sy};
            if (m_pImage && m_rcFileNameBar.PtInRect(pt) && !m_previewPath.empty()) {
                pmui::shell::open_path(GetHwnd(), m_previewPath);
                return 0;
            }
            if (m_pImage && m_imagePill.Visible()) {
                switch (m_imagePill.HitTest(pt)) {
                case CFileViewerImagePillToolbar::kZoomOut:
                    EnsureExplicitZoom();
                    {
                        CRect rc = GetClientRect();
                        ApplyZoom(1.0 / 1.15, rc.Width() / 2, rc.Height() / 2);
                    }
                    return 0;
                case CFileViewerImagePillToolbar::kZoomIn:
                    EnsureExplicitZoom();
                    {
                        CRect rc = GetClientRect();
                        ApplyZoom(1.15, rc.Width() / 2, rc.Height() / 2);
                    }
                    return 0;
                case CFileViewerImagePillToolbar::kFit:
                    ResetView();
                    return 0;
                case CFileViewerImagePillToolbar::kFull:
                    if (m_fullBtnTogglesFrameFs)
                        GetAncestor().PostMessage(UWM_TOGGLE_FULLSCREEN, 0, 0);
                    else
                        ApplyNativeZoomCentered();
                    return 0;
                case CFileViewerImagePillToolbar::kCrop: {
                    const UINT ciw = m_pImage->GetWidth(), cih = m_pImage->GetHeight();
                    if (ciw > 0 && cih > 0) {
                        EnsureImageTool();
                        m_imageActiveTool->OnHostImageSized(*this, static_cast<int>(ciw), static_cast<int>(cih));
                        if (m_imageActiveTool->IsActive())
                            m_imageActiveTool->SetActive(*this, false);
                        else {
                            m_imageActiveTool->ArmNewInteractiveSession(*this);
                            m_imageActiveTool->SetActive(*this, true);
                            if (GetHwnd())
                                ::SetFocus(GetHwnd());
                        }
                        Invalidate(FALSE);
                    }
                    return 0;
                }
                case CFileViewerImagePillToolbar::kPrevImg:
                    NavigateImageDelta(-1);
                    return 0;
                case CFileViewerImagePillToolbar::kNextImg:
                    NavigateImageDelta(1);
                    return 0;
                default:
                    break;
                }
            }
            if (m_pImage && m_imageActiveTool && m_imageActiveTool->IsActive()
                && m_imageActiveTool->HitChrome(*this, pt)) {
                OnImageToolFinishRequest();
                return 0;
            }
            if (!m_pImage) break;
            if (m_imageActiveTool && m_imageActiveTool->IsActive()) {
                if (m_imageActiveTool->OnMouseDown(*this, GetHwnd(), pt))
                    return 0;
                return 0;
            }
            EnsureExplicitZoom();
            ::SetCapture(GetHwnd());
            m_dragging = true;
            BeginImageInteractionPaint();
            CRect rc = GetClientRect();
            m_dragImgX = m_viewCx + (sx - rc.Width()  * 0.5) / m_zoom;
            m_dragImgY = m_viewCy + (sy - rc.Height() * 0.5) / m_zoom;
            m_dragScreenPt = pt;
            return 0;
        }

        case WM_MOUSEMOVE: {
            const int sx = GET_X_LPARAM(lparam), sy = GET_Y_LPARAM(lparam);
#if defined(FEATURE_D2D_GALLERY)
            if (m_filmstrip.HasContent()) {
                // Guard: only dispatch if cursor is at or near the dock strip.
                // This avoids animation/hover work while the mouse is over the
                // image area (e.g. during the magnify hover effect).
                CRect rcCl = GetClientRect();
                const int dpiMouse = (int)::GetDpiForWindow(GetHwnd());
                const int barHMouse = Ui115(30, dpiMouse);
                const int stripHostH = (std::max)(0, rcCl.Height() - barHMouse);
                const int stripTop = stripHostH - filmstrip::FilmStripWidget::DockHeightPx();
                if (sy >= stripTop - 4 && sy < stripHostH) { // 4px hysteresis
                    m_filmstrip.OnMouseMove(sx, sy, rcCl.Width(), stripHostH);
                    return 0; // filmstrip owns this band; skip image hover/pill work
                } else {
                    m_filmstrip.OnMouseMove(-1, -1, rcCl.Width(), stripHostH); // clears hover
                }
            }
#endif
            if (!m_trackingMouse) {
                TRACKMOUSEEVENT tme{sizeof(tme)};
                tme.dwFlags = TME_LEAVE; tme.hwndTrack = GetHwnd();
                ::TrackMouseEvent(&tme);
                m_trackingMouse = true;
            }
            const POINT ptMm{sx, sy};
            CRect       rcClient = GetClientRect();
            if (m_pImage && rcClient.PtInRect(ptMm)) {
                if (!m_imagePill.Visible())
                    Invalidate(FALSE);
                m_imagePill.SetVisible(true);
            }
            if (m_imageActiveTool && m_imageActiveTool->IsActive() && m_imageActiveTool->DraggingMouse()
                && m_pImage) {
                m_imageActiveTool->OnMouseMove(*this, GetHwnd(), ptMm);
            } else if (m_dragging && m_pImage) {
                CRect rc = GetClientRect();
                m_viewCx = m_dragImgX - (sx - rc.Width()  * 0.5) / m_zoom;
                m_viewCy = m_dragImgY - (sy - rc.Height() * 0.5) / m_zoom;
                BeginImageInteractionPaint();
                Invalidate(FALSE);
            } else if (m_pImage && !(m_imageActiveTool && m_imageActiveTool->DraggingMouse())) {
                const POINT pt{sx, sy};
                const int   prev = m_hoveredBtn;
                if (m_imageActiveTool && m_imageActiveTool->IsActive() && m_imageActiveTool->HitChrome(*this, pt))
                    m_hoveredBtn = BTN_CROP_OK;
                else if (m_rcFileNameBar.PtInRect(pt))
                    m_hoveredBtn = BTN_FILENAME;
                else {
                    const CFileViewerImagePillToolbar::Hit ph = m_imagePill.HitTest(pt);
                    if (ph == CFileViewerImagePillToolbar::kNone)
                        m_hoveredBtn = BTN_NONE;
                    else if (ph == CFileViewerImagePillToolbar::kPrevImg)
                        m_hoveredBtn = BTN_IMG_PREV;
                    else if (ph == CFileViewerImagePillToolbar::kNextImg)
                        m_hoveredBtn = BTN_IMG_NEXT;
                    else
                        m_hoveredBtn = static_cast<OverlayBtn>(static_cast<int>(ph));
                }
                if (m_hoveredBtn != prev)
                    Invalidate(FALSE);
            }
            break;
        }

        case WM_MOUSELEAVE:
            m_trackingMouse = false;
            if (m_hoveredBtn != BTN_NONE)
                m_hoveredBtn = BTN_NONE;
            if (m_pImage) {
                m_imagePill.SetVisible(false);
                Invalidate(FALSE);
            }
#if defined(FEATURE_D2D_GALLERY)
            m_filmstrip.OnMouseLeave();
#endif
            return 0;

        case WM_KILLFOCUS:
            if (m_hoveredBtn != BTN_NONE)
                m_hoveredBtn = BTN_NONE;
            if (m_pImage && m_imagePill.Visible()) {
                m_imagePill.SetVisible(false);
                Invalidate(FALSE);
            }
            break;

        case WM_LBUTTONUP: {
            const int sx = GET_X_LPARAM(lparam), sy = GET_Y_LPARAM(lparam);
            const POINT ptUp{sx, sy};
            if (m_pImage && m_imageActiveTool && m_imageActiveTool->IsActive())
                m_imageActiveTool->OnMouseUp(*this, GetHwnd(), ptUp);
            if (m_dragging) {
                m_dragging = false;
                ::ReleaseCapture();
                EndImageInteractionPaint(true);
            }
            return 0;
        }

        case WM_CAPTURECHANGED:
            if (m_dragging) {
                m_dragging = false;
                EndImageInteractionPaint(true);
            }
            if (m_imageActiveTool)
                m_imageActiveTool->OnCaptureLost(*this, GetHwnd());
            return 0;

        case WM_LBUTTONDBLCLK:
            m_dragging = false;
            EndImageInteractionPaint(false);
            ::ReleaseCapture();
            ResetView();
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                POINT pt; ::GetCursorPos(&pt); ScreenToClient(pt);
                if (m_pImage && m_imagePill.Visible()
                    && m_imagePill.HitTest(pt) != CFileViewerImagePillToolbar::kNone)
                    ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
                else if (m_pImage && m_imageActiveTool && m_imageActiveTool->IsActive()
                    && m_imageActiveTool->HitChrome(*this, pt))
                    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
                else if (m_pImage && m_rcFileNameBar.PtInRect(pt))
                    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
                else if (m_pImage && m_imageActiveTool && m_imageActiveTool->IsActive()) {
                    if (LPCTSTR cur = m_imageActiveTool->SuggestSetCursor(*this, pt))
                        ::SetCursor(::LoadCursor(nullptr, cur));
                    else
                        ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
                } else if (m_pImage)
                    ::SetCursor(::LoadCursor(nullptr, m_dragging ? IDC_SIZEALL : IDC_HAND));
                else
                    // Must set a cursor: returning TRUE without SetCursor leaves whatever
                    // the previous window drew (e.g. dock splitter resize) stuck here.
                    ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;

        // ── Touch gestures ────────────────────────────────────────────────────
        case WM_GESTURE: {
            if (!m_pImage) break;
            GESTUREINFO gi{}; gi.cbSize = sizeof(gi);
            if (!::GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lparam), &gi)) break;
            switch (gi.dwID) {
            case GID_ZOOM: {
                DWORD cur = static_cast<DWORD>(gi.ullArguments & 0xFFFFFFFF);
                if (gi.dwFlags & GF_BEGIN) { m_gestureRefDist = cur; }
                else if (m_gestureRefDist > 0 && cur > 0) {
                    POINT c = {gi.ptsLocation.x, gi.ptsLocation.y};
                    ScreenToClient(c);
                    ApplyZoom((double)cur / m_gestureRefDist, c.x, c.y);
                    m_gestureRefDist = cur;
                }
                break;
            }
            case GID_PAN: {
                if (m_imageActiveTool && m_imageActiveTool->IsActive())
                    break;
                POINT c = {gi.ptsLocation.x, gi.ptsLocation.y};
                ScreenToClient(c);
                if (gi.dwFlags & GF_BEGIN) { EnsureExplicitZoom(); m_gesturePrevPt = c; }
                else if (m_zoom > 0) {
                    m_viewCx -= (c.x - m_gesturePrevPt.x) / m_zoom;
                    m_viewCy -= (c.y - m_gesturePrevPt.y) / m_zoom;
                    m_gesturePrevPt = c;
                    BeginImageInteractionPaint();
                    Invalidate(FALSE);
                }
                break;
            }
            }
            ::CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam));
            return 0;
        }

        } // switch
        return WndProcDefault(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s; s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}
