#include "stdafx.h"
#include "FileViewer.h"
#include "FileViewerImageCropTool.h"
#include "FileViewerImageToolInterface.h"
#include "helpers/theme.hpp"
#include "helpers/text_conv.hpp"
#include "llm/llm_fs_guard.hpp"
#if defined(FEATURE_SVG_BUTTONS)
#include "helpers/svg_raster.hpp"
#include "svg_paths.generated.h"
#endif
#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

#pragma comment(lib, "gdiplus.lib")

namespace {

constexpr double kHandleViewPx   = 10.0;
constexpr double kEdgeHitViewPx = 8.0;
constexpr double kMinCropImgPx  = 8.0;
constexpr double kSnapViewPx    = 10.0;

inline double Clampd(double v, double lo, double hi)
{
    return (std::max)(lo, (std::min)(hi, v));
}

} // namespace

void CFileViewerImageCropTool::SetImageSize(int iw, int ih)
{
    m_iw = iw;
    m_ih = ih;
    if (m_active && m_iw > 0 && m_ih > 0)
        ResetCropToFullImage();
}

void CFileViewerImageCropTool::SetAspectPreset(PmAspectPreset p)
{
    m_preset = p;
    switch (p) {
    case PmAspectPreset::Free:
        m_ratio = {false, 1, 1};
        break;
    case PmAspectPreset::Original:
        m_ratio = {true, m_iw > 0 ? static_cast<double>(m_iw) : 1.0, m_ih > 0 ? static_cast<double>(m_ih) : 1.0};
        break;
    case PmAspectPreset::Square:
        m_ratio = {true, 1, 1};
        break;
    case PmAspectPreset::Ratio_4_3:
        m_ratio = {true, 4, 3};
        break;
    case PmAspectPreset::Ratio_3_2:
        m_ratio = {true, 3, 2};
        break;
    case PmAspectPreset::Ratio_16_9:
        m_ratio = {true, 16, 9};
        break;
    case PmAspectPreset::Ratio_9_16:
        m_ratio = {true, 9, 16};
        break;
    default:
        m_ratio = {false, 1, 1};
        break;
    }
}

void CFileViewerImageCropTool::SetViewTransform(int clientW, int clientH, double zoom, double viewCx,
    double viewCy)
{
    m_cw   = clientW;
    m_ch   = clientH;
    m_zoom = zoom;
    m_vcx  = viewCx;
    m_vcy  = viewCy;
}

void CFileViewerImageCropTool::SetActive(bool on) noexcept
{
    m_active = on;
    if (!on) {
        m_drag = {};
    }
}

void CFileViewerImageCropTool::ImageToView(double ix, double iy, double* vx, double* vy) const
{
    *vx = m_cw * 0.5 + (ix - m_vcx) * m_zoom;
    *vy = m_ch * 0.5 + (iy - m_vcy) * m_zoom;
}

void CFileViewerImageCropTool::ViewToImage(double vx, double vy, double* ix, double* iy) const
{
    *ix = m_vcx + (vx - m_cw * 0.5) / m_zoom;
    *iy = m_vcy + (vy - m_ch * 0.5) / m_zoom;
}

PmRectD CFileViewerImageCropTool::CropViewRectD() const
{
    double l, t, r, b;
    ImageToView(m_cropImg.x, m_cropImg.y, &l, &t);
    ImageToView(m_cropImg.x + m_cropImg.w, m_cropImg.y + m_cropImg.h, &r, &b);
    if (r < l)
        std::swap(l, r);
    if (b < t)
        std::swap(t, b);
    return {l, t, r - l, b - t};
}

void CFileViewerImageCropTool::NormalizeCrop(PmRectD& r) const
{
    if (r.w < 0) {
        r.x += r.w;
        r.w = -r.w;
    }
    if (r.h < 0) {
        r.y += r.h;
        r.h = -r.h;
    }
}

void CFileViewerImageCropTool::ClampCropToImage(PmRectD& r) const
{
    NormalizeCrop(r);
    if (m_iw <= 0 || m_ih <= 0)
        return;
    r.w = (std::max)(kMinCropImgPx, (std::min)(r.w, static_cast<double>(m_iw)));
    r.h = (std::max)(kMinCropImgPx, (std::min)(r.h, static_cast<double>(m_ih)));
    r.x = Clampd(r.x, 0, static_cast<double>(m_iw) - r.w);
    r.y = Clampd(r.y, 0, static_cast<double>(m_ih) - r.h);
}

void CFileViewerImageCropTool::ApplySnap(PmRectD& r, PmCropHit activeHit)
{
    (void)activeHit;
    if (m_zoom <= 1e-9 || m_iw <= 0 || m_ih <= 0)
        return;
    const double snapImg = kSnapViewPx / m_zoom;
    // Snap edges to image bounds
    if (std::fabs(r.x) <= snapImg)
        r.x = 0;
    if (std::fabs(r.y) <= snapImg)
        r.y = 0;
    if (std::fabs(r.x + r.w - m_iw) <= snapImg)
        r.x = static_cast<double>(m_iw) - r.w;
    if (std::fabs(r.y + r.h - m_ih) <= snapImg)
        r.y = static_cast<double>(m_ih) - r.h;
}

PmCropHit CFileViewerImageCropTool::HitTestView(POINT clientPt) const
{
    if (!m_active || m_iw <= 0 || m_ih <= 0 || m_zoom <= 1e-9)
        return PmCropHit::None;
    const PmRectD cv = CropViewRectD();
    const double  px = static_cast<double>(clientPt.x);
    const double  py = static_cast<double>(clientPt.y);
    const double  hs = kHandleViewPx * 0.5;

    auto inSquare = [&](double cx, double cy) {
        return std::fabs(px - cx) <= hs && std::fabs(py - cy) <= hs;
    };

    const double l = cv.x, t = cv.y, r = cv.x + cv.w, b = cv.y + cv.h;
    if (inSquare(l, t))
        return PmCropHit::TopLeft;
    if (inSquare(r, t))
        return PmCropHit::TopRight;
    if (inSquare(l, b))
        return PmCropHit::BottomLeft;
    if (inSquare(r, b))
        return PmCropHit::BottomRight;

    if (px >= l - kEdgeHitViewPx && px <= l + kEdgeHitViewPx && py >= t && py <= b)
        return PmCropHit::Left;
    if (px >= r - kEdgeHitViewPx && px <= r + kEdgeHitViewPx && py >= t && py <= b)
        return PmCropHit::Right;
    if (py >= t - kEdgeHitViewPx && py <= t + kEdgeHitViewPx && px >= l && px <= r)
        return PmCropHit::Top;
    if (py >= b - kEdgeHitViewPx && py <= b + kEdgeHitViewPx && px >= l && px <= r)
        return PmCropHit::Bottom;

    if (px > l && px < r && py > t && py < b)
        return PmCropHit::Move;
    return PmCropHit::None;
}

LPCTSTR CFileViewerImageCropTool::SuggestSetCursor(POINT clientPt) const
{
    switch (HitTestView(clientPt)) {
    case PmCropHit::TopLeft:
    case PmCropHit::BottomRight:
        return IDC_SIZENWSE;
    case PmCropHit::TopRight:
    case PmCropHit::BottomLeft:
        return IDC_SIZENESW;
    case PmCropHit::Left:
    case PmCropHit::Right:
        return IDC_SIZEWE;
    case PmCropHit::Top:
    case PmCropHit::Bottom:
        return IDC_SIZENS;
    case PmCropHit::Move:
        return IDC_SIZEALL;
    default:
        return nullptr;
    }
}

void CFileViewerImageCropTool::ResetCropToFullImage()
{
    if (m_iw <= 0 || m_ih <= 0) {
        m_cropImg = {};
        return;
    }
    m_cropImg = {0, 0, static_cast<double>(m_iw), static_cast<double>(m_ih)};
    SetAspectPreset(m_preset);
}

void CFileViewerImageCropTool::Cancel(HWND hwnd)
{
    (void)hwnd;
    SetActive(false);
}

void CFileViewerImageCropTool::UpdateDrag(double mx, double my)
{
    if (!m_drag.active)
        return;
    const double dix = mx - m_drag.startMouseImgX;
    const double diy = my - m_drag.startMouseImgY;
    PmRectD      c   = m_drag.startCrop;

    auto cornerAnchor = [&](PmCropHit corner) {
        PmRectD n = c;
        switch (corner) {
        case PmCropHit::TopLeft:
            n.x = c.x + dix;
            n.y = c.y + diy;
            n.w = c.w - dix;
            n.h = c.h - diy;
            break;
        case PmCropHit::TopRight:
            n.y = c.y + diy;
            n.w = c.w + dix;
            n.h = c.h - diy;
            break;
        case PmCropHit::BottomLeft:
            n.x = c.x + dix;
            n.w = c.w - dix;
            n.h = c.h + diy;
            break;
        case PmCropHit::BottomRight:
            n.w = c.w + dix;
            n.h = c.h + diy;
            break;
        default:
            break;
        }
        NormalizeCrop(n);
        ClampCropToImage(n);
        ApplySnap(n, corner);
        m_cropImg = n;
    };

    switch (m_drag.hit) {
    case PmCropHit::Move:
        c.x = c.x + dix;
        c.y = c.y + diy;
        ClampCropToImage(c);
        ApplySnap(c, PmCropHit::Move);
        m_cropImg = c;
        break;
    case PmCropHit::Left: {
        PmRectD n;
        n.x = c.x + dix;
        n.y = c.y;
        n.w = c.w - dix;
        n.h = c.h;
        NormalizeCrop(n);
        ClampCropToImage(n);
        m_cropImg = n;
        break;
    }
    case PmCropHit::Right: {
        PmRectD n;
        n.x = c.x;
        n.y = c.y;
        n.w = c.w + dix;
        n.h = c.h;
        NormalizeCrop(n);
        ClampCropToImage(n);
        m_cropImg = n;
        break;
    }
    case PmCropHit::Top: {
        PmRectD n;
        n.x = c.x;
        n.y = c.y + diy;
        n.w = c.w;
        n.h = c.h - diy;
        NormalizeCrop(n);
        ClampCropToImage(n);
        m_cropImg = n;
        break;
    }
    case PmCropHit::Bottom: {
        PmRectD n;
        n.x = c.x;
        n.y = c.y;
        n.w = c.w;
        n.h = c.h + diy;
        NormalizeCrop(n);
        ClampCropToImage(n);
        m_cropImg = n;
        break;
    }
    case PmCropHit::TopLeft:
        cornerAnchor(PmCropHit::TopLeft);
        break;
    case PmCropHit::TopRight:
        cornerAnchor(PmCropHit::TopRight);
        break;
    case PmCropHit::BottomLeft:
        cornerAnchor(PmCropHit::BottomLeft);
        break;
    case PmCropHit::BottomRight:
        cornerAnchor(PmCropHit::BottomRight);
        break;
    default:
        break;
    }
}

bool CFileViewerImageCropTool::OnMouseDown(HWND hwnd, POINT clientPt)
{
    if (!m_active)
        return false;
    const PmCropHit h = HitTestView(clientPt);
    if (h == PmCropHit::None)
        return false;
    double ix = 0, iy = 0;
    ViewToImage(static_cast<double>(clientPt.x), static_cast<double>(clientPt.y), &ix, &iy);
    m_drag.active         = true;
    m_drag.hit            = h;
    m_drag.startMouseImgX = ix;
    m_drag.startMouseImgY = iy;
    m_drag.startCrop      = m_cropImg;
    ::SetCapture(hwnd);
    return true;
}

bool CFileViewerImageCropTool::OnMouseMove(HWND hwnd, POINT clientPt)
{
    (void)hwnd;
    if (!m_active || !m_drag.active)
        return false;
    double ix = 0, iy = 0;
    ViewToImage(static_cast<double>(clientPt.x), static_cast<double>(clientPt.y), &ix, &iy);
    UpdateDrag(ix, iy);
    ::InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool CFileViewerImageCropTool::OnMouseUp(HWND hwnd, POINT /*clientPt*/)
{
    if (!m_drag.active)
        return false;
    m_drag.active = false;
    m_drag.hit    = PmCropHit::None;
    ::ReleaseCapture();
    NormalizeCrop(m_cropImg);
    ClampCropToImage(m_cropImg);
    ::InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void CFileViewerImageCropTool::OnCaptureLost(HWND hwnd)
{
    if (!m_drag.active)
        return;
    m_drag.active = false;
    m_drag.hit    = PmCropHit::None;
    NormalizeCrop(m_cropImg);
    ClampCropToImage(m_cropImg);
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

void CFileViewerImageCropTool::Paint(HDC hdc, HWND /*dpiHwnd*/) const
{
    if (!m_active || m_iw <= 0 || m_ih <= 0 || m_zoom <= 1e-9)
        return;

    double il = 0, it = 0, ir = 0, ib = 0;
    ImageToView(0, 0, &il, &it);
    ImageToView(static_cast<double>(m_iw), static_cast<double>(m_ih), &ir, &ib);
    if (ir < il)
        std::swap(il, ir);
    if (ib < it)
        std::swap(it, ib);

    const PmRectD cv = CropViewRectD();

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush dim(Gdiplus::Color(140, 0, 0, 0));

    const float imgL = (Gdiplus::REAL)il, imgT = (Gdiplus::REAL)it;
    const float imgR = (Gdiplus::REAL)ir, imgB = (Gdiplus::REAL)ib;
    const float cl   = (Gdiplus::REAL)cv.x, ct = (Gdiplus::REAL)cv.y;
    const float cr   = (Gdiplus::REAL)(cv.x + cv.w), cb = (Gdiplus::REAL)(cv.y + cv.h);

    auto fill = [&](float x, float y, float ww, float hh) {
        if (ww > 0.5f && hh > 0.5f)
            g.FillRectangle(&dim, x, y, ww, hh);
    };

    fill(imgL, imgT, imgR - imgL, ct - imgT);
    fill(imgL, cb, imgR - imgL, imgB - cb);
    fill(imgL, ct, cl - imgL, cb - ct);
    fill(cr, ct, imgR - cr, cb - ct);

    Gdiplus::Pen borderW(Gdiplus::Color(255, 255, 255, 255), 2.0f);
    g.DrawRectangle(&borderW, cl, ct, cr - cl, cb - ct);

    Gdiplus::SolidBrush hbr(Gdiplus::Color(255, 255, 255, 255));
    const float         hs = (Gdiplus::REAL)(kHandleViewPx * 0.5);
    const float corners[4][2] = {{cl, ct}, {cr, ct}, {cl, cb}, {cr, cb}};
    for (auto& c : corners)
        g.FillRectangle(&hbr, c[0] - hs, c[1] - hs, kHandleViewPx, kHandleViewPx);
}

void CFileViewerImageCropTool::GetCropExtractInts(int imageW, int imageH, int* outL, int* outT, int* outW,
    int* outH) const
{
    int left   = (int)std::floor(m_cropImg.x);
    int top    = (int)std::floor(m_cropImg.y);
    int right  = (int)std::ceil(m_cropImg.x + m_cropImg.w);
    int bottom = (int)std::ceil(m_cropImg.y + m_cropImg.h);
    int width  = right - left;
    int height = bottom - top;
    left   = (std::max)(0, (std::min)(left, imageW - 1));
    top    = (std::max)(0, (std::min)(top, imageH - 1));
    width  = (std::max)(1, (std::min)(width, imageW - left));
    height = (std::max)(1, (std::min)(height, imageH - top));
    *outL  = left;
    *outT  = top;
    *outW  = width;
    *outH  = height;
}

// ── Crop tool adapter (`IFileViewerImageTool`) + GDI+ save helpers ─────────────

namespace {

inline int Ui115(int basePx96, int dpi)
{
    return ::MulDiv(::MulDiv(basePx96, 115, 100), dpi, 96);
}

int GetGdiplusEncoderClsid(const WCHAR* mime, CLSID* clsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (num == 0 || size == 0)
        return -1;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, info) != static_cast<Gdiplus::Status>(0))
        return -1;
    for (UINT i = 0; i < num; ++i) {
        if (info[i].MimeType && wcscmp(info[i].MimeType, mime) == 0) {
            *clsid = info[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

const wchar_t* MimeTypeForImageExtension(const std::wstring& extLower)
{
    if (extLower == L".jpg" || extLower == L".jpeg")
        return L"image/jpeg";
    if (extLower == L".png")
        return L"image/png";
    if (extLower == L".bmp")
        return L"image/bmp";
    if (extLower == L".tif" || extLower == L".tiff")
        return L"image/tiff";
    if (extLower == L".gif")
        return L"image/gif";
    return nullptr;
}

bool SaveGdiplusImageToPath(Gdiplus::Image* img, const wchar_t* path, CString& err)
{
    err.Empty();
    if (!img || !path || !path[0]) {
        err = L"Invalid save path.";
        return false;
    }
    std::filesystem::path fp(path);
    auto ext = fp.extension().wstring();
    for (auto& c : ext)
        c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
    const wchar_t* mime = MimeTypeForImageExtension(ext);
    if (!mime) {
        err = L"Use a supported extension: .png, .jpg, .jpeg, .bmp, .tif, .gif.";
        return false;
    }
    CLSID enc{};
    if (GetGdiplusEncoderClsid(mime, &enc) < 0) {
        err = L"No image encoder for this format.";
        return false;
    }
    if (wcscmp(mime, L"image/jpeg") == 0) {
        Gdiplus::EncoderParameters ep{};
        ep.Count                      = 1;
        ep.Parameter[0].Guid          = Gdiplus::EncoderQuality;
        ep.Parameter[0].Type          = Gdiplus::EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ULONG                         quality = 92;
        ep.Parameter[0].Value         = &quality;
        if (img->Save(path, &enc, &ep) != static_cast<Gdiplus::Status>(0)) {
            err = L"Could not save JPEG.";
            return false;
        }
        return true;
    }
    if (img->Save(path, &enc, nullptr) != static_cast<Gdiplus::Status>(0)) {
        err = L"Could not save image.";
        return false;
    }
    return true;
}

#if defined(FEATURE_SVG_BUTTONS)
void FillRoundRectAlphaCropBtn(HDC hdc, int x, int y, int w, int h, int radius, BYTE alpha, BYTE rr, BYTE gg,
    BYTE bb)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float rx = (float)x, ry = (float)y, rw = (float)w, rh = (float)h;
    const float d  = (float)(radius * 2);
    Gdiplus::GraphicsPath path;
    path.AddArc(rx, ry, d, d, 180, 90);
    path.AddArc(rx + rw - d, ry, d, d, 270, 90);
    path.AddArc(rx + rw - d, ry + rh - d, d, d, 0, 90);
    path.AddArc(rx, ry + rh - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush br(Gdiplus::Color(alpha, rr, gg, bb));
    g.FillPath(&br, &path);
}

void DrawBitmapIconCropOk(HDC hdc, HBITMAP hb, int x, int y, int size, BYTE globalAlpha = 255)
{
    if (!hb || globalAlpha == 0)
        return;
    DIBSECTION ds{};
    if (::GetObject(hb, sizeof(ds), &ds) == sizeof(DIBSECTION) && ds.dsBm.bmBitsPixel == 32
        && ds.dsBm.bmBits != nullptr) {
        const int bw = static_cast<int>(std::labs(ds.dsBm.bmWidth));
        const int bh = static_cast<int>(std::labs(ds.dsBm.bmHeight));
        if (bw > 0 && bh > 0) {
            HDC mdc = ::CreateCompatibleDC(hdc);
            if (mdc) {
                HGDIOBJ old = ::SelectObject(mdc, hb);
                BLENDFUNCTION bf{};
                bf.BlendOp             = AC_SRC_OVER;
                bf.BlendFlags          = 0;
                bf.SourceConstantAlpha = globalAlpha;
                bf.AlphaFormat         = AC_SRC_ALPHA;
                (void)::AlphaBlend(hdc, x, y, size, size, mdc, 0, 0, bw, bh, bf);
                (void)::SelectObject(mdc, old);
                (void)::DeleteDC(mdc);
                return;
            }
        }
    }
}
#endif

Gdiplus::Bitmap* ExtractImageRegionToBitmap(Gdiplus::Image* src, int srcL, int srcT, int srcW, int srcH)
{
    if (!src || srcW <= 0 || srcH <= 0)
        return nullptr;
    constexpr Gdiplus::PixelFormat kFmt32Argb = static_cast<Gdiplus::PixelFormat>(2498570);
    auto* out = new Gdiplus::Bitmap(srcW, srcH, kFmt32Argb);
    if (!out || out->GetLastStatus() != static_cast<Gdiplus::Status>(0)) {
        delete out;
        return nullptr;
    }
    Gdiplus::Graphics g(out);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.DrawImage(src, Gdiplus::Rect(0, 0, srcW, srcH), srcL, srcT, srcW, srcH, Gdiplus::UnitPixel);
    return out;
}

} // namespace

class CCropFileViewerImageTool final : public IFileViewerImageTool {
public:
    ~CCropFileViewerImageTool() override { releaseCropOkBitmap(); }

    PmImageToolKind Kind() const noexcept override { return PmImageToolKind::Crop; }
    bool            IsActive() const noexcept override { return m_crop.Active(); }

    void SetActive(CFileViewer& host, bool on) override
    {
        (void)host;
        m_crop.SetActive(on);
    }

    void OnHostImageSized(CFileViewer& /*host*/, int iw, int ih) override
    {
        m_crop.SetActive(false);
        m_crop.SetImageSize(iw, ih);
    }

    void OnHostImageCleared(CFileViewer& /*host*/) override
    {
        m_crop.SetActive(false);
        m_crop.SetImageSize(0, 0);
        m_rcCropOk.SetRectEmpty();
        releaseCropOkBitmap();
    }

    void SetViewTransform(int clientW, int clientH, double zoom, double viewCx, double viewCy) override
    {
        m_crop.SetViewTransform(clientW, clientH, zoom, viewCx, viewCy);
    }

    bool OnMouseDown(CFileViewer& /*host*/, HWND hwnd, POINT clientPt) override
    {
        return m_crop.OnMouseDown(hwnd, clientPt);
    }
    bool OnMouseMove(CFileViewer& /*host*/, HWND hwnd, POINT clientPt) override
    {
        return m_crop.OnMouseMove(hwnd, clientPt);
    }
    bool OnMouseUp(CFileViewer& /*host*/, HWND hwnd, POINT clientPt) override
    {
        return m_crop.OnMouseUp(hwnd, clientPt);
    }
    void OnCaptureLost(CFileViewer& /*host*/, HWND hwnd) override { m_crop.OnCaptureLost(hwnd); }

    void PaintOverlay(const CFileViewer& /*host*/, HDC hdcMem, HWND dpiHwnd) override
    {
        m_crop.Paint(hdcMem, dpiHwnd);
    }

    void PaintChrome(const CFileViewer& /*host*/, HDC hdcMem, HWND dpiHwnd, int clientW, int clientH, int barH,
        int hoveredBtnEnum) override
    {
        if (!m_crop.Active() || clientW <= 0 || clientH <= 0)
            return;
        ensureCropOkBitmap(dpiHwnd);
        const int dpi = (int)::GetDpiForWindow(dpiHwnd);
        layoutCropOkButton(clientW, clientH, dpi, barH);
        paintCropOkButton(hdcMem, dpi, hoveredBtnEnum);
    }

    bool HitChrome(const CFileViewer& /*host*/, POINT clientPt) const override
    {
        return m_crop.Active() && m_rcCropOk.PtInRect(clientPt);
    }

    LPCTSTR SuggestSetCursor(const CFileViewer& /*host*/, POINT clientPt) const override
    {
        if (m_crop.Active() && m_rcCropOk.PtInRect(clientPt))
            return IDC_HAND;
        return m_crop.SuggestSetCursor(clientPt);
    }

    bool DraggingMouse() const noexcept override { return m_crop.Dragging(); }

    bool WantsVkReturnWhileActive() const noexcept override { return m_crop.Active(); }

    Gdiplus::Bitmap* TryCommitInMemoryEdit(CFileViewer& host) override
    {
        if (!m_crop.Active())
            return nullptr;
        Gdiplus::Image* img = host.PreviewImage();
        if (!img)
            return nullptr;
        const UINT iw = img->GetWidth(), ih = img->GetHeight();
        int        L = 0, T = 0, W = 0, H = 0;
        m_crop.GetCropExtractInts(static_cast<int>(iw), static_cast<int>(ih), &L, &T, &W, &H);
        const bool fullFrame = (L == 0 && T == 0 && W == static_cast<int>(iw) && H == static_cast<int>(ih));
        if (fullFrame)
            return nullptr;
        return ExtractImageRegionToBitmap(img, L, T, W, H);
    }

    void OnHostReplacedPreviewImage(CFileViewer& /*host*/, int newW, int newH) override
    {
        m_crop.SetImageSize(newW, newH);
        m_crop.ResetCropToFullImage();
    }

    bool HostAllowsOverwriteSave(const CFileViewer& host) const override
    {
        return host.HasLoadedImagePreview() && !host.PreviewPathW().empty();
    }

    bool SaveOverwrite(CFileViewer& host, CString& errOut) override
    {
        errOut.Empty();
        if (!host.HasLoadedImagePreview()) {
            errOut = L"No image to save.";
            return false;
        }
        if (host.PreviewPathW().empty()) {
            errOut = L"No file path — use Save As.";
            return false;
        }
        if (const std::string deny =
                media::llm::llm_fs_guard_write_deny_reason(std::filesystem::path(host.PreviewPathW()));
            !deny.empty()) {
            errOut = pmui::utf8_to_wide(deny).c_str();
            return false;
        }
        Gdiplus::Image* img = host.PreviewImage();
        const UINT      iw  = img->GetWidth(), ih = img->GetHeight();
        int             L = 0, T = 0, W = 0, H = 0;
        m_crop.GetCropExtractInts(static_cast<int>(iw), static_cast<int>(ih), &L, &T, &W, &H);
        std::unique_ptr<Gdiplus::Bitmap> bmp(ExtractImageRegionToBitmap(img, L, T, W, H));
        if (!bmp) {
            errOut = L"Could not build cropped image.";
            return false;
        }
        if (!SaveGdiplusImageToPath(bmp.get(), host.PreviewPathW().c_str(), errOut))
            return false;
        return true;
    }

    bool SaveAs(CFileViewer& host, HWND owner, CString& errOut) override
    {
        errOut.Empty();
        if (!host.HasLoadedImagePreview()) {
            errOut = L"No image to save.";
            return false;
        }
        Gdiplus::Image* img = host.PreviewImage();
        const UINT      iw  = img->GetWidth(), ih = img->GetHeight();
        int             L = 0, T = 0, W = 0, H = 0;
        m_crop.GetCropExtractInts(static_cast<int>(iw), static_cast<int>(ih), &L, &T, &W, &H);
        std::unique_ptr<Gdiplus::Bitmap> bmp(ExtractImageRegionToBitmap(img, L, T, W, H));
        if (!bmp) {
            errOut = L"Could not build cropped image.";
            return false;
        }

        wchar_t      pathBuf[MAX_PATH + 4]{};
        std::wstring initDir;
        std::wstring defName;
        {
            std::filesystem::path prev(host.PreviewPathW());
            if (!prev.empty()) {
                initDir = prev.parent_path().wstring();
                defName = prev.stem().wstring() + L"_crop";
                auto ext = prev.extension().wstring();
                for (auto& c : ext)
                    c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
                if (ext.empty())
                    ext = L".png";
                defName += ext;
            } else {
                defName = L"crop.png";
            }
            if (defName.size() < _countof(pathBuf))
                wcscpy_s(pathBuf, defName.c_str());
        }

        static const wchar_t kFilter[] =
            L"PNG (*.png)\0*.png\0"
            L"JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
            L"TIFF (*.tif;*.tiff)\0*.tif;*.tiff\0"
            L"Bitmap (*.bmp)\0*.bmp\0"
            L"GIF (*.gif)\0*.gif\0"
            L"\0";

        OPENFILENAMEW ofn{};
        ofn.lStructSize  = sizeof(ofn);
        ofn.hwndOwner    = owner;
        ofn.lpstrFilter  = kFilter;
        ofn.nFilterIndex = 1;
        ofn.lpstrFile    = pathBuf;
        ofn.nMaxFile     = static_cast<DWORD>(_countof(pathBuf));
        ofn.lpstrTitle   = L"Save cropped image";
        ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrDefExt  = L"png";
        if (!initDir.empty())
            ofn.lpstrInitialDir = initDir.c_str();

        if (!::GetSaveFileNameW(&ofn))
            return false;

        if (const std::string deny = media::llm::llm_fs_guard_write_deny_reason(std::filesystem::path(pathBuf));
            !deny.empty()) {
            errOut = pmui::utf8_to_wide(deny).c_str();
            return false;
        }

        if (!SaveGdiplusImageToPath(bmp.get(), pathBuf, errOut))
            return false;
        return true;
    }

    void OnDpiOrThemeChromeChange(CFileViewer& /*host*/) override { releaseCropOkBitmap(); }

    void ArmNewInteractiveSession(CFileViewer& /*host*/) override
    {
        m_crop.SetAspectPreset(PmAspectPreset::Free);
        m_crop.ResetCropToFullImage();
    }

private:
    void layoutCropOkButton(int clientW, int clientH, int dpi, int barH)
    {
        m_rcCropOk.SetRectEmpty();
        if (!m_crop.Active() || clientW <= 0 || clientH <= 0)
            return;
        const int btn = Ui115(40, dpi);
        const int gap = Ui115(10, dpi);
        const int r   = clientW - gap;
        const int b   = clientH - barH - gap;
        m_rcCropOk.SetRect(r - btn, b - btn, r, b);
    }

    void paintCropOkButton(HDC hdc, int dpi, int hoveredBtnEnum) const
    {
        if (!m_crop.Active() || m_rcCropOk.IsRectEmpty())
            return;
#if defined(FEATURE_SVG_BUTTONS)
        const bool  hov = (hoveredBtnEnum == CFileViewer::kImageToolChromeBtnConfirm);
        const int   rr  = Ui115(10, dpi);
        const CRect rc  = m_rcCropOk;
        const COLORREF fillBg = pmui::theme_palette().dark ? RGB(52, 54, 60) : RGB(240, 242, 248);
        FillRoundRectAlphaCropBtn(hdc, rc.left, rc.top, rc.Width(), rc.Height(), rr,
            hov ? (BYTE)200 : (BYTE)165, GetRValue(fillBg), GetGValue(fillBg), GetBValue(fillBg));
        if (m_bmpCropOk) {
            const int iconPx = ::MulDiv(::MulDiv(24, 115, 100), dpi, 96);
            const int ix     = rc.left + (rc.Width() - iconPx) / 2;
            const int iy     = rc.top + (rc.Height() - iconPx) / 2;
            DrawBitmapIconCropOk(hdc, m_bmpCropOk, ix, iy, iconPx, 255);
        }
#else
        (void)dpi;
        (void)hoveredBtnEnum;
        const CRect rc = m_rcCropOk;
        RECT rr{rc.left, rc.top, rc.right, rc.bottom};
        ::FillRect(hdc, &rr, (HBRUSH)::GetStockObject(DKGRAY_BRUSH));
#endif
    }

    void ensureCropOkBitmap(HWND dpiHwnd)
    {
#if !defined(FEATURE_SVG_BUTTONS)
        (void)dpiHwnd;
#else
        if (!dpiHwnd)
            return;
        const UINT dpi = static_cast<UINT>(::GetDpiForWindow(dpiHwnd));
        const int  iconPx = ::MulDiv(::MulDiv(24, 115, 100), static_cast<int>(dpi), 96);
        if (m_bmpCropOk && m_bmpCropOkDpi == dpi)
            return;
        releaseCropOkBitmap();
        m_bmpCropOkDpi = dpi;
        const auto& pal = pmui::theme_palette();
        const COLORREF iconRgb =
            pal.dark ? pal.window_fg : RGB(228, 230, 238);
        const auto r = static_cast<std::uint8_t>(GetRValue(iconRgb));
        const auto g = static_cast<std::uint8_t>(GetGValue(iconRgb));
        const auto b = static_cast<std::uint8_t>(GetBValue(iconRgb));
        std::wstring p(PM_TABLER_FILLED_DIR_W);
        if (!p.empty() && p.back() != L'/' && p.back() != L'\\')
            p += L'\\';
        p += L"circle-check.svg";
        m_bmpCropOk = pmui_svg_rasterize_file_wide(p.c_str(), iconPx, r, g, b);
#endif
    }

    void releaseCropOkBitmap()
    {
#if defined(FEATURE_SVG_BUTTONS)
        if (m_bmpCropOk) {
            ::DeleteObject(m_bmpCropOk);
            m_bmpCropOk = nullptr;
        }
        m_bmpCropOkDpi = 0;
#endif
    }

    CFileViewerImageCropTool m_crop{};
    CRect                    m_rcCropOk{};
#if defined(FEATURE_SVG_BUTTONS)
    HBITMAP m_bmpCropOk    = nullptr;
    UINT    m_bmpCropOkDpi = 0;
#endif
};

std::unique_ptr<IFileViewerImageTool> PmCreateFileViewerImageTool(PmImageToolKind kind)
{
    switch (kind) {
    case PmImageToolKind::Crop:
        return std::make_unique<CCropFileViewerImageTool>();
    default:
        return nullptr;
    }
}
