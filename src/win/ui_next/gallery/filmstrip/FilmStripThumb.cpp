// FilmStripThumb.cpp — D2D + WIC thumbnail rendering.
// Adapted from apps/win32-mini/filmstrip/: WIC factory stored in ImageThumb,
// no extern g_wic; cover/contain fill; rounded-rect clip layer.

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

#include <wincodec.h>
#include "FilmStripThumb.h"

#include <algorithm>
#include <cmath>

namespace filmstrip {

// ── Thumb base ───────────────────────────────────────────────────────────────

D2D1_ROUNDED_RECT Thumb::MakeRoundedRect(float cx, float cy,
                                          float size, float radius)
{
    const float half = size * 0.5f;
    return D2D1::RoundedRect(
        D2D1::RectF(cx - half, cy - half, cx + half, cy + half),
        radius, radius);
}

void Thumb::RenderPlaceholder(ID2D1RenderTarget* rt,
                               const D2D1_ROUNDED_RECT& rect,
                               bool dark) const
{
    // Loading-state placeholder — subtly different from the strip background
    // so the user can tell "decoding in progress" vs "no image here".
    const D2D1_COLOR_F bgClr = dark
        ? D2D1::ColorF(0.13f, 0.13f, 0.16f, 1.f)
        : D2D1::ColorF(0.82f, 0.82f, 0.84f, 1.f);
    const D2D1_COLOR_F edgeClr = dark
        ? D2D1::ColorF(0.32f, 0.32f, 0.38f, 0.5f)
        : D2D1::ColorF(0.55f, 0.55f, 0.60f, 0.5f);
    ID2D1SolidColorBrush* bg = nullptr;
    if (SUCCEEDED(rt->CreateSolidColorBrush(bgClr, &bg)) && bg) {
        rt->FillRoundedRectangle(rect, bg);
        bg->Release();
    }
    ID2D1SolidColorBrush* edge = nullptr;
    if (SUCCEEDED(rt->CreateSolidColorBrush(edgeClr, &edge)) && edge) {
        rt->DrawRoundedRectangle(rect, edge, 1.f);
        edge->Release();
    }
}

// ── ImageThumb ───────────────────────────────────────────────────────────────

ImageThumb::ImageThumb(std::wstring path, IWICImagingFactory* wic)
    : path_(std::move(path)), wic_(wic)
{
}

ImageThumb::~ImageThumb()
{
    if (bitmap_) { bitmap_->Release(); bitmap_ = nullptr; }
}

void ImageThumb::EnsureLoaded(ID2D1RenderTarget* rt) const
{
    if (bitmap_ || attempted_) return;
    attempted_ = true;
    if (!rt || !wic_ || path_.empty()) return;

    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(wic_->CreateDecoderFromFilename(path_.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || !dec)
        return;

    IWICBitmapFrameDecode* frame = nullptr;
    HRESULT hr = dec->GetFrame(0, &frame);
    dec->Release();
    if (FAILED(hr) || !frame) return;

    UINT iw = 0, ih = 0;
    frame->GetSize(&iw, &ih);
    if (!iw || !ih) { frame->Release(); return; }

    // Scale down to decode budget (600 px longest edge)
    constexpr UINT kBudget = 600;
    UINT tw = iw, th = ih;
    const UINT maxEdge = (std::max)(iw, ih);
    if (maxEdge > kBudget) {
        const double s = static_cast<double>(kBudget) / maxEdge;
        tw = (std::max)(1u, static_cast<UINT>(std::lround(iw * s)));
        th = (std::max)(1u, static_cast<UINT>(std::lround(ih * s)));
    }

    IWICBitmapScaler* scaler = nullptr;
    hr = wic_->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr) && scaler)
        hr = scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant);
    frame->Release();
    if (FAILED(hr) || !scaler) { if (scaler) scaler->Release(); return; }

    IWICFormatConverter* conv = nullptr;
    hr = wic_->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr) && conv)
        hr = conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.f,
                              WICBitmapPaletteTypeMedianCut);
    scaler->Release();
    if (FAILED(hr) || !conv) { if (conv) conv->Release(); return; }

    hr = rt->CreateBitmapFromWicBitmap(conv, nullptr, &bitmap_);
    conv->Release();
    (void)hr;
}

void ImageThumb::Render(ID2D1RenderTarget* rt,
                         const D2D1_ROUNDED_RECT& rr,
                         float opacity) const
{
    EnsureLoaded(rt);
    if (!bitmap_) { RenderPlaceholder(rt, rr); return; }

    const float bw = static_cast<float>(bitmap_->GetPixelSize().width);
    const float bh = static_cast<float>(bitmap_->GetPixelSize().height);
    const float cw = rr.rect.right  - rr.rect.left;
    const float ch = rr.rect.bottom - rr.rect.top;
    if (bw < 1.f || bh < 1.f || cw < 1.f || ch < 1.f) return;

    const bool cover = (options::thumbOptions::fill
                        == options::thumbOptions::ThumbFill::Cover);
    const float s = cover ? (std::max)(cw / bw, ch / bh)
                           : (std::min)(cw / bw, ch / bh);
    const float dw = bw * s, dh = bh * s;
    const D2D1_RECT_F dst{rr.rect.left + (cw - dw) * 0.5f,
                          rr.rect.top  + (ch - dh) * 0.5f,
                          rr.rect.left + (cw - dw) * 0.5f + dw,
                          rr.rect.top  + (ch - dh) * 0.5f + dh};
    const D2D1_RECT_F src{0.f, 0.f, bw, bh};

    // Rounded-corner clip via geometry layer
    ID2D1Factory* fac = nullptr;
    rt->GetFactory(&fac);
    if (!fac) return;

    ID2D1RoundedRectangleGeometry* geo = nullptr;
    if (FAILED(fac->CreateRoundedRectangleGeometry(rr, &geo)) || !geo) {
        fac->Release(); return;
    }
    fac->Release();

    D2D1_LAYER_PARAMETERS lp{};
    lp.contentBounds     = D2D1::InfiniteRect();
    lp.geometricMask     = geo;
    lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    lp.maskTransform     = D2D1::IdentityMatrix();
    lp.opacity           = 1.f; // clip only; opacity applied in DrawBitmap

    rt->PushLayer(&lp, nullptr);
    rt->DrawBitmap(bitmap_, &dst, opacity,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    rt->PopLayer();
    geo->Release();
}

void ImageThumb::Preload(ID2D1RenderTarget* rt) { EnsureLoaded(rt); }

void ImageThumb::Unload()
{
    if (bitmap_) { bitmap_->Release(); bitmap_ = nullptr; }
    attempted_ = false;
}

// ── ThumbFactory ─────────────────────────────────────────────────────────────

std::unique_ptr<Thumb> ThumbFactory::Create(const std::wstring& path,
                                             IWICImagingFactory* wic)
{
    if (path.empty() || !wic) return nullptr;
    return std::make_unique<ImageThumb>(path, wic);
}

// ── DrawChrome ───────────────────────────────────────────────────────────────

namespace thumb {

void DrawChrome(ID2D1RenderTarget* rt,
                const D2D1_ROUNDED_RECT& rect,
                bool selected)
{
    // Selection halo (outer rings)
    if (selected) {
        for (int i = 3; i >= 1; --i) {
            const float inflate = static_cast<float>(i) * 2.2f;
            D2D1_ROUNDED_RECT outer = rect;
            outer.rect.left   -= inflate; outer.rect.top    -= inflate;
            outer.rect.right  += inflate; outer.rect.bottom += inflate;
            outer.radiusX += inflate * 0.35f;
            outer.radiusY += inflate * 0.35f;
            const float a = 0.055f * static_cast<float>(i) * 0.92f;
            ID2D1SolidColorBrush* h = nullptr;
            if (SUCCEEDED(rt->CreateSolidColorBrush(
                    D2D1::ColorF(0.35f, 0.88f, 1.f, a), &h)) && h) {
                rt->FillRoundedRectangle(outer, h);
                h->Release();
            }
        }
    }
    // Ring
    const D2D1_COLOR_F ringCol = selected
        ? D2D1::ColorF(0.45f, 0.92f, 1.f, 0.92f)
        : D2D1::ColorF(0.82f, 0.86f, 0.94f, 0.12f);
    const float sw = selected ? 1.85f : 0.85f;
    ID2D1SolidColorBrush* b = nullptr;
    if (SUCCEEDED(rt->CreateSolidColorBrush(ringCol, &b)) && b) {
        rt->DrawRoundedRectangle(rect, b, sw);
        b->Release();
    }
}

} // namespace thumb

} // namespace filmstrip
