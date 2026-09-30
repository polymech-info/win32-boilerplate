#pragma once
// FilmStrip.h — self-contained D2D filmstrip dock widget (FEATURE_D2D_GALLERY).
// Adapted from apps/win32-mini/filmstrip/:
//   • All state owned by FilmStripWidget::Impl — no module-level globals.
//   • No UpdateLayeredWindow coordinate corrections (regular child HWND).
//   • No preview-image area: CFileViewer owns the main display.
//   • SelectionChangedCallback fires immediately on LButtonDown.
//
// Usage (inside CFileViewer):
//
//   m_filmstrip.Initialize(GetHwnd(), paths);
//   m_filmstrip.OnSelectionChanged([this](size_t, const std::wstring& p){
//       LoadPicture(p.c_str()); });
//
//   // In OnDraw (after painting the image and bottom bar):
//   m_filmstrip.PaintOverGdi(hMem, w, h);
//
//   // In WndProc:
//   case WM_MOUSEMOVE:  m_filmstrip.OnMouseMove(x, y, w, h); break;
//   case WM_MOUSEWHEEL: if (m_filmstrip.OnMouseWheel(d,x,y,w,h)) return 0; break;
//   case WM_LBUTTONDOWN:if (m_filmstrip.OnLButtonDown(x,y,w,h)) return 0; break;
//   case WM_MOUSELEAVE: m_filmstrip.OnMouseLeave(); break;
//   case WM_TIMER:      m_filmstrip.OnTimer(wParam); break;

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace filmstrip {

// ── Layout constants (tunable) ───────────────────────────────────────────────

namespace options {
enum class AnimationPreset {
    ScaleVerticalCenter, // Current/legacy: scaled thumb grows around its centre.
    ScaleUp,             // OS X dock style: bottom edge fixed, thumb grows upward.
};

enum class GalleryGlob {
    IMAGES,
};

constexpr AnimationPreset animationPreset = AnimationPreset::ScaleUp;
constexpr GalleryGlob galleryGlob = GalleryGlob::IMAGES;
constexpr bool allowKeyboardNav = true;
// Initial selected thumb is a compile-time behavior preset: select and center it
// during the strip's first layout only. Hover magnification still requires pointer input.
constexpr bool initialSelected = true;

namespace layout {
    constexpr int thumbSize   = 64;
    constexpr int thumbGap    = 20;
    constexpr int padLeft     = 10;
    constexpr int padRight    = 10;
    namespace padding { constexpr int top = 5; constexpr int bottom = 6; }
    namespace visual  {
        constexpr int shadowV        = 4;
        constexpr int halo           = 8;
        constexpr int overflowTop    = shadowV / 2 + halo / 2;
        constexpr int overflowBottom = shadowV / 2 + halo / 2;
    }
} // namespace layout
} // namespace options

// Computed dock height (pixels reserved at the bottom of the client rect).
int DockHeightPx() noexcept;
// Full vertical paint extent including upward overlay for ScaleUp.
int PaintExtentPx() noexcept;

// ── FilmStripWidget ──────────────────────────────────────────────────────────

class FilmStripWidget {
public:
    FilmStripWidget();
    ~FilmStripWidget();

    FilmStripWidget(const FilmStripWidget&)            = delete;
    FilmStripWidget& operator=(const FilmStripWidget&) = delete;
    FilmStripWidget(FilmStripWidget&&)                 = delete;
    FilmStripWidget& operator=(FilmStripWidget&&)      = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    // Initialize with the host HWND and an initial (possibly empty) path list.
    // COM must already be initialized (COINIT_APARTMENTTHREADED).
    bool Initialize(HWND host, const std::vector<std::wstring>& paths);

    // Replace the path list; preserves selection index when still valid.
    void SetPaths(const std::vector<std::wstring>& paths);

    // Set the selected index without firing SelectionChangedCallback.
    // Use this when the main view navigated programmatically (e.g. Next/Prev)
    // to keep the filmstrip highlight in sync without triggering a reload.
    void SetSelectionSilent(size_t index);

    // Initial host sync: when the row has not been laid out yet, focus/center
    // the selected thumb. After first layout, behaves like SetSelectionSilent.
    void SetSelectionInitial(size_t index);

    // Keyboard/programmatic navigation sync: update selection and move it toward
    // the center of the strip. Does not fire SelectionChangedCallback.
    void SetSelectionCentered(size_t index);

    // Release all D2D/WIC resources. Safe to call multiple times.
    void Shutdown();

    // ── Queries ───────────────────────────────────────────────────────────

    bool   HasContent()   const noexcept;
    size_t GetSelection() const noexcept;
    static int DockHeightPx() noexcept;
    static int PaintExtentPx() noexcept;

    // ── Rendering ─────────────────────────────────────────────────────────

    // Paint the dock strip at the bottom of the given HDC (client coords).
    // Pass the host's current dark-mode flag so the strip matches the theme.
    // Call after all other GDI+ painting; the strip is kDockH px tall.
    void PaintOverGdi(HDC hdc, int clientW, int clientH, bool dark = true);

    // ── Input forwarding ──────────────────────────────────────────────────

    // Returns true if the event was consumed by the strip.
    bool OnMouseWheel(int delta, int clientX, int clientY,
                      int clientW, int clientH);
    bool OnLButtonDown(int clientX, int clientY,
                       int clientW, int clientH);
    void OnMouseMove(int clientX, int clientY, int clientW, int clientH);
    void OnMouseLeave();
    // Returns true while animation is active (caller may schedule extra repaints).
    bool OnTimer(WPARAM timerId);

    // ── Async thumbnail upload ─────────────────────────────────────────────

    // WM_APP message posted to host when a background WIC decode completes.
    // The host should call FlushPendingUploads() from its WndProc handler.
    static constexpr UINT WM_THUMB_READY = WM_APP + 0x71;

    // Upload any bitmaps decoded by background threads to the D2D render target.
    // Call from WndProc when WM_THUMB_READY arrives.
    void FlushPendingUploads();

    // ── Callbacks ─────────────────────────────────────────────────────────

    // Fired when the user clicks a different thumbnail.
    using SelectionChangedCallback =
        std::function<void(size_t index, const std::wstring& path)>;
    void OnSelectionChanged(SelectionChangedCallback cb);

    // Fired when SetPaths changes the list.
    using PathsChangedCallback = std::function<void(size_t count)>;
    void OnPathsChanged(PathsChangedCallback cb);

private:
    class Impl;
    Impl* impl_;
};

} // namespace filmstrip
