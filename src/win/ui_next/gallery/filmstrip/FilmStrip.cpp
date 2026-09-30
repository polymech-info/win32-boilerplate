// FilmStrip.cpp — D2D filmstrip dock for FEATURE_D2D_GALLERY.
// Adapted from apps/win32-mini/filmstrip/:
//   • All state lives in FilmStripWidget::Impl (no module-level globals).
//   • Input uses client coordinates directly (no ULW/layered-window mapping).
//   • Only the dock strip is painted; no preview-image area above the strip.
//   • SelectionChangedCallback fires on LButtonDown thumb click.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CreateSolidBrush
#undef DrawText

#include <d2d1.h>
#include <d2d1helper.h>
#undef CreateSolidBrush
#undef DrawText

#include <dwrite.h>
#include <wincodec.h>
#include <objbase.h>

#include "FilmStrip.h"
#include "FilmStripThumb.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace filmstrip {

// ── Layout constants ─────────────────────────────────────────────────────────

constexpr int kThumbPx   = options::layout::thumbSize;
constexpr int kThumbGap  = options::layout::thumbGap;
constexpr int kPadLeft   = options::layout::padLeft;
constexpr int kPadRight  = options::layout::padRight;

constexpr bool kPresetScaleUp =
    options::animationPreset == options::AnimationPreset::ScaleUp;
constexpr float kFocusMaxBoost              = 1.85f;
constexpr float kMaxScale                   = 1.f + kFocusMaxBoost;
constexpr float kFocusInfluenceRadiusCells  = 1.55f;
constexpr float kFocusSpringAccel           = 0.44f;
constexpr float kFocusSpringDrag            = 0.14f;
constexpr float kFocusLerpStrength          = 0.26f;
constexpr int   kThumbRowOffsetY            = 6;

constexpr int kMaxThumbPx = static_cast<int>(static_cast<float>(kThumbPx) * kMaxScale);
constexpr int kContentTop = options::layout::padding::top + options::layout::visual::overflowTop;
constexpr float kScaledThumbCenterY =
    static_cast<float>(kContentTop) + static_cast<float>(kMaxThumbPx) * 0.5f;
constexpr int kThumbYInDock =
    kPresetScaleUp
        ? options::layout::padding::top + kThumbRowOffsetY
        : static_cast<int>(kScaledThumbCenterY - static_cast<float>(kThumbPx) * 0.5f)
            + kThumbRowOffsetY;
constexpr int kDockH = kPresetScaleUp
    ? options::layout::padding::top
        + kThumbPx
        + options::layout::visual::overflowBottom
        + options::layout::padding::bottom
        + kThumbRowOffsetY
    : options::layout::padding::top
        + options::layout::visual::overflowTop
        + kMaxThumbPx
        + options::layout::visual::overflowBottom
        + options::layout::padding::bottom;
constexpr int kPaintExtentH = kPresetScaleUp
    ? kDockH
        + (kMaxThumbPx - kThumbPx)
        + options::layout::visual::overflowTop
    : kDockH;

constexpr UINT_PTR kAnimTimerId = 0x504D4653u; // 'PMFS'
constexpr UINT_PTR kWarmThumbTimerId = 0x504D4654u; // 'PMFT'
constexpr UINT kDecodeMaxThumbSidePx = 180;

// ── Free helpers ─────────────────────────────────────────────────────────────

namespace {

template <typename T>
inline void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

float SmoothStep01(float t)
{
    t = (std::clamp)(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

} // namespace

// ── Impl ─────────────────────────────────────────────────────────────────────

class FilmStripWidget::Impl {
public:
    // ── Host ────────────────────────────────────────────────────────────────
    HWND host = nullptr;

    // ── Data ────────────────────────────────────────────────────────────────
    std::vector<std::wstring>            paths;
    std::vector<std::unique_ptr<Thumb>>  thumbs;
    size_t sel   = 0;

    // ── Theme ────────────────────────────────────────────────────────────────
    bool   dark = true; // updated each PaintOverGdi call from the host

    // ── Scroll state ────────────────────────────────────────────────────────
    float  scroll         = 0.f;
    float  scrollTarget   = 0.f;
    bool   didInitScroll  = false;
    bool   animActive     = false;

    // ── Focus / cover-flow animation ────────────────────────────────────────
    float  focusCenter         = 0.f;
    float  focusCenterTarget   = 0.f;
    float  focusStrength       = 0.f;
    float  focusStrengthTarget = 0.f;
    float  focusVel            = 0.f;

    // ── Pointer cache (for scroll-anim focus refresh) ────────────────────────
    int    lastPtrX = 0, lastPtrY = 0;
    int    lastW    = 0, lastH    = 0;

    // ── D2D / WIC resources ─────────────────────────────────────────────────
    ID2D1Factory*        d2d   = nullptr;
    ID2D1DCRenderTarget* dcRt  = nullptr;
    IWICImagingFactory*  wic   = nullptr;
    IDWriteFactory*      dwrite= nullptr;
    IDWriteTextFormat*   fmt   = nullptr;
    UINT  rtDpi = 0;
    int   bottomInsetPx = 0; // host pixels below the strip, e.g. filename bar
    bool  renderDisabled = false;

    // Render-target-owned paint resources. These are hot-path objects, so avoid
    // creating/releasing them for every animated thumb frame.
    ID2D1SolidColorBrush* brShadow       = nullptr;
    ID2D1SolidColorBrush* brHalo         = nullptr;
    ID2D1SolidColorBrush* brSelectedRing = nullptr;
    ID2D1SolidColorBrush* brNormalRing   = nullptr;
    ID2D1SolidColorBrush* brBg           = nullptr;
    ID2D1SolidColorBrush* brSep          = nullptr;
    ID2D1SolidColorBrush* brText         = nullptr;
    ID2D1Layer*           thumbClipLayer = nullptr;
    bool paintBrushesDark = true;
    bool paintBrushesValid = false;

    // Cached D2D bitmaps for the dock thumbnails (UI thread only)
    std::unordered_map<std::wstring, ID2D1Bitmap*> thumbBmps;
    std::unordered_set<std::wstring> thumbDecodeFailures;
    int  decodeBudgetThisPaint = 0;
    bool skippedDecodeThisPaint = false;
    size_t warmCacheCursor = 0;
    size_t warmCacheVisited = 0;
    bool   warmCacheActive = false;

    // Kept for FileViewer's WndProc integration. The original mini filmstrip is
    // synchronous/cached; no background upload queue is used here.
    enum : UINT { WM_FILMSTRIP_THUMB_READY = WM_APP + 0x71 };

    void FlushPendingUploads() {}

    // ── Callbacks ───────────────────────────────────────────────────────────
    FilmStripWidget::SelectionChangedCallback onSelectionChanged;
    FilmStripWidget::PathsChangedCallback     onPathsChanged;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    bool Initialize(HWND h, const std::vector<std::wstring>& p)
    {
        Shutdown();
        host  = h;
        paths = p;
        sel   = 0;
        RebuildThumbs();
        StartWarmCache();
        return true;
    }

    void SetPaths(const std::vector<std::wstring>& p)
    {
        if (p == paths) {
            // Host image reload/navigation re-sends the same list. Preserve all
            // visual state: scroll, focus, decoded thumbs, and initial layout.
            return;
        }
        const size_t oldSel = sel;
        paths = p;
        sel = (oldSel < paths.size()) ? oldSel : (paths.empty() ? 0 : paths.size() - 1);
        scroll = scrollTarget = 0.f;
        didInitScroll = false;
        focusCenter = focusCenterTarget = 0.f;
        focusStrength = focusStrengthTarget = focusVel = 0.f;
        ClearBitmapCache();
        RebuildThumbs();
        StartWarmCache();
        if (onPathsChanged) onPathsChanged(paths.size());
        RequestPaint();
    }

    void SetSelectionSilent(size_t index)
    {
        if (index < Count() && index != sel) {
            sel = index;
            // Silent host sync should not move thumbs. Keep scroll/focus exactly
            // where the user left them; only the selection chrome changes.
            RequestPaint();
        }
    }

    void SetSelectionInitial(size_t index)
    {
        if (index >= Count()) return;
        if (index != sel)
            sel = index;
        if (options::initialSelected && !didInitScroll) {
            const float f = static_cast<float>(sel);
            focusCenter = focusCenterTarget = f;
            focusStrength = focusStrengthTarget = 0.f;
            focusVel = 0.f;
        }
        StartWarmCache();
        RequestPaint();
    }

    void SetSelectionCentered(size_t index)
    {
        if (index >= Count()) return;
        if (index != sel)
            sel = index;
        const float f = static_cast<float>(sel);
        focusCenterTarget = f;
        focusStrengthTarget = 1.f;
        focusVel = 0.f;
        if (lastW > 0)
            CenterOnSelection(lastW);
        else
            didInitScroll = false;
        KickFocusBurst();
        KickAnim();
        StartWarmCache();
        RequestPaint();
    }

    void Shutdown()
    {
        if (host && animActive) { KillTimer(host, kAnimTimerId); animActive = false; }
        StopWarmCache();
        ClearBitmapCache();
        thumbs.clear();
        paths.clear();
        sel = 0;
        scroll = scrollTarget = 0.f;
        didInitScroll = false;
        focusCenter = focusCenterTarget = focusStrength = focusStrengthTarget = focusVel = 0.f;
        ReleasePaintBrushes();
        SafeRelease(fmt);
        SafeRelease(dwrite);
        SafeRelease(dcRt);
        SafeRelease(wic);
        SafeRelease(d2d);
        rtDpi = 0;
        renderDisabled = false;
        host = nullptr;
    }

    // ── Queries ──────────────────────────────────────────────────────────────

    bool   HasContent()   const noexcept { return !renderDisabled && !thumbs.empty(); }
    size_t GetSelection() const noexcept { return sel; }
    size_t Count()        const noexcept { return thumbs.size(); }

    // ── Internal helpers ─────────────────────────────────────────────────────

    void RebuildThumbs()
    {
        thumbs.clear();
        if (!wic) EnsureFactories(); // wic needed for ImageThumb
        thumbs.reserve(paths.size());
        for (const auto& p : paths) {
            auto t = ThumbFactory::Create(p, wic);
            if (t) thumbs.push_back(std::move(t));
        }
    }

    void ClearBitmapCache()
    {
        for (auto& e : thumbBmps) SafeRelease(e.second);
        thumbBmps.clear();
        thumbDecodeFailures.clear();
        for (auto& t : thumbs) t->Unload();
        warmCacheCursor = 0;
        warmCacheVisited = 0;
    }

    bool EnsureFactories()
    {
        if (renderDisabled) return false;
        if (d2d && wic && dwrite) return true;
        if (!d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d))) {
            MarkRenderDisabled();
            return false;
        }
        if (!wic && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                reinterpret_cast<void**>(&wic))))
            return false;
        if (!dwrite && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(&dwrite))))
            return false;
        if (!fmt) {
            if (FAILED(dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us", &fmt))) {
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us", &fmt);
            }
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
        // Rebuild thumbs now that wic is available
        if (thumbs.empty() && !paths.empty()) RebuildThumbs();
        return true;
    }

    bool EnsureDcRenderTarget()
    {
        if (renderDisabled || !d2d) return false;
        const UINT dpi = host ? ::GetDpiForWindow(host) : 96u;
        // DC render target is not sized at creation — BindDC supplies the rect
        // each frame. Only recreate when the target is gone or DPI changed.
        if (dcRt && dpi == rtDpi) return true;

        ReleasePaintBrushes();
        SafeRelease(dcRt);
        // Bitmaps are bound to the old render target on DPI change only.
        if (dpi != rtDpi) ClearBitmapCache();
        rtDpi = 0;

        D2D1_RENDER_TARGET_PROPERTIES rtp{};
        rtp.type                  = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        rtp.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        // IGNORE: D2D writes opaque pixels to the HDC. With PREMULTIPLIED,
        // any transparent D2D region (e.g. outside a rounded-rect clip) bleeds
        // the underlying GDI content through, causing ghost/double-render artefacts.
        rtp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        rtp.usage                 = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

        if (FAILED(d2d->CreateDCRenderTarget(&rtp, &dcRt)) || !dcRt) {
            MarkRenderDisabled();
            return false;
        }
        dcRt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        dcRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        dcRt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        rtDpi = dpi;
        return true;
    }

    void ReleasePaintBrushes()
    {
        SafeRelease(brShadow);
        SafeRelease(brHalo);
        SafeRelease(brSelectedRing);
        SafeRelease(brNormalRing);
        SafeRelease(brBg);
        SafeRelease(brSep);
        SafeRelease(brText);
        SafeRelease(thumbClipLayer);
        paintBrushesValid = false;
    }

    void EnsurePaintBrushes()
    {
        if (!dcRt) return;
        if (paintBrushesValid && paintBrushesDark == dark) return;

        ReleasePaintBrushes();
        paintBrushesDark = dark;

        dcRt->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.f), &brShadow);
        dcRt->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.88f, 1.f, 1.f), &brHalo);
        dcRt->CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.92f, 1.f, 0.92f), &brSelectedRing);
        dcRt->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.86f, 0.94f, 0.12f), &brNormalRing);

        const D2D1_COLOR_F bgClr = dark
            ? D2D1::ColorF(10.f / 255.f, 10.f / 255.f, 10.f / 255.f, 1.f)
            : D2D1::ColorF(1.f, 1.f, 1.f, 1.f);
        const D2D1_COLOR_F sepClr = dark
            ? D2D1::ColorF(1.f, 1.f, 1.f, 0.06f)
            : D2D1::ColorF(0.f, 0.f, 0.f, 0.08f);
        const D2D1_COLOR_F txtClr = dark
            ? D2D1::ColorF(0.62f, 0.74f, 0.88f, 0.72f)
            : D2D1::ColorF(0.30f, 0.40f, 0.55f, 0.80f);
        dcRt->CreateSolidColorBrush(bgClr, &brBg);
        dcRt->CreateSolidColorBrush(sepClr, &brSep);
        dcRt->CreateSolidColorBrush(txtClr, &brText);
        dcRt->CreateLayer(nullptr, &thumbClipLayer);

        paintBrushesValid = true;
    }

    void RequestPaint()
    {
        if (!host) return;
        // Invalidate only the dock strip, not the full client.  This prevents
        // the 60-fps hover animation from forcing a full GDI+ image blit.
        RECT rc{};
        ::GetClientRect(host, &rc);
        const int stripBottom = (std::max)(rc.top, rc.bottom - bottomInsetPx);
        const int paintTop = stripBottom - kPaintExtentH;
        if (paintTop < rc.top) {
            ::InvalidateRect(host, nullptr, FALSE); // window too small: full paint
            return;
        }
        // Include a tiny vertical margin for antialiasing at the top/bottom edge.
        RECT strip{rc.left,
                   static_cast<LONG>((std::max)(static_cast<int>(rc.top), paintTop - 2)),
                   rc.right,
                   static_cast<LONG>((std::min)(static_cast<int>(rc.bottom), stripBottom + 2))};
        ::InvalidateRect(host, &strip, FALSE);
    }

    void MarkRenderDisabled()
    {
        if (renderDisabled)
            return;
        renderDisabled = true;
        StopWarmCache();
        if (host)
            ::InvalidateRect(host, nullptr, FALSE);
    }

    void StartWarmCache()
    {
        if (!host || renderDisabled || paths.empty())
            return;
        warmCacheCursor = (sel < paths.size()) ? sel : 0;
        warmCacheVisited = 0;
        if (!warmCacheActive) {
            warmCacheActive = true;
            ::SetTimer(host, kWarmThumbTimerId, 18, nullptr);
        }
    }

    void StopWarmCache()
    {
        if (host && warmCacheActive)
            ::KillTimer(host, kWarmThumbTimerId);
        warmCacheActive = false;
        warmCacheCursor = 0;
        warmCacheVisited = 0;
    }

    bool WarmThumbCacheStep()
    {
        if (!warmCacheActive)
            return false;
        if (!HasContent() || !EnsureFactories() || !EnsureDcRenderTarget() || !dcRt) {
            StopWarmCache();
            return true;
        }

        int decodedThisTick = 0;
        const size_t n = paths.size();
        while (warmCacheVisited < n && decodedThisTick < 1) {
            const size_t i = warmCacheCursor % n;
            warmCacheCursor = (warmCacheCursor + 1) % n;
            ++warmCacheVisited;
            const std::wstring& path = paths[i];
            if (thumbBmps.find(path) != thumbBmps.end()
                || thumbDecodeFailures.find(path) != thumbDecodeFailures.end()) {
                continue;
            }
            decodeBudgetThisPaint = 1;
            skippedDecodeThisPaint = false;
            (void)GetCachedThumbBitmap(path);
            ++decodedThisTick;
        }

        if (warmCacheVisited >= n)
            StopWarmCache();
        RequestPaint();
        return true;
    }

    void KickAnim()
    {
        if (!host) return;
        if (!animActive) {
            ::SetTimer(host, kAnimTimerId, 16, nullptr);
            animActive = true;
        }
    }

    // ── Layout math ──────────────────────────────────────────────────────────

    float CellStride() const { return static_cast<float>(kThumbPx + kThumbGap); }

    float ContentWidth(int w) const
    {
        return static_cast<float>((std::max)(0, w - kPadLeft - kPadRight));
    }

    float ThumbFocusScale(size_t i) const
    {
        if (focusStrength <= 0.0001f) return 1.f;
        const float d = std::fabs(focusCenter - static_cast<float>(i));
        if (d >= kFocusInfluenceRadiusCells) return 1.f;
        const float t = 1.f - d / kFocusInfluenceRadiusCells;
        return 1.f + kFocusMaxBoost * focusStrength * SmoothStep01(t);
    }

    float ThumbWidth(size_t i) const
    {
        return static_cast<float>(kThumbPx) * ThumbFocusScale(i);
    }

    float NominalRowWidth() const
    {
        const size_t n = Count();
        if (!n) return 0.f;
        return static_cast<float>(n * kThumbPx + (n - 1) * kThumbGap);
    }

    float RowWidth() const
    {
        float sum = 0.f;
        const size_t n = Count();
        for (size_t i = 0; i < n; ++i) {
            sum += ThumbWidth(i);
            if (i + 1 < n) sum += static_cast<float>(kThumbGap);
        }
        return sum;
    }

    float RowOriginX(int w) const
    {
        const float vw = ContentWidth(w);
        const float row = RowWidth();
        const float centerPad = (std::max)(0.f, (vw - row) * 0.5f);
        return static_cast<float>(kPadLeft) + centerPad - scroll;
    }

    float MaxScroll(int w) const
    {
        return (std::max)(0.f, RowWidth() - ContentWidth(w));
    }

    void ClampScrollTarget(int w)
    {
        scrollTarget = (std::clamp)(scrollTarget, 0.f, MaxScroll(w));
    }

    void CenterOnSelection(int w)
    {
        const size_t n = Count();
        if (!n || sel >= n) return;
        float c = 0.f;
        for (size_t i = 0; i < sel; ++i) c += ThumbWidth(i) + static_cast<float>(kThumbGap);
        const float vw = ContentWidth(w);
        scrollTarget = c + ThumbWidth(sel) * 0.5f - vw * 0.5f;
        ClampScrollTarget(w);
    }

    void ComputeLeftEdges(float x0, std::vector<float>& out) const
    {
        const size_t n = Count();
        out.resize(n);
        float x = x0;
        for (size_t i = 0; i < n; ++i) {
            out[i] = x;
            x += ThumbWidth(i);
            if (i + 1 < n) x += static_cast<float>(kThumbGap);
        }
    }

    // ── Animation step ────────────────────────────────────────────────────────

    bool StepScroll()
    {
        const float d = scrollTarget - scroll;
        if (std::fabs(d) < 0.25f) {
            if (scroll != scrollTarget) { scroll = scrollTarget; return true; }
            return false;
        }
        scroll += d * 0.38f;
        return true;
    }

    bool StepFocus()
    {
        const size_t n = Count();
        if (!n) { focusVel = 0.f; return false; }
        const float lastIdx = static_cast<float>(n - 1);
        const float err = focusCenterTarget - focusCenter;
        focusVel += err * kFocusSpringAccel;
        focusVel *= kFocusSpringDrag;
        focusCenter += focusVel;
        focusCenter = (std::clamp)(focusCenter, 0.f, lastIdx);
        if (focusCenter <= 0.f && focusVel < 0.f) focusVel = 0.f;
        if (focusCenter >= lastIdx && focusVel > 0.f) focusVel = 0.f;
        const float ds = focusStrengthTarget - focusStrength;
        if (std::fabs(ds) > 0.0015f) focusStrength += ds * kFocusLerpStrength;
        else if (ds != 0.f) focusStrength = focusStrengthTarget;
        return std::fabs(err) > 0.0012f || std::fabs(focusVel) > 0.006f
            || std::fabs(focusStrengthTarget - focusStrength) > 0.0015f;
    }

    void KickFocusBurst()
    {
        // Mini filmstrip behavior: immediately advance the spring a few frames
        // on fresh pointer input so the visible focus catches up to the cursor.
        for (int i = 0; i < 4; ++i)
            (void)StepFocus();
    }

    bool AnimSettled() const
    {
        return std::fabs(scroll - scrollTarget) < 0.35f
            && std::fabs(focusCenter - focusCenterTarget) < 0.004f
            && std::fabs(focusStrength - focusStrengthTarget) < 0.004f
            && std::fabs(focusVel) < 0.008f;
    }

    void StopAnimIfIdle()
    {
        if (host && animActive && AnimSettled()) {
            scroll         = scrollTarget;
            focusCenter    = focusCenterTarget;
            focusStrength  = focusStrengthTarget;
            focusVel       = 0.f;
            ::KillTimer(host, kAnimTimerId);
            animActive = false;
        }
    }

    // ── Hit testing (plain client coords, no ULW mapping) ────────────────────

    float StripTopY(int h) const { return static_cast<float>(h - kDockH); }

    bool PtInStrip(int clientY, int h) const
    {
        return clientY >= h - kDockH && clientY < h;
    }

    // Returns thumb index under clientX (within the strip), or -1.
    int HitThumb(int clientX, int clientY, int w, int h) const
    {
        if (!PtInStrip(clientY, h)) return -1;
        const float x0 = RowOriginX(w);
        const float localX = static_cast<float>(clientX) - x0;
        if (localX < 0.f) return -1;
        float pos = 0.f;
        const size_t n = Count();
        for (size_t i = 0; i < n; ++i) {
            const float tw = ThumbWidth(i);
            if (localX < pos + tw) return static_cast<int>(i);
            pos += tw;
            if (i + 1 < n) {
                const float gapEnd = pos + static_cast<float>(kThumbGap);
                if (localX < gapEnd) return -1;
                pos = gapEnd;
            }
        }
        return -1;
    }

    // Update focus animation target from pointer position.
    // focusCenterTarget snaps to the nearest integer thumb index using the
    // actual (animated) thumb widths, not a continuous float derived from
    // nominal CellStride().  This eliminates mid-gap jitter: the spring only
    // ever transitions between discrete integer positions, so there is no
    // moving target to overshoot against.
    bool ApplyFocusFromPointer(int clientX, int clientY, int w, int h)
    {
        if (!PtInStrip(clientY, h)) {
            if (focusStrengthTarget > 0.f) { focusStrengthTarget = 0.f; focusVel = 0.f; }
            return false;
        }
        const size_t n = Count();
        if (!n) { return false; }

        const float x0 = RowOriginX(w);
        const float localX = static_cast<float>(clientX) - x0;
        const float rowEnd = NominalRowWidth() + static_cast<float>(kThumbPx) * kFocusMaxBoost * 2.5f;
        if (localX >= 0.f && localX <= rowEnd) {
            const float lastIdx = static_cast<float>(n - 1);
            const float focusLoc = localX - 0.5f * CellStride();
            focusCenterTarget = (std::clamp)(focusLoc / CellStride(), 0.f, lastIdx);
            focusStrengthTarget = 1.f;
            return true;
        }
        if (focusStrengthTarget > 0.f) { focusStrengthTarget = 0.f; focusVel = 0.f; }
        return false;
    }

    // ── D2D draw helpers ─────────────────────────────────────────────────────

    ID2D1Bitmap* GetCachedThumbBitmap(const std::wstring& path)
    {
        auto it = thumbBmps.find(path);
        if (it != thumbBmps.end() && it->second) return it->second;
        if (thumbDecodeFailures.find(path) != thumbDecodeFailures.end()) return nullptr;
        if (!dcRt || !wic) return nullptr;
        if (decodeBudgetThisPaint <= 0) {
            skippedDecodeThisPaint = true;
            return nullptr;
        }
        --decodeBudgetThisPaint;

        IWICBitmapDecoder* dec = nullptr;
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || !dec) {
            thumbDecodeFailures.insert(path);
            return nullptr;
        }
        IWICBitmapFrameDecode* frame = nullptr;
        HRESULT hr = dec->GetFrame(0, &frame);
        dec->Release();
        if (FAILED(hr) || !frame) {
            thumbDecodeFailures.insert(path);
            return nullptr;
        }
        UINT iw = 0, ih = 0; frame->GetSize(&iw, &ih);
        UINT tw = iw, th = ih;
        const UINT m = (std::max)(iw, ih);
        if (m > kDecodeMaxThumbSidePx) {
            const double s = static_cast<double>(kDecodeMaxThumbSidePx) / m;
            tw = (std::max)(1u, static_cast<UINT>(std::lround(iw * s)));
            th = (std::max)(1u, static_cast<UINT>(std::lround(ih * s)));
        }
        IWICBitmapScaler* scaler = nullptr;
        hr = wic->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr) && scaler) hr = scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant);
        frame->Release();
        if (FAILED(hr) || !scaler) {
            SafeRelease(scaler);
            thumbDecodeFailures.insert(path);
            return nullptr;
        }
        IWICFormatConverter* conv = nullptr;
        hr = wic->CreateFormatConverter(&conv);
        if (SUCCEEDED(hr) && conv)
            hr = conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapDitherTypeNone, nullptr, 0.f,
                                  WICBitmapPaletteTypeMedianCut);
        scaler->Release();
        if (FAILED(hr) || !conv) {
            SafeRelease(conv);
            thumbDecodeFailures.insert(path);
            return nullptr;
        }
        ID2D1Bitmap* bmp = nullptr;
        hr = dcRt->CreateBitmapFromWicBitmap(conv, nullptr, &bmp);
        conv->Release();
        if (SUCCEEDED(hr) && bmp) thumbBmps[path] = bmp;
        else thumbDecodeFailures.insert(path);
        return bmp;
    }

    D2D1_ROUNDED_RECT ScaledThumbRect(float left, float yThumb, float scale) const
    {
        const float w  = static_cast<float>(kThumbPx) * scale;
        const float rad  = 9.f * scale;
        if (kPresetScaleUp) {
            const float bottom = yThumb + static_cast<float>(kThumbPx);
            return D2D1::RoundedRect(
                D2D1::RectF(left, bottom - w, left + w, bottom), rad, rad);
        }
        const float cx = left + w * 0.5f;
        const float cy = yThumb + static_cast<float>(kThumbPx) * 0.5f;
        const float half = w * 0.5f;
        return D2D1::RoundedRect(
            D2D1::RectF(cx - half, cy - half, cx + half, cy + half), rad, rad);
    }

    void DrawDropShadow(ID2D1RenderTarget* rt,
                        const D2D1_ROUNDED_RECT& rr, float sz, float hard)
    {
        if (sz <= 0.5f || !brShadow) return;
        const float h  = (std::clamp)(hard, 0.f, 1.f);
        const int   L  = (std::clamp)(4 + static_cast<int>(sz * 0.4f + h * 5.f), 4, 14);
        const float oldOpacity = brShadow->GetOpacity();
        for (int k = L; k >= 1; --k) {
            const float t = static_cast<float>(k) / static_cast<float>(L);
            const float inf = sz * t;
            D2D1_ROUNDED_RECT sh = rr;
            sh.rect.left -= inf; sh.rect.top -= inf;
            sh.rect.right += inf; sh.rect.bottom += inf;
            sh.radiusX += inf * 0.35f; sh.radiusY += inf * 0.35f;
            const float u = static_cast<float>(L - k + 1) / static_cast<float>(L);
            const float falloff = std::pow((std::max)(0.f, 1.f - u) + 0.08f, 1.f + h * 5.5f);
            const float a = (0.055f + 0.26f * h) * falloff
                * ((std::min)(sz, 14.f) / 14.f * 0.55f + 0.45f);
            brShadow->SetOpacity(a);
            rt->FillRoundedRectangle(sh, brShadow);
        }
        brShadow->SetOpacity(oldOpacity);
    }

    void DrawRoundedThumb(ID2D1RenderTarget* rt,
                          ID2D1Bitmap* bmp,
                          const D2D1_ROUNDED_RECT& rr,
                          float opacity) const
    {
        if (!bmp) return;
        const float bw = static_cast<float>(bmp->GetPixelSize().width);
        const float bh = static_cast<float>(bmp->GetPixelSize().height);
        const float cw = rr.rect.right - rr.rect.left;
        const float ch = rr.rect.bottom - rr.rect.top;
        if (bw < 1.f || bh < 1.f || cw < 1.f || ch < 1.f) return;
        const float s  = (options::thumbOptions::fill == options::thumbOptions::ThumbFill::Cover)
                         ? (std::max)(cw / bw, ch / bh) : (std::min)(cw / bw, ch / bh);
        const float dw = bw * s, dh = bh * s;
        const D2D1_RECT_F dst{rr.rect.left + (cw - dw) * 0.5f,
                              rr.rect.top  + (ch - dh) * 0.5f,
                              rr.rect.left + (cw - dw) * 0.5f + dw,
                              rr.rect.top  + (ch - dh) * 0.5f + dh};
        const D2D1_RECT_F src{0.f, 0.f, bw, bh};

        ID2D1RoundedRectangleGeometry* geo = nullptr;
        if (FAILED(d2d->CreateRoundedRectangleGeometry(rr, &geo)) || !geo) return;
        D2D1_LAYER_PARAMETERS lp{};
        lp.contentBounds     = D2D1::InfiniteRect();
        lp.geometricMask     = geo;
        lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
        lp.maskTransform     = D2D1::IdentityMatrix();
        lp.opacity           = 1.f;
        rt->PushLayer(&lp, thumbClipLayer);
        rt->DrawBitmap(bmp, &dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
        rt->PopLayer();
        geo->Release();
    }

    void DrawStripBackground(ID2D1RenderTarget* rt,
                              const D2D1_RECT_F& rc) const
    {
        if (brBg) rt->FillRectangle(rc, brBg);
        // Subtle inner top-edge line for depth.
        if (brSep) {
            rt->DrawLine(D2D1::Point2F(rc.left,  rc.top),
                         D2D1::Point2F(rc.right, rc.top),
                         brSep, 1.f);
        }
    }

    void DrawChrome(ID2D1RenderTarget* rt,
                    const D2D1_ROUNDED_RECT& rect,
                    bool selected)
    {
        if (selected && brHalo) {
            const float oldOpacity = brHalo->GetOpacity();
            for (int i = 3; i >= 1; --i) {
                const float inflate = static_cast<float>(i) * 2.2f;
                D2D1_ROUNDED_RECT outer = rect;
                outer.rect.left   -= inflate; outer.rect.top    -= inflate;
                outer.rect.right  += inflate; outer.rect.bottom += inflate;
                outer.radiusX += inflate * 0.35f;
                outer.radiusY += inflate * 0.35f;
                brHalo->SetOpacity(0.055f * static_cast<float>(i) * 0.92f);
                rt->FillRoundedRectangle(outer, brHalo);
            }
            brHalo->SetOpacity(oldOpacity);
        }

        ID2D1SolidColorBrush* ring = selected ? brSelectedRing : brNormalRing;
        if (ring)
            rt->DrawRoundedRectangle(rect, ring, selected ? 1.85f : 0.85f);
    }

    // ── Paint ────────────────────────────────────────────────────────────────

    void PaintOverGdi(HDC hdc, int w, int h, bool isDark)
    {
        dark = isDark; // cache for any helper that needs it
        if (host) {
            RECT hostRc{};
            ::GetClientRect(host, &hostRc);
            bottomInsetPx = (std::max)(0, static_cast<int>(hostRc.bottom) - h);
        }
        if (!HasContent()) return;
        if (!hdc || w < 32 || h < kDockH + 16) return;
        if (!EnsureFactories()) return;
        if (!EnsureDcRenderTarget() || !dcRt) return;
        EnsurePaintBrushes();
        // Thumbnail decode is warmed by kWarmThumbTimerId; paint must stay cheap
        // so hover magnification does not hitch on large source images.
        decodeBudgetThisPaint = 0;
        skippedDecodeThisPaint = false;

        // Bind only the dock strip rect — D2D doesn't need to own the full HDC
        // surface. D2D (0,0) maps to GDI (0, stripTop), so all draw coordinates
        // below use StripTopY relative to the full window height as-is because
        // we bind the full rect; this keeps all math unchanged while letting the
        // driver know only the bottom band is touched.
        RECT bind{0, 0, w, h};
        if (FAILED(dcRt->BindDC(hdc, &bind))) return;

        ClampScrollTarget(w);
        scroll = (std::clamp)(scroll, 0.f, MaxScroll(w));
        if (!didInitScroll && Count() > 0) {
            CenterOnSelection(w);
            scroll = scrollTarget;
            didInitScroll = true;
        }

        dcRt->BeginDraw();
        dcRt->SetTransform(D2D1::IdentityMatrix());

        const float stripY = StripTopY(h);
        const D2D1_RECT_F stripRc = D2D1::RectF(0.f, stripY,
                                                  static_cast<float>(w),
                                                  static_cast<float>(h));
        const D2D1_RECT_F paintRc = D2D1::RectF(0.f,
                                                  static_cast<float>(h - kPaintExtentH),
                                                  static_cast<float>(w),
                                                  static_cast<float>(h));

        // Clip to the full overlay extent. In ScaleUp the reserved dock stays
        // small, while focused thumbs are allowed to grow upward over the image.
        dcRt->PushAxisAlignedClip(paintRc, D2D1_ANTIALIAS_MODE_ALIASED);

        DrawStripBackground(dcRt, stripRc);

        const size_t n = Count();
        const float  yThumb = stripY + static_cast<float>(kThumbYInDock);
        const float  x0     = RowOriginX(w);

        std::vector<float> lefts;
        ComputeLeftEdges(x0, lefts);

        // Build visible draw order: paint non-selected before selected (depth)
        std::vector<size_t> order;
        order.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const float l = lefts[i];
            const float tw = static_cast<float>(kThumbPx) * ThumbFocusScale(i);
            if (l > static_cast<float>(w) + CellStride()) continue;
            if (l + tw < -CellStride()) continue;
            order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return ThumbFocusScale(a) < ThumbFocusScale(b);
        });

        constexpr float kShadow = options::thumbOptions::dropShadowSize;
        constexpr float kHard   = options::thumbOptions::dropShadowHardness;

        for (const size_t i : order) {
            const float scale = ThumbFocusScale(i);
            const D2D1_ROUNDED_RECT rr = ScaledThumbRect(lefts[i], yThumb, scale);
            const bool   isSel = (i == sel);
            ID2D1Bitmap* bmp   = (i < paths.size())
                                 ? GetCachedThumbBitmap(paths[i]) : nullptr;
            if (kShadow > 0.5f)
                DrawDropShadow(dcRt, rr, kShadow * scale, kHard);
            if (isSel)
                DrawChrome(dcRt, rr, true);
            if (bmp)
                DrawRoundedThumb(dcRt, bmp, rr, isSel ? 1.f : 0.78f);
            else if (i < thumbs.size())
                thumbs[i]->RenderPlaceholder(dcRt, rr, dark);
            if (!isSel)
                DrawChrome(dcRt, rr, false);
        }

        if (n == 0 && fmt) {
            const wchar_t* msg = L"No images";
            const D2D1_RECT_F tr = D2D1::RectF(static_cast<float>(kPadLeft),
                stripY + 8.f, static_cast<float>(w - kPadRight),
                static_cast<float>(h - 6));
            if (brText) {
                dcRt->DrawText(msg, static_cast<UINT32>(wcslen(msg)), fmt,
                               tr, brText, D2D1_DRAW_TEXT_OPTIONS_NONE,
                               DWRITE_MEASURING_MODE_NATURAL);
            }
        }

        dcRt->PopAxisAlignedClip();

        const HRESULT hr = dcRt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            ReleasePaintBrushes();
            SafeRelease(dcRt);
            rtDpi = 0;
            ClearBitmapCache();
            StartWarmCache();
        }
    }

    // ── Input ─────────────────────────────────────────────────────────────────

    bool OnMouseWheel(int delta, int clientX, int clientY, int w, int h)
    {
        lastPtrX = clientX; lastPtrY = clientY; lastW = w; lastH = h;
        if (!PtInStrip(clientY, h)) return false;
        const float step = 48.f * (static_cast<float>(delta) / WHEEL_DELTA);
        scrollTarget -= step;
        ClampScrollTarget(w);
        if (ApplyFocusFromPointer(clientX, clientY, w, h))
            KickFocusBurst();
        KickAnim();
        RequestPaint();
        return true;
    }

    bool OnLButtonDown(int clientX, int clientY, int w, int h)
    {
        if (!PtInStrip(clientY, h)) return false;
        const int idx = HitThumb(clientX, clientY, w, h);
        const size_t n = Count();
        if (idx < 0 || static_cast<size_t>(idx) >= n) return true; // ate click in strip
        const size_t newSel = static_cast<size_t>(idx);
        if (newSel == sel) {
            RequestPaint();
            return true;
        }

        sel = newSel;
        // Click selection should not scroll/recenter; it feels like the row
        // jumps away from the pointer. Programmatic navigation may still
        // center via SetSelectionSilent/CenterOnSelection.
        if (onSelectionChanged && sel < paths.size())
            onSelectionChanged(sel, paths[sel]);
        RequestPaint();
        return true;
    }

    void OnMouseMove(int clientX, int clientY, int w, int h)
    {
        lastPtrX = clientX; lastPtrY = clientY; lastW = w; lastH = h;
        const float prev = focusStrengthTarget;
        if (ApplyFocusFromPointer(clientX, clientY, w, h)) {
            KickAnim();
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, host, 0};
            ::TrackMouseEvent(&tme);
            KickFocusBurst();
            RequestPaint();
        } else if (prev > 0.f && focusStrengthTarget < 0.5f) {
            KickAnim(); RequestPaint();
        }
    }

    void OnMouseLeave()
    {
        if (focusStrengthTarget > 0.f || focusStrength > 0.01f) {
            focusStrengthTarget = 0.f; focusVel = 0.f;
            KickAnim(); RequestPaint();
        }
    }

    bool OnTimer(WPARAM id)
    {
        if (id == kWarmThumbTimerId)
            return WarmThumbCacheStep();
        if (id != kAnimTimerId) return false;
        const bool ms = StepScroll();
        if (ms && PtInStrip(lastPtrY, lastH))
            ApplyFocusFromPointer(lastPtrX, lastPtrY, lastW, lastH);
        const bool mf = StepFocus();
        StopAnimIfIdle();
        if (ms || mf) RequestPaint();
        return true;
    }
};

// ── Free function ─────────────────────────────────────────────────────────────

int DockHeightPx() noexcept { return kDockH; }
int PaintExtentPx() noexcept { return kPaintExtentH; }

// ── FilmStripWidget public API ───────────────────────────────────────────────

FilmStripWidget::FilmStripWidget()  : impl_(new Impl()) {}
FilmStripWidget::~FilmStripWidget() { impl_->Shutdown(); delete impl_; }

bool FilmStripWidget::Initialize(HWND h, const std::vector<std::wstring>& p)
    { return impl_->Initialize(h, p); }

void FilmStripWidget::SetPaths(const std::vector<std::wstring>& p)
    { impl_->SetPaths(p); }

void FilmStripWidget::SetSelectionSilent(size_t i) { impl_->SetSelectionSilent(i); }
void FilmStripWidget::SetSelectionInitial(size_t i) { impl_->SetSelectionInitial(i); }
void FilmStripWidget::SetSelectionCentered(size_t i) { impl_->SetSelectionCentered(i); }

void FilmStripWidget::Shutdown() { impl_->Shutdown(); }

bool   FilmStripWidget::HasContent()   const noexcept { return impl_->HasContent(); }
size_t FilmStripWidget::GetSelection() const noexcept { return impl_->GetSelection(); }
int    FilmStripWidget::DockHeightPx() noexcept       { return filmstrip::DockHeightPx(); }
int    FilmStripWidget::PaintExtentPx() noexcept      { return filmstrip::PaintExtentPx(); }

void FilmStripWidget::PaintOverGdi(HDC hdc, int w, int h, bool dark)
    { impl_->PaintOverGdi(hdc, w, h, dark); }

bool FilmStripWidget::OnMouseWheel(int d, int x, int y, int w, int h)
    { return impl_->OnMouseWheel(d, x, y, w, h); }
bool FilmStripWidget::OnLButtonDown(int x, int y, int w, int h)
    { return impl_->OnLButtonDown(x, y, w, h); }
void FilmStripWidget::OnMouseMove(int x, int y, int w, int h)
    { impl_->OnMouseMove(x, y, w, h); }
void FilmStripWidget::OnMouseLeave() { impl_->OnMouseLeave(); }
bool FilmStripWidget::OnTimer(WPARAM id) { return impl_->OnTimer(id); }

void FilmStripWidget::OnSelectionChanged(SelectionChangedCallback cb)
    { impl_->onSelectionChanged = std::move(cb); }
void FilmStripWidget::OnPathsChanged(PathsChangedCallback cb)
    { impl_->onPathsChanged = std::move(cb); }

void FilmStripWidget::FlushPendingUploads()
    { impl_->FlushPendingUploads(); }

} // namespace filmstrip
