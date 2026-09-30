## Implementation (pm-image, Windows)

- **CLI:** Startup splash is **off by default**; pass **`--splash`** when launching the native UI to show this window (pair **`--no-splash`** is explicit off). See [CLI readme](../cli/readme.md).
- **CMake:** `option(FEATURE_SPLASH … ON)` — embeds `src/res/splash-1.png` and `src/res/logo.png` in `Resource.rc` (ids `IDB_PM_SPLASH_1_PNG` / `IDB_PM_LOGO_PNG` in `Resource.h`). Code: `helpers/splash_window.hpp` / `splash_window.cpp` (`pmui::splash_show` / `pmui::splash_hide`).
- **Main window:** `splash_show` after `install_ui_log_sink` in `launch_ui_next.cpp`; `splash_fade_out_and_hide(240)` in `CMainFrame::OnDeferredPostLayoutInit` (or `splash_hide` on exception). Layered `WS_EX_TOPMOST` + `LWA_ALPHA`.
- **Standalone `--ui-chat` (web):** `splash_reassert_topmost` from `CChatOnlyFrame::OnCreate` so the new frame does not cover the splash; `splash_fade_out_and_hide(420)` when JS posts `kind=ready` (If resources look stale, run `npm run config:release` then `npm run buildf`.)
- **Chat workbench (embedded WebView2 in the main frame):** splash fades on the same **`OnDeferredPostLayoutInit`** timer as the rest of the UI (~150ms after layout), not after the chat bundle posts `kind=ready` (that was too slow and blocked perceived startup on WebView2 + React cold load).
- **Native chat:** `splash_fade_out_and_hide(200)` at end of `OnCreate`.

The design notes below still apply to asset dimensions and DPI.

---

Short answer: **ship 2 PNGs (1× + 2×)**, sized for ~400–520px logical width, and scale at runtime.

## Recommended sizes

Use a **design size of ~440×260 (1×)**:

* **1× (normal DPI)** → `440 × 260`
* **2× (HiDPI / Retina)** → `880 × 520`

This hits a sweet spot:

* sharp on 100–200% scaling
* not too heavy
* flexible for layout

If your logo is more square:

* 1× → `360 × 360`
* 2× → `720 × 720`

## Why not bigger?

Going huge (e.g. 2000px) is pointless here:

* GDI+ will downscale anyway
* wastes memory + decode time (you *feel* this during startup)
* no visual gain for a splash

## DPI handling (important)

Use system DPI:

```cpp
UINT dpi = GetDpiForWindow(hwnd);
float scale = dpi / 96.0f;
```

Then scale your draw size:

```cpp
int targetW = int(440 * scale);
int targetH = int(260 * scale);
```

## Picking which PNG

Simple rule:

```cpp
if (dpi >= 144) use 2x;
else use 1x;
```

(144 = 150% scaling threshold — good cutoff)

## Resource setup

```rc
IDB_SPLASH_1X RCDATA "res/splash_1x.png"
IDB_SPLASH_2X RCDATA "res/splash_2x.png"
```

## Visual guidelines (what actually matters)

* transparent background PNG (let your window paint bg)
* keep margins → don’t fill full image
* avoid tiny text → blur on scaling
* center-weighted composition (works across sizes)

## If you want it *really* crisp

Use **SVG (ThorVG)** instead of PNG:

* single asset
* perfect scaling
* smaller binary size sometimes

But PNG 1× + 2× is already solid and simple.

---

## TL;DR

* 1×: **~440×260**
* 2×: **~880×520**
* switch at ~150% DPI
* scale at runtime
* don’t oversize assets

If you want, I can give you a version with **fade-in + fade-out animation** on the splash — makes it feel much more polished for almost no extra code.

--------------------------------------------------


Below is a ready-to-drop **Win32/Win32xx splash overlay** using a **PNG from `.rc` resource** + GDI+ spinner.

## `.rc`

```rc
IDB_SPLASH_PNG RCDATA "res/splash.png"
```

## `resource.h`

```cpp
#define IDB_SPLASH_PNG 5001
```

## `SplashOverlay.hpp`

```cpp
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

class SplashOverlay {
public:
    HWND hwnd{};
    UINT_PTR timer{};
    int frame{};
    Gdiplus::Image* png{};

    static constexpr wchar_t CLASS_NAME[] = L"PM_SplashOverlay";

    static void InitGdiPlus() {
        static ULONG_PTR token{};
        static bool once = false;
        if (!once) {
            Gdiplus::GdiplusStartupInput in;
            Gdiplus::GdiplusStartup(&token, &in, nullptr);
            once = true;
        }
    }

    bool Create(HWND parent, int pngResourceId) {
        InitGdiPlus();
        Register();

        png = LoadPngFromResource(pngResourceId);

        hwnd = CreateWindowExW(
            WS_EX_TRANSPARENT,
            CLASS_NAME,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, 100,
            parent,
            nullptr,
            GetModuleHandleW(nullptr),
            this
        );

        return hwnd != nullptr;
    }

    void ResizeToParent() {
        RECT rc{};
        GetClientRect(GetParent(hwnd), &rc);
        MoveWindow(hwnd, 0, 0, rc.right, rc.bottom, TRUE);
    }

    void Start() {
        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        timer = SetTimer(hwnd, 1, 16, nullptr);
    }

    void Stop() {
        if (timer) KillTimer(hwnd, timer);
        timer = 0;
        ShowWindow(hwnd, SW_HIDE);
    }

    ~SplashOverlay() {
        if (timer && hwnd) KillTimer(hwnd, timer);
        delete png;
    }

private:
    static void Register() {
        static bool done = false;
        if (done) return;

        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = CLASS_NAME;
        wc.lpfnWndProc = WndProc;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;

        RegisterClassW(&wc);
        done = true;
    }

    static Gdiplus::Image* LoadPngFromResource(int id) {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        HRSRC res = FindResourceW(hInst, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!res) return nullptr;

        DWORD size = SizeofResource(hInst, res);
        HGLOBAL loaded = LoadResource(hInst, res);
        if (!loaded) return nullptr;

        void* data = LockResource(loaded);
        if (!data) return nullptr;

        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!mem) return nullptr;

        void* dst = GlobalLock(mem);
        memcpy(dst, data, size);
        GlobalUnlock(mem);

        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(mem, TRUE, &stream) != S_OK)
            return nullptr;

        auto* img = Gdiplus::Image::FromStream(stream);
        stream->Release();

        if (!img || img->GetLastStatus() != Gdiplus::Ok) {
            delete img;
            return nullptr;
        }

        return img;
    }

    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<SplashOverlay*>(
            GetWindowLongPtrW(h, GWLP_USERDATA)
        );

        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }

        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
        case WM_TIMER:
            self->frame++;
            InvalidateRect(h, nullptr, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            self->Paint();
            return 0;
        }

        return DefWindowProcW(h, msg, wp, lp);
    }

    void Paint() {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

        Gdiplus::SolidBrush bg(Gdiplus::Color(245, 245, 245));
        g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

        int cx = rc.right / 2;
        int cy = rc.bottom / 2;

        if (png) {
            const int maxW = 260;
            const int maxH = 160;

            int iw = (int)png->GetWidth();
            int ih = (int)png->GetHeight();

            double scale = min((double)maxW / iw, (double)maxH / ih);
            int w = (int)(iw * scale);
            int h = (int)(ih * scale);

            g.DrawImage(
                png,
                cx - w / 2,
                cy - h / 2 - 40,
                w,
                h
            );
        }

        DrawSpinner(g, cx, cy + 80);

        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush text(Gdiplus::Color(80, 80, 80));

        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::RectF tr(
            0.0f,
            (float)(cy + 115),
            (float)rc.right,
            30.0f
        );

        g.DrawString(L"Starting…", -1, &font, tr, &fmt, &text);

        EndPaint(hwnd, &ps);
    }

    void DrawSpinner(Gdiplus::Graphics& g, int cx, int cy) {
        const int dots = 12;
        const float radius = 22.0f;

        for (int i = 0; i < dots; ++i) {
            float a = float((i + frame * 0.28) / dots * 6.28318530718);
            int alpha = 40 + i * 17;

            Gdiplus::SolidBrush brush(
                Gdiplus::Color(alpha, 30, 30, 30)
            );

            float x = cx + std::cos(a) * radius;
            float y = cy + std::sin(a) * radius;

            g.FillEllipse(&brush, x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        }
    }
};
```

## Use in Win32xx main window

```cpp
#include "SplashOverlay.hpp"
#include "resource.h"

class CMainFrame : public CFrame {
public:
    SplashOverlay splash;

    int OnCreate(CREATESTRUCT& cs) override {
        int ret = CFrame::OnCreate(cs);

        splash.Create(*this, IDB_SPLASH_PNG);
        splash.ResizeToParent();
        splash.Start();

        InitWebView2Hidden();

        return ret;
    }

    void OnSize(UINT type, CSize size) override {
        CFrame::OnSize(type, size);

        if (splash.hwnd)
            splash.ResizeToParent();

        ResizeWebView();
    }

    void OnWebAppReady() {
        ShowWebView2();
        splash.Stop();
    }
};
```

## WebView2 ready handoff

In your JS app:

```js
window.chrome?.webview?.postMessage({ type: "app-ready" });
```

In C++:

```cpp
if (wcsstr(json.get(), L"app-ready")) {
    OnWebAppReady();
}
```

That gives you:

```txt
native window appears instantly
PNG/logo shows immediately
spinner runs smoothly
WebView2 loads hidden
overlay disappears only when app is actually ready
```
