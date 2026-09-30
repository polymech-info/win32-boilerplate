# GDI+ Filmstrip Fallback — Reference Implementation

This is a reference/documentation-only sketch of a GDI+-based filmstrip renderer
that can paint into any `HDC`, including off-screen memory DCs.  It was prototyped
during "win10 d2d" debugging and is **not compiled** — the production path uses D2D.

## Why D2D cannot use a memory DC

`ID2D1DCRenderTarget::BindDC` requires a **screen-compatible DC**.  It fails
(`E_INVALIDARG` / `D2DERR_*`) when given a GDI off-screen memory DC created with
`CreateCompatibleDC`.  The fix is to call `PaintOverGdi` after `BitBlt`, on the
real screen DC (`dc.GetHDC()`), not on `hMem`.

## Root cause in FileViewer.cpp (fixed)

```cpp
// WRONG — hMem is an off-screen DC; BindDC fails every time
m_filmstrip.PaintOverGdi(hMem, w, h - barH, pal.dark);
::BitBlt(dc.GetHDC(), 0, 0, w, h, hMem, 0, 0, SRCCOPY);

// CORRECT — paint the filmstrip onto the screen DC after the BitBlt composite
::BitBlt(dc.GetHDC(), 0, 0, w, h, hMem, 0, 0, SRCCOPY);
if (m_filmstrip.HasContent())
    m_filmstrip.PaintOverGdi(dc.GetHDC(), w, h - barH, pmui::theme_palette().dark);
```

## GDI+ fallback sketch

If a true GDI+ fallback is ever needed (e.g. for printing or off-screen compositing),
the approach below works with any HDC.  Key points:

- Uses `Gdiplus::Image` loaded from disk; **must be cached** per path to avoid
  reloading on every paint frame (60 fps × N thumbnails = unacceptable I/O).
- No rounded-corner clipping, no drop-shadow, no spring animation — pure static layout.
- `gdiplus.lib` must already be linked (it is, via `#pragma comment(lib, "gdiplus.lib")`
  in `FilmStrip.cpp`).

```cpp
// Deps already present in FilmStrip.cpp:
//   #include <gdiplus.h>
//   #pragma comment(lib, "gdiplus.lib")

// Per-Impl cache to add alongside thumbBmps:
//   std::unordered_map<std::wstring, Gdiplus::Image*> gdiThumbCache;
// Clear in ClearBitmapCache():
//   for (auto& e : gdiThumbCache) delete e.second;
//   gdiThumbCache.clear();

void PaintOverGdiGdiPlusFallback(HDC hdc, int w, int h, bool isDark)
{
    const UINT dpiPaint    = host ? ::GetDpiForWindow(host) : 96u;
    const int scaledDockH  = DpiScale(kDockH,   dpiPaint);
    const int scaledThumbPx= DpiScale(kThumbPx,  dpiPaint);
    const int scaledThumbGap=DpiScale(kThumbGap, dpiPaint);
    const int scaledPadLeft= DpiScale(kPadLeft,  dpiPaint);

    Gdiplus::Graphics gfx(hdc);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const float stripY = static_cast<float>(h - scaledDockH);

    // Background
    Gdiplus::SolidBrush bgBrush(isDark
        ? Gdiplus::Color(255, 10, 10, 10)
        : Gdiplus::Color(255, 245, 245, 245));
    gfx.FillRectangle(&bgBrush, 0, static_cast<INT>(stripY), w, scaledDockH);

    // Top border
    Gdiplus::Pen borderPen(isDark
        ? Gdiplus::Color(255, 35, 35, 35)
        : Gdiplus::Color(255, 195, 195, 195), 1.0f);
    gfx.DrawLine(&borderPen, 0, static_cast<INT>(stripY), w, static_cast<INT>(stripY));

    const size_t n = Count();
    if (n == 0) {
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 128, 128, 128));
        Gdiplus::Font font(L"Segoe UI", 12.0f);
        gfx.DrawString(L"No images", -1, &font,
                       Gdiplus::PointF(10.f, stripY + 10.f), &textBrush);
        return;
    }

    const int thumbTotal = scaledThumbPx + scaledThumbGap;
    const int totalWidth = static_cast<int>(n) * thumbTotal - scaledThumbGap;
    int startX = (w - totalWidth) / 2;
    if (startX < scaledPadLeft) startX = scaledPadLeft;

    for (size_t i = 0; i < n; ++i) {
        const int x = startX + static_cast<int>(i) * thumbTotal;
        const int y = static_cast<int>(stripY) + (scaledDockH - scaledThumbPx) / 2;

        // Selection ring
        if (i == sel) {
            Gdiplus::Pen selPen(Gdiplus::Color(255, 0, 200, 255), 3.0f);
            gfx.DrawRectangle(&selPen, x - 3, y - 3, scaledThumbPx + 6, scaledThumbPx + 6);
        }

        // Thumbnail (cached)
        bool drewImage = false;
        if (i < paths.size()) {
            Gdiplus::Image* img = nullptr;
            auto it = gdiThumbCache.find(paths[i]);
            if (it != gdiThumbCache.end()) {
                img = it->second;
            } else {
                auto* loaded = new Gdiplus::Image(paths[i].c_str());
                if (loaded->GetLastStatus() == Gdiplus::Ok)
                    gdiThumbCache[paths[i]] = img = loaded;
                else
                    delete loaded;
            }
            if (img) {
                const int iw = static_cast<int>(img->GetWidth());
                const int ih = static_cast<int>(img->GetHeight());
                if (iw > 0 && ih > 0) {
                    const float s = std::min(
                        static_cast<float>(scaledThumbPx) / iw,
                        static_cast<float>(scaledThumbPx) / ih);
                    const int dw = static_cast<int>(iw * s);
                    const int dh = static_cast<int>(ih * s);
                    gfx.DrawImage(img,
                        x + (scaledThumbPx - dw) / 2,
                        y + (scaledThumbPx - dh) / 2, dw, dh);
                    drewImage = true;
                }
            }
        }

        // Placeholder
        if (!drewImage) {
            Gdiplus::SolidBrush ph(((i / 5) % 2 == 0)
                ? Gdiplus::Color(255, 64, 64, 64)
                : Gdiplus::Color(255, 80, 80, 80));
            gfx.FillRectangle(&ph, x, y, scaledThumbPx, scaledThumbPx);
        }

        // Border
        const bool isSel = (i == sel);
        Gdiplus::Pen bp(isSel
            ? Gdiplus::Color(255, 0, 200, 255)
            : (isDark ? Gdiplus::Color(255, 60, 60, 60)
                      : Gdiplus::Color(255, 180, 180, 180)),
            isSel ? 2.0f : 1.0f);
        gfx.DrawRectangle(&bp, x, y, scaledThumbPx, scaledThumbPx);
    }
}
```
