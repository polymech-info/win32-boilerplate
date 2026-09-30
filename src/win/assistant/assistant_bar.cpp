#include "assistant_bar.hpp"

#if defined(_WIN32) && defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>    // GET_X/Y_LPARAM
#include <shlobj.h>      // CSIDL_APPDATA
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// ── Private window messages ───────────────────────────────────────────────────
static constexpr UINT WM_ASST_SET_SPY = WM_USER + 1;
static constexpr UINT WM_ASST_SET_STT = WM_USER + 2;
static constexpr UINT WM_ASST_SET_TTS = WM_USER + 3;

// ── Timer IDs ──────────────────────────────────────────────────────────────────
static constexpr UINT_PTR TIMER_IDLE     = 1;   // delay before sliding off-screen
static constexpr UINT_PTR TIMER_ANIM     = 2;   // animation tick
static constexpr UINT     TIMER_IDLE_MS  = 2000;
static constexpr UINT     TIMER_ANIM_MS  = 14;  // ~70 fps

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr int k_w        = 34;
static constexpr int k_radius   = 10;
static constexpr int k_btn_sz   = 26;
static constexpr int k_pad_x    = (k_w - k_btn_sz) / 2;   // 4 px each side
static constexpr int k_gap      = 3;
static constexpr int k_pad_top  = 5;
static constexpr int k_pad_bot  = 5;
static constexpr int k_snap_dist = 24;  // px from edge → snap
static constexpr int k_peek      = 4;   // px still visible when hidden
static constexpr int k_hide_step = 4;   // px moved per animation frame

// ── Button indices ────────────────────────────────────────────────────────────
static constexpr int BTN_SPY   = 0;
static constexpr int BTN_STT   = 1;
static constexpr int BTN_TTS   = 2;
static constexpr int BTN_SHOT  = 3;
static constexpr int BTN_CHAT  = 4;
static constexpr int BTN_CLOSE = 5;

// ── Button Y positions (top of circle, in window coords) ──────────────────────
static constexpr int k_spy_y    = k_pad_top;
static constexpr int k_stt_y    = k_spy_y   + k_btn_sz + k_gap;
static constexpr int k_tts_y    = k_stt_y   + k_btn_sz + k_gap;
static constexpr int k_shot_y   = k_tts_y   + k_btn_sz + k_gap;
static constexpr int k_chat_y   = k_shot_y  + k_btn_sz + k_gap;
static constexpr int k_drag_top = k_chat_y  + k_btn_sz + k_gap;
static constexpr int k_drag_h   = 8;
static constexpr int k_close_y  = k_drag_top + k_drag_h + k_gap;
static constexpr int k_close_sz = k_btn_sz;
static constexpr int k_h        = k_close_y + k_close_sz + k_pad_bot;

// ── Colours (GDI+ ARGB) ───────────────────────────────────────────────────────
static const Gdiplus::Color kBg       {220, 18,  22,  38};   // dark navy, ~86% opaque
static const Gdiplus::Color kBorder   { 90, 255, 255, 255};
static const Gdiplus::Color kActiveSpy{255, 234,  88,  12};  // orange
static const Gdiplus::Color kActiveStt{255,  34, 197,  94};  // green
static const Gdiplus::Color kActiveTts{255,  99, 102, 241};  // indigo
static const Gdiplus::Color kInactive {150,  71,  85, 105};
static const Gdiplus::Color kHover    {210, 100, 116, 139};
static const Gdiplus::Color kCloseHov {220, 239,  68,  68};  // red on close hover
static const Gdiplus::Color kIcon     {255, 255, 255, 255};

namespace media::assistant {

// ── Snap / hide state ─────────────────────────────────────────────────────────
enum class SnapEdge  { None = 0, Left, Right, Top, Bottom };
enum class HideState { Visible, Hiding, Hidden, Showing };

// True when the bar is rotated 90° to lie horizontal (top/bottom snap).
static bool is_horiz_edge(SnapEdge e) {
    return e == SnapEdge::Top || e == SnapEdge::Bottom;
}

// ─────────────────────────────────────────────────────────────────────────────
// Position persistence  (%APPDATA%\PolyMech\pm-image\assistant-bar.pos)
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring pos_file_path() {
    wchar_t dir[MAX_PATH] = {};
    if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) return {};
    return std::wstring(dir) + L"\\PolyMech\\pm-image\\assistant-bar.pos";
}

static void save_pos(int x, int y, SnapEdge edge) {
    const std::wstring path = pos_file_path();
    if (path.empty()) return;
    const std::wstring dir = path.substr(0, path.rfind(L'\\'));
    ::CreateDirectoryW(dir.c_str(), nullptr);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") == 0 && f) {
        std::fprintf(f, "%d %d %d\n", x, y, static_cast<int>(edge));
        std::fclose(f);
    }
}

static bool load_pos(int& x, int& y, SnapEdge& edge) {
    const std::wstring path = pos_file_path();
    if (path.empty()) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) return false;
    int e = 0;
    const bool ok = (std::fscanf(f, "%d %d %d", &x, &y, &e) >= 2);
    std::fclose(f);
    edge = ok ? static_cast<SnapEdge>(e) : SnapEdge::None;
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Leftmost monitor: default snap position
// ─────────────────────────────────────────────────────────────────────────────

static POINT leftmost_monitor_pos() {
    struct Best { int min_left; POINT pos; };
    Best best = {INT_MAX, {0, 0}};
    ::EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rc, LPARAM lp) -> BOOL {
            auto& b = *reinterpret_cast<Best*>(lp);
            if (rc->left < b.min_left) {
                b.min_left = rc->left;
                b.pos.x   = rc->left;
                b.pos.y   = rc->top + (rc->bottom - rc->top - k_h) / 2;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&best));
    return best.pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ helpers
// ─────────────────────────────────────────────────────────────────────────────

static void fill_round_rect(Gdiplus::Graphics& g,
                             float x, float y, float w, float h,
                             float r, Gdiplus::Color col)
{
    Gdiplus::GraphicsPath path;
    const float d = r * 2.f;
    path.AddArc(x,         y,         d, d, 180.f,  90.f);
    path.AddArc(x + w - d, y,         d, d, 270.f,  90.f);
    path.AddArc(x + w - d, y + h - d, d, d,   0.f,  90.f);
    path.AddArc(x,         y + h - d, d, d,  90.f,  90.f);
    path.CloseFigure();
    Gdiplus::SolidBrush br(col);
    g.FillPath(&br, &path);
}

static void stroke_round_rect(Gdiplus::Graphics& g,
                               float x, float y, float w, float h,
                               float r, Gdiplus::Color col, float pw = 1.f)
{
    Gdiplus::GraphicsPath path;
    const float d = r * 2.f;
    const float px = x + .5f, py = y + .5f, qw = w - 1.f, qh = h - 1.f;
    path.AddArc(px,          py,          d, d, 180.f,  90.f);
    path.AddArc(px + qw - d, py,          d, d, 270.f,  90.f);
    path.AddArc(px + qw - d, py + qh - d, d, d,   0.f,  90.f);
    path.AddArc(px,          py + qh - d, d, d,  90.f,  90.f);
    path.CloseFigure();
    Gdiplus::Pen pen(col, pw);
    g.DrawPath(&pen, &path);
}

// ─────────────────────────────────────────────────────────────────────────────
// Icons (sized for 26 px button circles)
// ─────────────────────────────────────────────────────────────────────────────

// Power / spy toggle: arc with gap at top + vertical stem
static void draw_power_icon(Gdiplus::Graphics& g, float cx, float cy, Gdiplus::Color col)
{
    Gdiplus::Pen pen(col, 1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    // 270° arc centred at 6 o'clock (leaves 90° gap at 12 o'clock)
    g.DrawArc(&pen, cx - 5.5f, cy - 3.5f, 11.f, 11.f, 315.f, 270.f);
    // Stem through the gap
    g.DrawLine(&pen, cx, cy - 7.5f, cx, cy - 0.5f);
}

// Microphone: capsule body + U-stand + stem + foot bar
static void draw_mic_icon(Gdiplus::Graphics& g, float cx, float cy, Gdiplus::Color col)
{
    Gdiplus::SolidBrush br(col);
    Gdiplus::Pen        pen(col, 1.4f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    const float bx = cx - 1.75f, by = cy - 5.5f, bw = 3.5f, bh = 7.f, br2 = 1.75f;
    Gdiplus::GraphicsPath body;
    body.AddArc(bx, by,              br2 * 2.f, br2 * 2.f, 180.f, 180.f);
    body.AddArc(bx, by + bh - br2*2, br2 * 2.f, br2 * 2.f,   0.f, 180.f);
    body.CloseFigure();
    g.FillPath(&br, &body);

    g.DrawArc(&pen, cx - 3.5f, cy + 0.5f, 7.f, 5.5f, 0.f, 180.f);
    g.DrawLine(&pen, cx, cy + 6.f, cx, cy + 8.f);
    g.DrawLine(&pen, cx - 2.5f, cy + 8.f, cx + 2.5f, cy + 8.f);
}

// Speaker: trapezoid cone + two arcs
static void draw_speaker_icon(Gdiplus::Graphics& g, float cx, float cy, Gdiplus::Color col)
{
    Gdiplus::SolidBrush br(col);
    Gdiplus::Pen        pen(col, 1.4f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::PointF pts[4] = {
        {cx - 5.f, cy - 1.8f},
        {cx - 5.f, cy + 1.8f},
        {cx - 0.5f, cy + 5.f},
        {cx - 0.5f, cy - 5.f},
    };
    g.FillPolygon(&br, pts, 4);
    g.DrawArc(&pen, cx,       cy - 3.f,  6.f,  6.f, 315.f, 90.f);
    g.DrawArc(&pen, cx + 1.f, cy - 5.f, 10.f, 10.f, 320.f, 80.f);
}

// Camera: rounded-rect body (stroke) + viewfinder bump + lens circle
static void draw_camera_icon(Gdiplus::Graphics& g, float cx, float cy, Gdiplus::Color col)
{
    Gdiplus::Pen pen(col, 1.4f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    const float bw = 12.f, bh = 8.f;
    const float by = cy - bh / 2.f + 1.f;
    stroke_round_rect(g, cx - bw / 2.f, by, bw, bh, 1.5f, col, 1.4f);

    // Viewfinder bump
    Gdiplus::PointF bump[] = {
        {cx - 2.f, by},
        {cx - 1.f, by - 2.5f},
        {cx + 1.f, by - 2.5f},
        {cx + 2.f, by},
    };
    g.DrawLines(&pen, bump, 4);

    g.DrawEllipse(&pen, cx - 2.5f, by + 1.f, 5.f, 5.f);
}

// Chat bubble: rounded rect + tail + three dots
static void draw_chat_icon(Gdiplus::Graphics& g, float cx, float cy, Gdiplus::Color col)
{
    Gdiplus::SolidBrush br(col);
    Gdiplus::Pen        pen(col, 1.4f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    const float bw = 12.f, bh = 8.5f;
    const float bx = cx - bw / 2.f, by = cy - bh / 2.f - 1.5f;
    stroke_round_rect(g, bx, by, bw, bh, 2.f, col, 1.4f);

    Gdiplus::PointF tail[] = {
        {bx + 1.5f, by + bh - 0.5f},
        {bx - 1.f,  by + bh + 2.5f},
        {bx + 4.f,  by + bh - 0.5f},
    };
    g.DrawLines(&pen, tail, 3);

    for (int i = -1; i <= 1; ++i)
        g.FillEllipse(&br, cx + i * 3.f - 1.f, by + bh / 2.f - 1.f, 2.f, 2.f);
}

// Close: X cross
static void draw_x_icon(Gdiplus::Graphics& g, float cx, float cy, Gdiplus::Color col)
{
    Gdiplus::Pen pen(col, 1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float s = 4.5f;
    g.DrawLine(&pen, cx - s, cy - s, cx + s, cy + s);
    g.DrawLine(&pen, cx + s, cy - s, cx - s, cy + s);
}

// Drag-handle dots (2 rows, centred)
static void draw_drag_dots(Gdiplus::Graphics& g, int top_y)
{
    Gdiplus::SolidBrush dot(Gdiplus::Color(65, 255, 255, 255));
    for (int row = 0; row < 2; ++row) {
        const float dy = static_cast<float>(top_y + row * 5);
        for (int col = 0; col < 2; ++col) {
            const float dx = static_cast<float>(k_w / 2 - 3 + col * 7);
            g.FillEllipse(&dot, dx, dy, 2.f, 2.f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-window state
// ─────────────────────────────────────────────────────────────────────────────

struct WndState {
    AssistantBarCallbacks cbs;
    std::atomic<bool> spy_on{true};
    std::atomic<bool> stt_on{false};
    std::atomic<bool> tts_on{false};
    int hover_btn = -1;   // BTN_* or -1

    SnapEdge  snap_edge    = SnapEdge::None;
    HideState hide_state   = HideState::Visible;
    int       full_x       = 0;  // window x when fully visible
    int       full_y       = 0;  // window y when fully visible
    int       anim_progress = 0; // px slid toward hidden (0 = fully shown)

    static WndState* from(HWND h) {
        return reinterpret_cast<WndState*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Animation helpers
// ─────────────────────────────────────────────────────────────────────────────

static int hide_dist(const WndState* st)
{
    // k_w is the narrow dimension for both orientations:
    //   vertical bar (left/right snap): k_w wide  → slide by k_w - k_peek
    //   horizontal bar (top/bottom snap): k_w tall → slide by k_w - k_peek
    switch (st->snap_edge) {
    case SnapEdge::Left:
    case SnapEdge::Right:
    case SnapEdge::Top:
    case SnapEdge::Bottom: return k_w - k_peek;
    default:               return 0;
    }
}

static POINT anim_pos(const WndState* st)
{
    POINT p = {st->full_x, st->full_y};
    switch (st->snap_edge) {
    case SnapEdge::Left:   p.x -= st->anim_progress; break;
    case SnapEdge::Right:  p.x += st->anim_progress; break;
    case SnapEdge::Top:    p.y -= st->anim_progress; break;
    case SnapEdge::Bottom: p.y += st->anim_progress; break;
    default: break;
    }
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering — UpdateLayeredWindow with per-pixel PARGB
// ─────────────────────────────────────────────────────────────────────────────

static void premultiply(void* bits, int count)
{
    auto* px = static_cast<DWORD*>(bits);
    for (int i = 0; i < count; ++i) {
        const DWORD c = px[i];
        const DWORD a = (c >> 24) & 0xFF;
        if (a == 0)   { px[i] = 0; continue; }
        if (a == 255) continue;
        const DWORD r = ((c >> 16) & 0xFF) * a / 255;
        const DWORD g = ((c >>  8) & 0xFF) * a / 255;
        const DWORD b = ( c        & 0xFF) * a / 255;
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

// Draw the complete vertical layout (k_w × k_h) onto an arbitrary Graphics context.
// Used for both the direct vertical render and as the source for the horizontal rotation.
static void draw_vert_layout(Gdiplus::Graphics& g,
                              bool spy_on, bool stt_on, bool tts_on, int hover)
{
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    fill_round_rect  (g, 1.f, 1.f, k_w - 2.f, k_h - 2.f, k_radius, kBg);
    stroke_round_rect(g, 1.f, 1.f, k_w - 2.f, k_h - 2.f, k_radius, kBorder, 0.8f);

    auto draw_btn = [&](int idx, int y_top, int sz, bool is_toggle, bool state) {
        const bool hov = (hover == idx);
        Gdiplus::Color bg_col;
        if (is_toggle && state) {
            switch (idx) {
            case BTN_SPY: bg_col = kActiveSpy; break;
            case BTN_STT: bg_col = kActiveStt; break;
            case BTN_TTS: bg_col = kActiveTts; break;
            default:      bg_col = kHover;     break;
            }
        } else if (hov && idx == BTN_CLOSE) {
            bg_col = kCloseHov;
        } else {
            bg_col = hov ? kHover : kInactive;
        }

        const float bx = static_cast<float>(k_pad_x);
        const float by = static_cast<float>(y_top);
        const float bs = static_cast<float>(sz);
        Gdiplus::SolidBrush cbr(bg_col);
        g.FillEllipse(&cbr, bx, by, bs, bs);

        const float cx = bx + bs / 2.f;
        const float cy = by + bs / 2.f;
        switch (idx) {
        case BTN_SPY:   draw_power_icon  (g, cx, cy, kIcon); break;
        case BTN_STT:   draw_mic_icon    (g, cx, cy, kIcon); break;
        case BTN_TTS:   draw_speaker_icon(g, cx, cy, kIcon); break;
        case BTN_SHOT:  draw_camera_icon (g, cx, cy, kIcon); break;
        case BTN_CHAT:  draw_chat_icon   (g, cx, cy, kIcon); break;
        case BTN_CLOSE: draw_x_icon      (g, cx, cy, kIcon); break;
        }
    };

    draw_btn(BTN_SPY,   k_spy_y,   k_btn_sz,  true,  spy_on);
    draw_btn(BTN_STT,   k_stt_y,   k_btn_sz,  true,  stt_on);
    draw_btn(BTN_TTS,   k_tts_y,   k_btn_sz,  true,  tts_on);
    draw_btn(BTN_SHOT,  k_shot_y,  k_btn_sz,  false, false);
    draw_btn(BTN_CHAT,  k_chat_y,  k_btn_sz,  false, false);
    draw_drag_dots(g, k_drag_top);
    draw_btn(BTN_CLOSE, k_close_y, k_close_sz, false, false);
}

static void repaint_ulw(HWND hwnd)
{
    WndState* st = WndState::from(hwnd);
    const bool spy_on = st ? st->spy_on.load() : true;
    const bool stt_on = st ? st->stt_on.load() : false;
    const bool tts_on = st ? st->tts_on.load() : false;
    const int  hover  = st ? st->hover_btn      : -1;

    const bool horiz = st && is_horiz_edge(st->snap_edge);
    const int  bmp_w = horiz ? k_h : k_w;
    const int  bmp_h = horiz ? k_w : k_h;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       =  bmp_w;
    bmi.bmiHeader.biHeight      = -bmp_h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   bits = nullptr;
    HDC     mdc  = ::CreateCompatibleDC(nullptr);
    HBITMAP dib  = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { ::DeleteDC(mdc); return; }
    HGDIOBJ old = ::SelectObject(mdc, dib);

    if (!horiz) {
        // ── Vertical: draw directly onto the DIB ─────────────────────────────
        Gdiplus::Graphics g(mdc);
        draw_vert_layout(g, spy_on, stt_on, tts_on, hover);
    } else {
        // ── Horizontal: render into a temporary vertical DIB, then rotate ────
        // GDI+ SetTransform + DrawImage on a GDI HDC only repositions the
        // destination rectangle — it does NOT rotate image content.  A direct
        // pixel-copy loop is guaranteed to work and has negligible cost for
        // our tiny 34×192 px surface.
        BITMAPINFO bmi_v      = {};
        bmi_v.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi_v.bmiHeader.biWidth       =  k_w;
        bmi_v.bmiHeader.biHeight      = -k_h; // top-down
        bmi_v.bmiHeader.biPlanes      = 1;
        bmi_v.bmiHeader.biBitCount    = 32;
        bmi_v.bmiHeader.biCompression = BI_RGB;

        void*   bv  = nullptr;
        HDC     mdc_v = ::CreateCompatibleDC(nullptr);
        HBITMAP dib_v = ::CreateDIBSection(nullptr, &bmi_v, DIB_RGB_COLORS, &bv, nullptr, 0);
        if (dib_v && bv) {
            HGDIOBJ old_v = ::SelectObject(mdc_v, dib_v);
            {
                Gdiplus::Graphics gv(mdc_v);
                draw_vert_layout(gv, spy_on, stt_on, tts_on, hover);
            }
            // 90° CCW rotation so the TOP of the vertical bar becomes the
            // LEFT of the horizontal bar (spy on left, close on right).
            // Mapping: dst_col = src_row, dst_row = k_w-1 - src_col
            // In flat arrays (top-down, stride = pixel-width):
            //   src[src_row * k_w + src_col]  →  dst[(k_w-1-src_col) * k_h + src_row]
            // dst surface is bmp_w=k_h wide × bmp_h=k_w tall.
            const auto* sp = static_cast<const DWORD*>(bv);
            auto*       dp = static_cast<DWORD*>(bits);
            for (int sv = 0; sv < k_h; ++sv)
                for (int su = 0; su < k_w; ++su)
                    dp[(k_w - 1 - su) * k_h + sv] = sp[sv * k_w + su];

            ::SelectObject(mdc_v, old_v);
            ::DeleteObject(dib_v);
        }
        ::DeleteDC(mdc_v);
    }

    premultiply(bits, bmp_w * bmp_h);

    HDC           screen = ::GetDC(nullptr);
    POINT         src    = {0, 0};
    SIZE          sz     = {bmp_w, bmp_h};
    BLENDFUNCTION bf     = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    ::UpdateLayeredWindow(hwnd, screen, nullptr, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    ::ReleaseDC(nullptr, screen);

    ::SelectObject(mdc, old);
    ::DeleteObject(dib);
    ::DeleteDC(mdc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Viewport safety: ensure window is at least partially visible on some monitor
// ─────────────────────────────────────────────────────────────────────────────

// Returns {x,y} clamped so that at least k_peek × k_peek px of the window
// (w × h) fall within some monitor work area.  Falls back to leftmost monitor
// default when the saved position is completely off-screen (e.g. after removing
// a monitor or dragging the window to the desktop edge while debugging).
static POINT ensure_visible(int x, int y, int w, int h)
{
    struct D { int x, y, w, h; bool ok; } d = {x, y, w, h, false};
    ::EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT m, LPARAM lp) -> BOOL {
            auto& d = *reinterpret_cast<D*>(lp);
            const int ox = std::max(d.x, static_cast<int>(m->left));
            const int oy = std::max(d.y, static_cast<int>(m->top));
            const int ex = std::min(d.x + d.w, static_cast<int>(m->right));
            const int ey = std::min(d.y + d.h, static_cast<int>(m->bottom));
            if (ex - ox >= k_peek && ey - oy >= k_peek) d.ok = true;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&d));
    if (d.ok) return {x, y};
    return leftmost_monitor_pos();
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-testing
// ─────────────────────────────────────────────────────────────────────────────

// Core hit-test in vertical drawing coords (k_w × k_h space).
static int hit_button_v(int x, int y)
{
    const float cx = k_pad_x + k_btn_sz / 2.f;
    auto in_circle = [&](int top_y, int sz) {
        const float dx = x - cx;
        const float dy = y - (top_y + sz / 2.f);
        return (dx*dx + dy*dy) <= (sz / 2.f) * (sz / 2.f);
    };
    if (in_circle(k_spy_y,   k_btn_sz))   return BTN_SPY;
    if (in_circle(k_stt_y,   k_btn_sz))   return BTN_STT;
    if (in_circle(k_tts_y,   k_btn_sz))   return BTN_TTS;
    if (in_circle(k_shot_y,  k_btn_sz))   return BTN_SHOT;
    if (in_circle(k_chat_y,  k_btn_sz))   return BTN_CHAT;
    if (in_circle(k_close_y, k_close_sz)) return BTN_CLOSE;
    return -1;
}

// Hit-test in window client coords; transforms to vertical space when horizontal.
// Inverse of 90° CW: (xh, yh) → (k_w − yh, xh) in vertical space.
static int hit_button(int x, int y, SnapEdge edge)
{
    if (is_horiz_edge(edge))
        return hit_button_v(k_w - y, x);
    return hit_button_v(x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Snap-to-edge: called after WM_EXITSIZEMOVE
// ─────────────────────────────────────────────────────────────────────────────

static void try_snap(HWND hwnd, WndState* st)
{
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    const int cur_w = rc.right  - rc.left;
    const int cur_h = rc.bottom - rc.top;
    st->full_x        = rc.left;
    st->full_y        = rc.top;
    st->anim_progress = 0;

    struct EnumData {
        int wx, wy, cur_w, cur_h;
        SnapEdge best_edge;
        int best_snap_x, best_snap_y, best_dist;
    } ed = {rc.left, rc.top, cur_w, cur_h,
            SnapEdge::None, rc.left, rc.top, k_snap_dist + 1};

    ::EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT mrc, LPARAM lp) -> BOOL {
            auto& ed = *reinterpret_cast<EnumData*>(lp);
            auto try_edge = [&](SnapEdge e, int sx, int sy, int dist) {
                if (dist < ed.best_dist) {
                    ed.best_dist   = dist;
                    ed.best_edge   = e;
                    ed.best_snap_x = sx;
                    ed.best_snap_y = sy;
                }
            };
            // Left/Right: bar stays vertical (k_w × k_h)
            try_edge(SnapEdge::Left,
                     mrc->left,        ed.wy,
                     std::abs(ed.wx - mrc->left));
            try_edge(SnapEdge::Right,
                     mrc->right - k_w, ed.wy,
                     std::abs(ed.wx + ed.cur_w - mrc->right));
            // Top/Bottom: bar flips horizontal (k_h × k_w), so it is k_w tall
            try_edge(SnapEdge::Top,
                     ed.wx, mrc->top,
                     std::abs(ed.wy - mrc->top));
            try_edge(SnapEdge::Bottom,
                     ed.wx, mrc->bottom - k_w,
                     std::abs(ed.wy + ed.cur_h - mrc->bottom));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ed));

    st->snap_edge = ed.best_edge;

    // Resize window to match the new orientation before repositioning.
    const bool new_horiz = is_horiz_edge(ed.best_edge);
    const int  new_w     = new_horiz ? k_h : k_w;
    const int  new_h     = new_horiz ? k_w : k_h;

    if (ed.best_edge != SnapEdge::None) {
        st->full_x = ed.best_snap_x;
        st->full_y = ed.best_snap_y;
        ::SetWindowPos(hwnd, nullptr, st->full_x, st->full_y, new_w, new_h,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        ::SetTimer(hwnd, TIMER_IDLE, TIMER_IDLE_MS, nullptr);
    } else {
        // No snap: ensure the window isn't completely off-screen
        const POINT safe = ensure_visible(st->full_x, st->full_y, k_w, k_h);
        st->full_x = safe.x;
        st->full_y = safe.y;
        ::SetWindowPos(hwnd, nullptr, st->full_x, st->full_y, k_w, k_h,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        ::KillTimer(hwnd, TIMER_IDLE);
        ::KillTimer(hwnd, TIMER_ANIM);
        st->hide_state = HideState::Visible;
    }
    save_pos(st->full_x, st->full_y, st->snap_edge);
    repaint_ulw(hwnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Screenshot → clipboard
// ─────────────────────────────────────────────────────────────────────────────

static void capture_to_clipboard(HWND bar_hwnd, HWND target)
{
    if (!target || !::IsWindow(target)) return;
    RECT rc;
    if (!::GetWindowRect(target, &rc)) return;
    const int w = rc.right  - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC     scrdc = ::GetDC(nullptr);
    HDC     memdc = ::CreateCompatibleDC(scrdc);
    HBITMAP bmp   = ::CreateCompatibleBitmap(scrdc, w, h);
    ::SelectObject(memdc, bmp);
    ::BitBlt(memdc, 0, 0, w, h, scrdc, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    ::DeleteDC(memdc);
    ::ReleaseDC(nullptr, scrdc);

    if (::OpenClipboard(bar_hwnd)) {
        ::EmptyClipboard();
        ::SetClipboardData(CF_BITMAP, bmp);  // clipboard owns bmp from here
        ::CloseClipboard();
    } else {
        ::DeleteObject(bmp);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WndState* st = WndState::from(hwnd);

    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &pt);
        const SnapEdge se = st ? st->snap_edge : SnapEdge::None;
        return (hit_button(pt.x, pt.y, se) >= 0) ? HTCLIENT : HTCAPTION;
    }

    case WM_EXITSIZEMOVE:
        if (st) try_snap(hwnd, st);
        return 0;

    case WM_MOUSEMOVE: {
        if (!st) break;
        // Trigger show animation when the peek strip is hovered while hidden/hiding
        if (st->snap_edge != SnapEdge::None) {
            if (st->hide_state == HideState::Hidden ||
                st->hide_state == HideState::Hiding) {
                ::KillTimer(hwnd, TIMER_IDLE);
                st->hide_state = HideState::Showing;
                ::SetTimer(hwnd, TIMER_ANIM, TIMER_ANIM_MS, nullptr);
            } else if (st->hide_state == HideState::Visible) {
                ::KillTimer(hwnd, TIMER_IDLE);
                ::SetTimer(hwnd, TIMER_IDLE, TIMER_IDLE_MS, nullptr);
            }
        }
        // Button hover tracking
        const int btn = hit_button(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), st->snap_edge);
        if (btn != st->hover_btn) {
            st->hover_btn = btn;
            repaint_ulw(hwnd);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            ::TrackMouseEvent(&tme);
        }
        break;
    }

    case WM_MOUSELEAVE:
        if (st) {
            st->hover_btn = -1;
            repaint_ulw(hwnd);
            if (st->snap_edge != SnapEdge::None && st->hide_state == HideState::Visible)
                ::SetTimer(hwnd, TIMER_IDLE, TIMER_IDLE_MS, nullptr);
        }
        break;

    case WM_TIMER: {
        if (!st) break;
        if (wp == TIMER_IDLE) {
            POINT cur;
            ::GetCursorPos(&cur);
            RECT wrc;
            ::GetWindowRect(hwnd, &wrc);
            if (!::PtInRect(&wrc, cur)) {
                ::KillTimer(hwnd, TIMER_IDLE);
                st->hide_state = HideState::Hiding;
                ::SetTimer(hwnd, TIMER_ANIM, TIMER_ANIM_MS, nullptr);
            }
        } else if (wp == TIMER_ANIM) {
            const int target = hide_dist(st);
            if (st->hide_state == HideState::Hiding) {
                st->anim_progress = std::min(st->anim_progress + k_hide_step, target);
                const POINT p = anim_pos(st);
                ::SetWindowPos(hwnd, nullptr, p.x, p.y, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (st->anim_progress >= target) {
                    st->hide_state = HideState::Hidden;
                    ::KillTimer(hwnd, TIMER_ANIM);
                }
            } else if (st->hide_state == HideState::Showing) {
                st->anim_progress = std::max(st->anim_progress - k_hide_step, 0);
                const POINT p = anim_pos(st);
                ::SetWindowPos(hwnd, nullptr, p.x, p.y, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (st->anim_progress <= 0) {
                    st->hide_state = HideState::Visible;
                    ::KillTimer(hwnd, TIMER_ANIM);
                    ::SetTimer(hwnd, TIMER_IDLE, TIMER_IDLE_MS, nullptr);
                }
            }
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (!st) break;
        const int btn = hit_button(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), st->snap_edge);
        switch (btn) {
        case BTN_SPY: {
            const bool on = !st->spy_on.load();
            st->spy_on.store(on);
            repaint_ulw(hwnd);
            if (st->cbs.on_spy_toggle) st->cbs.on_spy_toggle(on);
            break;
        }
        case BTN_STT: {
            const bool on = !st->stt_on.load();
            st->stt_on.store(on);
            repaint_ulw(hwnd);
            if (st->cbs.on_stt_toggle) st->cbs.on_stt_toggle(on);
            break;
        }
        case BTN_TTS: {
            const bool on = !st->tts_on.load();
            st->tts_on.store(on);
            repaint_ulw(hwnd);
            if (st->cbs.on_tts_toggle) st->cbs.on_tts_toggle(on);
            break;
        }
        case BTN_SHOT: {
            HWND target = st->cbs.get_target_hwnd ? st->cbs.get_target_hwnd() : nullptr;
            if (target) capture_to_clipboard(hwnd, target);
            break;
        }
        case BTN_CHAT:
            if (st->cbs.on_chat_open) st->cbs.on_chat_open();
            break;
        case BTN_CLOSE:
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        break;
    }

    case WM_RBUTTONUP: {
        HMENU menu = ::CreatePopupMenu();
        ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"PM-Image Assistant");
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, 1, L"Close toolbar");
        POINT pt2 = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ClientToScreen(hwnd, &pt2);
        const int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         pt2.x, pt2.y, 0, hwnd, nullptr);
        ::DestroyMenu(menu);
        if (cmd == 1) ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    }

    case WM_ASST_SET_SPY:
        if (st) { st->spy_on.store(wp != 0); repaint_ulw(hwnd); }
        return 0;

    case WM_ASST_SET_STT:
        if (st) { st->stt_on.store(wp != 0); repaint_ulw(hwnd); }
        return 0;

    case WM_ASST_SET_TTS:
        if (st) { st->tts_on.store(wp != 0); repaint_ulw(hwnd); }
        return 0;

    case WM_PAINT: {
        // ULW owns the surface — just validate the update region.
        PAINTSTRUCT ps;
        ::BeginPaint(hwnd, &ps);
        ::EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        ::KillTimer(hwnd, TIMER_IDLE);
        ::KillTimer(hwnd, TIMER_ANIM);
        if (st && st->cbs.on_close) st->cbs.on_close();
        ::PostQuitMessage(0);
        return 0;

    default: break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────────────
// AssistantBar public API
// ─────────────────────────────────────────────────────────────────────────────

static const wchar_t k_wnd_class[] = L"PmAssistantBar";

struct AssistantBar::Impl { HWND hwnd = nullptr; };

AssistantBar::AssistantBar()  : impl_(new Impl{}) {}
AssistantBar::~AssistantBar() { delete impl_; }

int AssistantBar::run(const AssistantBarConfig&   cfg,
                      const AssistantBarCallbacks& callbacks)
{
    ULONG_PTR gdip_token = 0;
    {
        Gdiplus::GdiplusStartupInput inp;
        Gdiplus::GdiplusStartup(&gdip_token, &inp, nullptr);
    }

    HINSTANCE hinst = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinst;
    wc.hCursor       = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = k_wnd_class;
    ::RegisterClassExW(&wc);

    int      pos_x = 0, pos_y = 0;
    SnapEdge snap   = SnapEdge::Left;
    if (!load_pos(pos_x, pos_y, snap)) {
        const POINT org = leftmost_monitor_pos();
        pos_x = org.x;
        pos_y = org.y;
    }

    // Safety: if the saved position is completely off-screen (e.g. monitor was
    // removed or window was dragged out of the virtual desktop), reset to the
    // leftmost monitor and clear snap so the user can reposition it.
    const bool init_horiz0 = is_horiz_edge(snap);
    const int  probe_w = init_horiz0 ? k_h : k_w;
    const int  probe_h = init_horiz0 ? k_w : k_h;
    {
        const POINT safe = ensure_visible(pos_x, pos_y, probe_w, probe_h);
        if (safe.x != pos_x || safe.y != pos_y) {
            pos_x = safe.x;
            pos_y = safe.y;
            snap  = SnapEdge::Left;   // default to left-edge snap on reset
        }
    }

    WndState state;
    state.cbs         = callbacks;
    state.spy_on      = cfg.spy_on;
    state.stt_on      = cfg.stt_on;
    state.tts_on      = cfg.tts_on;
    state.full_x      = pos_x;
    state.full_y      = pos_y;
    state.snap_edge   = snap;

    // Create with correct dimensions for the restored snap orientation.
    const bool init_horiz = is_horiz_edge(snap);
    const int  init_w     = init_horiz ? k_h : k_w;
    const int  init_h     = init_horiz ? k_w : k_h;

    // WS_EX_NOACTIVATE: mouse events still arrive but the bar never steals focus,
    // so GetForegroundWindow() always returns the user's actual app window.
    HWND hwnd = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        k_wnd_class, L"PM-Image Assistant",
        WS_POPUP,
        pos_x, pos_y, init_w, init_h,
        nullptr, nullptr, hinst, nullptr);

    if (!hwnd) {
        Gdiplus::GdiplusShutdown(gdip_token);
        return -1;
    }
    impl_->hwnd = hwnd;

    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    repaint_ulw(hwnd);
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    if (snap != SnapEdge::None)
        ::SetTimer(hwnd, TIMER_IDLE, TIMER_IDLE_MS, nullptr);

    MSG msg = {};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    impl_->hwnd = nullptr;
    Gdiplus::GdiplusShutdown(gdip_token);
    return static_cast<int>(msg.wParam);
}

void AssistantBar::request_close() {
    if (impl_->hwnd) ::PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}
void AssistantBar::set_spy_state(bool on) {
    if (impl_->hwnd) ::PostMessageW(impl_->hwnd, WM_ASST_SET_SPY, on ? 1 : 0, 0);
}
void AssistantBar::set_stt_state(bool on) {
    if (impl_->hwnd) ::PostMessageW(impl_->hwnd, WM_ASST_SET_STT, on ? 1 : 0, 0);
}
void AssistantBar::set_tts_state(bool on) {
    if (impl_->hwnd) ::PostMessageW(impl_->hwnd, WM_ASST_SET_TTS, on ? 1 : 0, 0);
}

} // namespace media::assistant

#endif // _WIN32 && FEATURE_ASSISTANT
