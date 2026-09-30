#ifndef PM_UI_FILEVIEWER_H
#define PM_UI_FILEVIEWER_H
//
// Canonical centre file preview for pm-image: image (GDI+), XBlox, plain text, markdown
// (embedded browser or WebView2), PDF / 3D / spreadsheet via viewer-next. Core
// view logic is in FileViewer.cpp; loaders are split across FileViewer_Text.cpp,
// FileViewer_Markdown.cpp, FileViewer_PDF.cpp, FileViewer_Video.cpp, FileViewer_3D.cpp, and
// FileViewer_Images.cpp (RAW pipeline, image load, spinner).
//
//   * CMainFrame hosts the primary CFileViewer as its client view (see FileViewer()).
//
#include "stdafx.h"
#include "features.h"
#include "helpers/dock_helpers.h"
#include "FileViewerImageToolInterface.h"
#include "FileViewerImagePillToolbar.h"
#include "PreviewCoordinator.h"   // pmui::PreviewSource, PreviewStatus
#include "Resource.h"
#include <shellapi.h>
#include <gdiplus.h>
#include <atomic>
#include <memory>
#include <thread>

class CWebViewManager;

#if defined(FEATURE_D2D_GALLERY)
#include "gallery/filmstrip/FilmStrip.h"
#endif

/// Scrollable drop target + file preview (images, text, markdown, hosted documents).
class CFileViewer : public CScrollView
{
public:
    /// Bitmask for same-folder / multi-select centre “browse” ordering (prev/next list).
    /// Matches @c OpenFile priority: image, markdown, video, PDF, spreadsheet, 3D, then plain text.
    /// Default @c BROWSE_MASK_ALL; CLI/UI may narrow (e.g. pictures-only) later.
    enum BROWSE_MASK : unsigned {
        BROWSE_MASK_NONE        = 0,
        BROWSE_MASK_PICTURES    = 1u << 0,
        BROWSE_MASK_MARKDOWN    = 1u << 1,
        BROWSE_MASK_VIDEO       = 1u << 2,
        BROWSE_MASK_PDF         = 1u << 3,
        BROWSE_MASK_SPREADSHEET = 1u << 4,
        BROWSE_MASK_THREE_D     = 1u << 5,
        BROWSE_MASK_TEXT_CODE   = 1u << 6,
        BROWSE_MASK_ALL         = BROWSE_MASK_PICTURES | BROWSE_MASK_MARKDOWN | BROWSE_MASK_VIDEO
                           | BROWSE_MASK_PDF | BROWSE_MASK_SPREADSHEET | BROWSE_MASK_THREE_D
                           | BROWSE_MASK_TEXT_CODE,
    };

    CFileViewer();
    virtual ~CFileViewer() override;

    void SetCentreBrowseMask(BROWSE_MASK mask) noexcept { m_centreBrowseMask = mask; }
    BROWSE_MASK CentreBrowseMask() const noexcept { return m_centreBrowseMask; }

    /// §8 unified entry: pick the best previewable file from @p paths
    /// (image > markdown > video > PDF > spreadsheet > 3D > html > text), load it, and
    /// return the outcome.  Empty @p paths clears the preview.
    /// @p source is informational (logging / future per-source policy).
    pmui::PreviewStatus OpenFile(const std::vector<std::wstring>& paths,
                                 pmui::PreviewSource source);
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    void SetXbloxContext(const std::vector<std::wstring>& selection, const std::wstring& folder);
#endif

    bool LoadPicture(LPCWSTR path);
    void ClearPicture();

    /// Image preview: base file name for the bottom bar only (click opens in default app).
    void SetFileInfoFromPath(LPCWSTR path);
    void SetFileInfoText(LPCWSTR text);
    void ClearFileInfo();

    /// Load @p path as plain text. When `FEATURE_VIEWER_WEB` is on and the markdown
    /// preview engine is `webview2`, uses `CViewerWebPanel` + `apps/viewer-next`
    /// (hosted file on the markdown-assets vhost, same pattern as PDF/spreadsheet).
    /// Otherwise loads into the embedded read-only EDIT control.
    /// Refuses files whose first 4 KB contain a NUL byte (binary detector).
    /// Caps preview to the per-extension limit (`viewer_document_max_bytes_for_ext`);
    /// the native EDIT path truncates with a notice line when larger.
    bool LoadText(LPCWSTR path);
    void ClearText();

    /// Render @p path as Markdown (md4c → HTML). When `FEATURE_VIEWER_WEB` is on and
    /// `markdown_preview_engine()` is `webview2`, hosts `apps/viewer-next` via
    /// `CViewerWebPanel` + iframe preview; otherwise uses legacy `CWebBrowser` (MSHTML).
    /// Falls back to `LoadText()` if the host cannot be created.
    bool LoadMarkdown(LPCWSTR path);
    /// STL / OBJ / STEP / STP / DXF via WebView2 + apps/viewer-next (`FEATURE_VIEWER_WEB`).
    bool LoadThreeD(LPCWSTR path);
    /// OpenSCAD source preview: compile `.scad` with `openscad.exe` to temp STL, then render via ThreeD viewer.
    bool LoadOpenSCAD(LPCWSTR path);
    /// Recompile currently previewed OpenSCAD file with user-provided CLI defines (e.g. `-Dfoo=1 -Dbar=2`).
    bool ReloadOpenSCADWithDefines(const std::wstring& defines_cli);
    /// PDF via WebView2 + react-pdf (`FEATURE_VIEWER_WEB`).
    bool LoadPdf(LPCWSTR path);
    /// CSV / XLS / XLSX via WebView2 + xlsx (`FEATURE_VIEWER_WEB`).
    bool LoadSpreadsheet(LPCWSTR path);
    /// Common video containers via WebView2 + viewer-next (native `<video controls>`; `FEATURE_VIEWER_WEB`).
    bool LoadVideo(LPCWSTR path);
    /// SVG / browser-compatible raster image via WebView2 + viewer-next.
    bool LoadBrowserImage(LPCWSTR path);
    /// Local HTML file rendered in an iframe inside viewer-next (`FEATURE_VIEWER_WEB`).
    /// The virtual-host folder mapping gives the iframe a base URL equal to the file's
    /// parent directory so relative CSS / images / scripts resolve correctly.
    /// Falls back to `LoadText()` when WebView2 is unavailable.
    bool LoadHtml(LPCWSTR path);
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    /// XBlox block-chain JSON (`*.xblox`) hosted by `dist/shared/xblox.html`.
    bool LoadXblox(LPCWSTR path);
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    /// Dedicated centre-surface home app (`dist/shared/home.html`) hosted directly in WebView2.
    bool LoadHome();
#endif
    /// Agent session flow visualization for .agent.json files via WebView2 + @xyflow/react
    /// (`FEATURE_VIEWER_WEB`). JSON content is passed inline to viewer-next.
    /// Falls back to `LoadText()` when WebView2 is unavailable.
    bool LoadAgentFlow(LPCWSTR path);
    void ClearMarkdown();
#if defined(FEATURE_VIEWER_WEB) || defined(FEATURE_BROWSER) || (defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER))
    /// Clears WebView2 `SetVirtualHostNameToFolderMapping` for the assets host. Call when fully
    /// abandoning web preview (image mode, empty selection, delete-before-Shell-op); not on every
    /// `ClearMarkdown` — `LoadText` / `LoadMarkdown` remap in the same navigation otherwise.
#if defined(FEATURE_VIEWER_WEB)
    void ReleaseViewerWebFolderMapping();
#endif

    /// Register live WebView2 preview children (`preview` / `home`) on the shared CWebView bus.
    void SetBusManager(CWebViewManager* manager);
#endif

    /// Re-sync EDIT / WebView2 + scroll-surface colours after App Settings or
    /// system theme changes (the frame preview is not a dock, so it does not
    /// get `apply_window_theme_recursive` from `CMainFrame::ApplyAppearance`).
    void RefreshThemeChrome();

    /// When true (default), the overlay "Full" button posts @c UWM_TOGGLE_FULLSCREEN
    /// on the root frame. When false (e.g. chat fullscreen shell), "Full" jumps to
    /// 100% pixel zoom centered on the image instead of toggling the main window.
    void SetFullButtonTogglesFrameFullscreen(bool enable) noexcept;
    bool FullButtonTogglesFrameFullscreen() const noexcept;

    /// Centre preview is a loaded bitmap (not text / markdown / hosted viewers).
    bool HasLoadedImagePreview() const noexcept
    {
        return m_pImage != nullptr && !m_textMode && !m_mdMode;
    }
    /// Chrome hover id for `IFileViewerImageTool::PaintChrome` (in-image confirm control).
    /// Kept in sync with `OverlayBtn::BTN_CROP_OK` while the pill uses the same hover union.
    static constexpr int kImageToolChromeBtnConfirm = 6;

    /// True while an in-image tool session is active (overlay visible).
    bool ImageToolSessionActive() const noexcept
    {
        return m_imageActiveTool && m_imageActiveTool->IsActive();
    }
    /// Main window may route Enter here when the active tool wants to commit in place.
    bool ImageToolConsumesEnterKey() const noexcept
    {
        return m_imageActiveTool && m_imageActiveTool->IsActive() && m_imageActiveTool->WantsVkReturnWhileActive();
    }
    /// Overwrite save allowed for the current preview (delegates to the active tool when present).
    bool CanSaveImagePreviewOverwrite() const noexcept;
    void OnImageToolFinishRequest();
    void OnImageToolFinish(PmImageToolKind finishedTool, Gdiplus::Bitmap* newImageOrNull);
    bool SaveImagePreviewOverwrite(CString& errOut);
    bool SaveImagePreviewSaveAs(HWND owner, CString& errOut);

    Gdiplus::Image*       PreviewImage() const noexcept { return m_pImage; }
    const std::wstring&   PreviewPathW() const noexcept { return m_previewPath; }

#if defined(FEATURE_VIEWER_WEB) && FEATURE_VIEWER_WEB
    /// Returns the live `CViewerWebPanel` pointer, or nullptr if not yet created.
    class CViewerWebPanel* ViewerWebPanel() const noexcept
    {
        return static_cast<class CViewerWebPanel*>(m_viewerMdPanel);
    }
#endif

    /// When prev/next neighbours exist, handles Left/Right/Home/End (no Ctrl/Alt).
    /// Used from @c CMainFrame::TryProcessGlobalHotkeys when focus is not on the viewer.
    bool TryConsumeImageNavKeys(WPARAM vk, bool ctrlDown, bool altDown);
    /// Mouse back/forward: @c XBUTTON1 → previous, @c XBUTTON2 → next (same as browser).
    bool TryConsumeImageNavXButtons(unsigned xbuttonFromGetXButtonWparam) noexcept;

protected:
    void    PreCreate(CREATESTRUCT& cs) override;
    virtual int  OnCreate(CREATESTRUCT& cs) override;
    virtual void OnDraw(CDC& dc) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    struct OpenScadCompileResult {
        unsigned token = 0;
        std::wstring sourcePathW;
        std::wstring previewPathW;
        std::string sourceUtf8;
        std::string errUtf8;
        std::uintmax_t previewSize = 0;
        bool success = false;
    };

    CFileViewer(const CFileViewer&) = delete;
    CFileViewer& operator=(const CFileViewer&) = delete;

    LRESULT OnDropFiles(WPARAM wparam);

    void ApplyNativeZoomCentered();
    void ApplyDefaultImageZoom();

    void ClearImageNav() noexcept;
    void RebuildImageNavFromOpen(const std::vector<std::wstring>& paths, const std::wstring& loadedPath);
    void RefreshImageNavFromParentOf(const std::wstring& loadedPath);
    static bool PathMatchesCentreBrowseMask(const std::wstring& path, BROWSE_MASK mask) noexcept;
    bool ImageNavHasNeighbors() const noexcept;
    void NavigateImageDelta(int delta);
    void NavigateImageFirstLast(bool first);
    void SyncPreviewCoordinatorAfterNav(const std::wstring& path);

    bool m_fullBtnTogglesFrameFs = true;
    /// Last generated OpenSCAD temp STL (`LoadOpenSCAD`); deleted on next preview switch / teardown.
    std::wstring m_openScadTempStlPath;
    /// Source `.scad` currently bound to OpenSCAD preview (used for recompile requests from web UI).
    std::wstring m_openScadSourcePath;
    /// Extra OpenSCAD CLI defines from web UI (`-D...` tokens).
    std::wstring m_openScadDefinesCli;
    /// Last raw OpenSCAD source pushed to web UI.
    std::string m_openScadSourceUtf8;
    std::atomic<bool> m_openScadCompileInFlight{false};
    std::atomic<unsigned> m_openScadCompileToken{0};

    // ── Text preview (read-only multi-line EDIT child) ─────────────────────────
    HWND   m_hTextEdit = nullptr;
    HFONT  m_textFont  = nullptr;
    bool   m_textMode  = false;     // hides image rendering when true
    void   EnsureTextEdit();        // lazy-create the EDIT child + monospace font
    void   LayoutTextEdit();        // size the EDIT to the client rect
    static constexpr std::size_t kTextLimitBytes = 5 * 1024 * 1024;  // 5 MB cap

    // ── Markdown preview (WebView2 `CViewerWebPanel` or legacy `CWebBrowser`) ─
    // Heap-allocated to avoid pulling WebView2 / wxx headers into this header.
    void* m_browser       = nullptr;   // CWebBrowser* (MSHTML) when engine is webbrowser
    void* m_viewerMdPanel = nullptr;   // CViewerWebPanel* when FEATURE_VIEWER_WEB + webview2
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    void* m_xbloxWebView  = nullptr;   // CBlockView* direct host for dist/shared/xblox.html
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    void* m_homeWebView   = nullptr;   // CWebView* direct host for dist/shared/home.html
#endif
    CWebViewManager* m_busManager = nullptr; // Non-owning; forwarded to centre WebView children when present.
    bool  m_mdMode        = false;
    std::wstring m_mdTempHtml;         // legacy: temp .html for Navigate(); cleaned on switch
    void  EnsureBrowser();
    void  EnsureViewerMdPanel();
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    void  EnsureXbloxWebView();
    void  LayoutXbloxWebView();
    void  DestroyXbloxWebView();
#endif
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    void  EnsureHomeWebView();
    void  LayoutHomeWebView();
    void  DestroyHomeWebView();
#endif
    void  LayoutBrowser();
    void  LayoutViewerMdPanel();
    void  DestroyViewerMdPanel();
    void  ApplyPreviewChildTheming();

    // ── Image data ────────────────────────────────────────────────────────────
    // m_imgStream is the SHCreateMemStream-backed IStream that owns the
    // file bytes when LoadPicture used the in-memory path (the standard
    // GDI+ path; the RAW pipeline decodes to a heap Bitmap and leaves
    // m_imgStream null). We keep an explicit reference for the lifetime
    // of m_pImage because GDI+ lazy-decodes from the stream during paint.
    // Released by DropImageAndStream() and the destructor; never by the
    // RAW callbacks (they only swap m_pImage).
    Gdiplus::Image* m_pImage    = nullptr;
    IUnknown*       m_imgStream = nullptr;   // really IStream* — keep header-light
    ULONG_PTR       m_gdipToken = 0;
    CBrush          m_bgBrush;
    CString         m_label;
    CString         m_fileInfoText;
    std::wstring    m_previewPath;

    /// In-viewer prev/next: ordered previewable paths (multi-select and/or same-folder scan) + index.
    std::vector<std::wstring> m_imageNavPaths;
    int                       m_imageNavIndex = -1;
    BROWSE_MASK               m_centreBrowseMask = BROWSE_MASK_ALL;

    /// Releases m_pImage *and* m_imgStream (in order). Use everywhere the
    /// previous pattern was `delete m_pImage; m_pImage = nullptr;` so the
    /// memory-stream reference doesn't outlive the image and silently leak.
    void DropImageAndStream();
    void SyncImageToolFromHostPicture();
    /// Ensures `m_imageActiveTool` can answer save / extract queries (default: crop implementation).
    void EnsureImageTool();

    // ── View transform ────────────────────────────────────────────────────────
    double m_zoom   = 0.0;   // 0 = auto-fit
    double m_viewCx = 0.0;
    double m_viewCy = 0.0;
    double GetFitZoom()    const;
    void   ResetView();
    void   EnsureExplicitZoom();
    void   ApplyZoom(double factor, int sx, int sy);
    void   BeginImageInteractionPaint();
    void   EndImageInteractionPaint(bool invalidate);

    // ── Mouse pan ─────────────────────────────────────────────────────────────
    bool   m_dragging    = false;
    double m_dragImgX    = 0.0;
    double m_dragImgY    = 0.0;
    POINT  m_dragScreenPt{};
    bool   m_fastImageInteractionPaint = false;

    // ── Touch / gesture ───────────────────────────────────────────────────────
    DWORD  m_gestureRefDist = 0;
    POINT  m_gesturePrevPt  = {};

    // ── Overlay: top pill (`CFileViewerImagePillToolbar`); bottom bar (file name + size) ──
    enum OverlayBtn {
        BTN_NONE      = -1,
        BTN_ZOOM_OUT  = 0,
        BTN_FIT       = 1,
        BTN_FULL      = 2,
        BTN_CROP      = 3,
        BTN_ZOOM_IN   = 4,
        BTN_FILENAME  = 5,
        BTN_CROP_OK   = 6,
        BTN_IMG_PREV  = 7,
        BTN_IMG_NEXT  = 8,
    };
    CRect m_rcFileNameBar{};
    int   m_hoveredBtn    = BTN_NONE;
    bool  m_trackingMouse = false;
    double m_fitZoomAtLayout = 0;   // GetFitZoom() when m_zoom last set; WM_SIZE rescales m_zoom
    CFileViewerImagePillToolbar           m_imagePill{};
    std::unique_ptr<IFileViewerImageTool> m_imageActiveTool{};

    // ── D2D Gallery: filmstrip dock ───────────────────────────────────────────
#if defined(FEATURE_D2D_GALLERY)
    filmstrip::FilmStripWidget m_filmstrip{};

    /// Client rect minus the filmstrip dock and the filename bar.
    /// Layout (bottom → top): [filename bar] [filmstrip] [image area].
    /// Used by zoom/pan math so the image fits the visible canvas only.
    CRect GetImageAreaRect() const
    {
        CRect rc = GetClientRect();
        // Filename bar height (DPI-scaled 30 px base, same formula as Ui115(30, dpi)).
        const int dpi  = GetHwnd() ? (int)::GetDpiForWindow(GetHwnd()) : 96;
        const int barH = ::MulDiv(::MulDiv(30, 115, 100), dpi, 96);
#if defined(FEATURE_D2D_GALLERY)
        if (m_filmstrip.HasContent())
            rc.bottom -= filmstrip::FilmStripWidget::DockHeightPx();
#endif
        if (rc.Height() > barH) rc.bottom -= barH;
        return rc;
    }
#else
    CRect GetImageAreaRect() const { return GetClientRect(); }
#endif

    // ── Loading spinner ───────────────────────────────────────────────────────
    // Shown while Stage 1 (fast RAW preview) is decoding in the background.
    bool  m_spinnerActive = false;
    int   m_spinnerAngle  = 0;         // current tip angle, degrees
    static constexpr UINT TIMER_SPINNER   = 1;
    static constexpr UINT TIMER_MD_CHROME = 2;   // deferred flatten after IWebBrowser2 navigates
    static constexpr UINT TIMER_IMAGE_INTERACTION_SETTLE = 3;
    void DrawSpinner(HDC hdc, int cx, int cy, int radius) const;   // FileViewer_Images.cpp
    void ClearOpenSCADTempPreviewFile() noexcept;
    void PushOpenSCADPreviewToWeb(const std::wstring& sourcePathW,
                                  const std::wstring& previewPathW,
                                  std::uintmax_t previewSize,
                                  std::string_view err_utf8);
    LRESULT OnOpenSCADCompileDone(WPARAM wparam, LPARAM lparam);

    // ── RAW pipeline ──────────────────────────────────────────────────────────
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    // Both stages run in parallel background threads. Workers do *only*
    // libvips work and produce raw JPEG bytes; the UI thread (WndProc on
    // UWM_RAW_PREVIEW_READY / UWM_RAW_DECODED) wraps the bytes in a GDI+
    // Image::FromStream — no GDI+ call ever happens off the UI thread, so
    // detached workers can't race with Gdiplus::GdiplusShutdown.
    //
    // Cancellation: each launched worker captures a shared_ptr<atomic<bool>>
    // that CancelRawThreads(false) flips to true before detaching the
    // thread. The worker checks the flag at the start, after vips returns,
    // and immediately before PostMessage. We can't interrupt
    // vips_image_new_from_file mid-flight (no public cancellation handle in
    // libvips for raw decode), but we *can* skip the post-decode work and
    // discard the bytes — no GDI+ allocation, no UI-thread message.
    //
    // Generation counters are kept as a defence-in-depth: if a worker races
    // past its cancel check and PostMessages a stale buffer, the WndProc
    // handler still drops it.
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    static bool DecodeVipsThumbnailToJpeg(const std::string& utf8Path,
                                          int                targetPx,
                                          std::vector<unsigned char>& out_jpeg);
    static bool DecodeVipsFullToJpeg(const std::string&         utf8Path,
                                     std::vector<unsigned char>& out_jpeg);

    /// Cancel any in-flight RAW workers. @p wait_for_join joins the threads
    /// (correct for destruction — guarantees workers are gone before GDI+
    /// shutdown). Default is detach (correct for navigation — never stalls
    /// the UI for the duration of a libvips full-decode).
    void   CancelRawThreads(bool wait_for_join = false);

    /// If @p path has a RAW extension, starts background decode and returns true
    /// (caller should return immediately). Otherwise returns false.
    bool   TryLoadPictureRawPipeline(LPCWSTR path);

    CString              m_rawStatusLabel;       // shown until quality view arrives

    // Stage 1 — fast preview (WIC embedded preview, then vips_thumbnail fallback)
    std::thread          m_rawFastThread;
    std::atomic<int>     m_rawFastGen{0};
    CancelFlag           m_rawFastCancel;

    // Stage 2 — quality decode (vips_image_new_from_file, full Bayer)
#ifdef FEATURE_RAW_VIEW
    std::thread          m_rawThread;
    std::atomic<int>     m_rawGeneration{0};
    CancelFlag           m_rawCancel;
#endif
#endif
};

#endif // PM_UI_FILEVIEWER_H
