// CWebView.cpp — flexible WebView2 wrapper (popup / docked, host comms, console bridge).
//
// Compile-gated: only added to sources when FEATURE_BROWSER=ON in CMakeLists.txt.
//
// Architecture mirrors ViewerWebPanel.cpp / ChatWebPanel.cpp:
//   • PIMPL (struct Impl) keeps WebView2 COM types out of the header.
//   • All WebView2 COM calls happen on the UI thread (the window's message pump).
//   • Worker threads must PostMessage to bounce back before touching COM state.
//   • WebView2 controller created asynchronously in OnCreate via env + controller callbacks.
//   • Console bridge injected via AddScriptToExecuteOnDocumentCreated on every page.
//   • Host → web: PostToWeb(json) → PostWebMessageAsString; page receives via cwWebOnHostMessage.
//   • Web → host: chrome.webview.postMessage(json) → add_WebMessageReceived → onMessage callback.
//
#include "stdafx.h"
#include "win/web/CWebView.h"
#include "win/web/webview_bootstrap.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/theme.hpp"
#include "logger/logger.h"
#include "win/settings_store.hpp"

#include <CommCtrl.h>
#include <WebView2.h>
#include <dwmapi.h>
#include <oleidl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <windowsx.h>
#include <wrl.h>
#include <wrl/client.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "Shlwapi.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

// ── Timer IDs ────────────────────────────────────────────────────────────────

constexpr UINT_PTR kTimerChromeResync = 0x63776231u; // "cwb1"
constexpr UINT     UWM_CWEB_POST_WEB_MSG = WM_APP + 93;

// ── Borderless popup metrics (must stay in sync with k_transparent_hello_html) ─
// Title bar height and caption-button widths are shared between WM_NCHITTEST and
// the HTML layout so the OS hit-zones overlay the rendered buttons exactly.
constexpr int kWvTitleH   = 32;  // px — #titlebar height in HTML
constexpr int kWvBtnW     = 46;  // px — each caption button width (min/max/close)
constexpr int kWvBorderPx = 6;   // px — WS_THICKFRAME resize-edge hit band
constexpr int kWvColorKeyInsetPx = 2; // tiny host edge strip so resize works in COLORKEY mode
// COLORKEY used by transparent popup modes. Keep this far from typical UI colors
// (matching the reference sample) so compositor quantization does not collapse it
// toward black and break key matching.
static constexpr COLORREF kTransColorKey = RGB(0xDF, 0xFE, 0xEF);

// ── DWM chrome helper ─────────────────────────────────────────────────────────

static void cweb_apply_dwm_chrome(HWND hwnd, bool keep_shadow)
{
    // Keep NC rendering enabled when we explicitly want compositor shadow.
    int ncPolicy = keep_shadow ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
    (void)DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

    // Remove the 1-px system border (Windows 10 1809+).
    const COLORREF noColor = static_cast<COLORREF>(0xFFFFFFFEu);
    (void)DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &noColor, sizeof(noColor));

    // Rounded corners (Windows 11+).
    const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    (void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    // Keep a small compositor margin when requested so native shadow can render.
    const MARGINS m = keep_shadow ? MARGINS{2, 2, 2, 2} : MARGINS{};
    (void)DwmExtendFrameIntoClientArea(hwnd, &m);
}

// ── Logging helpers ───────────────────────────────────────────────────────────
static void cweb_log_info(const std::string& s)  { logger::info(s); }
static void cweb_log_trace(const std::string& s)  { logger::trace(s); }
static void cweb_log_warn(const std::string& s)  { logger::warn(s); }
static void cweb_log_error(const std::string& s) { logger::error(s); }

static DWORD64 cweb_now_ms() { return ::GetTickCount64(); }

static std::string cweb_u8_clip(std::string_view u8, std::size_t max = 1500)
{
    if (u8.size() <= max) return std::string(u8);
    return std::string(u8.substr(0, max)) + "…(" + std::to_string(u8.size()) + "B)";
}

static std::string cweb_u8_from_wide(const std::wstring& w)
{
    return pmui::wide_to_utf8(w);
}

static fs::path cweb_module_exe_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size())
        return {};
    buf.resize(n);
    return fs::path(buf).parent_path();
}

static std::vector<fs::path> cweb_shared_file_candidates(const wchar_t* name)
{
    const fs::path exe = cweb_module_exe_dir();
    std::vector<fs::path> candidates;
    if (!exe.empty()) {
        if (exe.filename() == L"win-x64")
            candidates.push_back(exe.parent_path() / L"shared" / name);
        candidates.push_back(exe / L"shared" / name);
        candidates.push_back(exe / name);
    }
    try {
        candidates.push_back(fs::current_path() / L"dist" / L"shared" / name);
    } catch (...) {
    }
    return candidates;
}

static std::string cweb_default_global_css()
{
    return R"css(
:root {
  color-scheme: light dark;
  --pm-cweb-font-family: "Segoe UI", system-ui, -apple-system, BlinkMacSystemFont, Roboto, Arial, sans-serif;
  --pm-cweb-bg: #f8fafc;
  --pm-cweb-fg: #0f172a;
}

:root.dark,
:root[data-theme="dark"] {
  --pm-cweb-bg: #1e1e1e;
  --pm-cweb-fg: #e4e4e7;
}

html,
body {
  min-height: 100%;
  margin: 0;
  background: var(--pm-cweb-bg);
  color: var(--pm-cweb-fg);
  font-family: var(--pm-cweb-font-family);
  font-size: 14px;
  line-height: 1.45;
  text-rendering: optimizeLegibility;
  -webkit-font-smoothing: antialiased;
}
)css";
}

static std::string cweb_load_global_css()
{
    for (const auto& candidate : cweb_shared_file_candidates(L"global.css")) {
        try {
            std::ifstream in(candidate, std::ios::binary);
            if (!in)
                continue;
            std::string css(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            if (!css.empty())
                return css;
        } catch (...) {
            cweb_log_warn(std::string("[cweb] failed to read global.css: ")
                + cweb_u8_from_wide(candidate.wstring()));
        }
    }
    return cweb_default_global_css();
}

static std::wstring cweb_global_style_bootstrap_js(bool useColorKey)
{
    std::string surface_css = cweb_load_global_css();
    if (useColorKey) {
        surface_css += "\nhtml,body{background:rgba("
            + std::to_string(GetRValue(kTransColorKey)) + ","
            + std::to_string(GetGValue(kTransColorKey)) + ","
            + std::to_string(GetBValue(kTransColorKey)) + ",0)!important;}\n";
    }

    const std::string css_json = nlohmann::json(surface_css).dump();
    const std::string js =
        "(function(){try{"
        "var de=document.documentElement;"
        "if(de&&!de.hasAttribute('data-theme')){"
        "var dark=false;try{dark=!!window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;}catch(e){}"
        "de.classList.toggle('dark',dark);"
        "}"
        "if(!document.getElementById('__pm_cweb_global_css')){"
        "var s=document.createElement('style');s.id='__pm_cweb_global_css';s.textContent=" + css_json + ";"
        "(document.head||de||document.documentElement).appendChild(s);"
        "}"
        "}catch(e){}})();";
    return pmui::utf8_to_wide(js);
}

static HWND cweb_find_largest_chromium_render_widget_under(HWND host)
{
    struct Ctx { HWND best{}; LONG bestArea{}; };
    Ctx ctx{};
    auto enumProc = +[](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t cls[64]{};
        if (::GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) <= 0)
            return TRUE;
        if (::wcsncmp(cls, L"Chrome_WidgetWin_", 17) != 0)
            return TRUE;
        RECT rc{};
        if (!::GetWindowRect(hwnd, &rc))
            return TRUE;
        const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
        if (area > c->bestArea) {
            c->bestArea = area;
            c->best = hwnd;
        }
        return TRUE;
    };
    ::EnumChildWindows(host, enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

// ── User-data folder ──────────────────────────────────────────────────────────

static std::wstring cweb_user_data_folder()
{
    try {
        fs::path p = media::settings::get_config_dir() / "web-browser";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p.wstring();
    } catch (...) {
        return {};
    }
}

// True for both borderless popup modes (PopupTransparent + PopupPure).
static bool cweb_is_borderless(CWebViewOptions::Mode m)
{
    return m == CWebViewOptions::Mode::PopupTransparent
        || m == CWebViewOptions::Mode::PopupPure;
}

static bool cweb_uses_colorkey(CWebViewOptions::Mode m)
{
    return m == CWebViewOptions::Mode::PopupTransparent
        || m == CWebViewOptions::Mode::PopupPure;
}

static int cweb_controller_inset(CWebViewOptions::Mode m)
{
    if (!cweb_is_borderless(m)) return 0;
    return cweb_uses_colorkey(m) ? kWvColorKeyInsetPx : kWvBorderPx;
}

static int cweb_controller_inset(const CWebViewOptions& o)
{
    if (!o.frame) return 0;
    return cweb_controller_inset(o.mode);
}

// ── WebView2 chrome helpers (mirrors ViewerWebPanel.cpp) ──────────────────────

// inset > 0 reserves a bare strip around the window edge (not covered by WebView2)
// so WM_NCHITTEST on the host HWND fires for resize edges.  Pass kWvBorderPx for
// PopupTransparent; leave 0 for Docked / Popup.
static void cweb_apply_bounds(ICoreWebView2Controller* ctrl, HWND host, int inset = 0)
{
    if (!ctrl || !host || !::IsWindow(host)) return;
    RECT rc{};
    ::GetClientRect(host, &rc);
    if (inset > 0 && !::IsZoomed(host)) {
        rc.left   += inset;
        rc.top    += inset;
        rc.right  -= inset;
        rc.bottom -= inset;
    }
    ctrl->put_Bounds(rc);
}

static void cweb_apply_theme_background(ICoreWebView2Controller* ctrl, bool transparent = false,
                                        bool useColorKey = false)
{
    if (!ctrl) return;
    ComPtr<ICoreWebView2Controller2> c2;
    if (FAILED(ctrl->QueryInterface(IID_PPV_ARGS(&c2))) || !c2) return;
    COREWEBVIEW2_COLOR col;
    if (transparent) {
        if (useColorKey) {
            // Match the reference implementation exactly:
            // alpha=0 + RGB=keycolor, then force html/body to rgba(key,0).
            col = {0,
                GetRValue(kTransColorKey),
                GetGValue(kTransColorKey),
                GetBValue(kTransColorKey)};
        } else {
            // Borderless non-COLORKEY mode: transparent areas reveal host surface.
            col = {0, 0, 0, 0};
        }
    } else {
        const auto& pal = pmui::theme_palette();
        col = {255,
            GetRValue(pal.web_surface_bg),
            GetGValue(pal.web_surface_bg),
            GetBValue(pal.web_surface_bg)};
    }
    c2->put_DefaultBackgroundColor(col);
}

static void cweb_apply_color_scheme(ICoreWebView2* webview)
{
    if (!webview) return;
    ComPtr<ICoreWebView2_13>     w13;
    ComPtr<ICoreWebView2Profile> prof;
    if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&w13))) || !w13) return;
    if (FAILED(w13->get_Profile(&prof)) || !prof) return;
    const bool dark = pmui::theme_palette().dark;
    const COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme =
        dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
             : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
    (void)prof->put_PreferredColorScheme(scheme);
}

// skip_host_chrome=true when the host window is a top-level popup that owns its own
// title bar — nuke/flatten helpers strip WS_BORDER bits (part of WS_CAPTION) from the
// host HWND, which removes the title bar.  For docked child windows they are fine.
static void cweb_full_chrome_resync(ICoreWebView2Controller* ctrl, HWND host,
                                    bool skip_host_chrome = false,
                                    bool borderless = false,
                                    bool transparentBg = false,
                                    bool useColorKey = false)
{
    if (!ctrl || !host || !::IsWindow(host)) return;
    ComPtr<ICoreWebView2> wv;
    if (SUCCEEDED(ctrl->get_CoreWebView2(&wv)) && wv)
        cweb_apply_color_scheme(wv.Get());
    const int inset = borderless ? (useColorKey ? kWvColorKeyInsetPx : kWvBorderPx) : 0;
    cweb_apply_bounds(ctrl, host, inset);
    cweb_apply_theme_background(ctrl, transparentBg, useColorKey);
    if (!skip_host_chrome && !useColorKey) {
        const auto& pal = pmui::theme_palette();
        pmui::nuke_webview2_host_chrome(host, pal.web_surface_bg);
        pmui::flatten_webview_host_parent_chain(host, pal.web_surface_bg, 16);
    }
    ::InvalidateRect(host, nullptr, TRUE);
}

// ── JS console bridge script ──────────────────────────────────────────────────
//
// Injected into every document via AddScriptToExecuteOnDocumentCreated.
// Wraps console.{log,warn,error,info} to post { t:"cweb_console", level, msg }
// back to the host. Also installs window.cwWebOnHostMessage as the receive hook
// for host → web messages dispatched by CWebView::PostToWeb.

static const char k_console_bridge_js[] = R"js(
(function(){
  var _post;
  try {
    var _wv = window.chrome && window.chrome.webview;
    if (_wv && typeof _wv.postMessage === 'function')
      _post = function(s){ _wv.postMessage(s); };
  } catch(e){}

  // Forward console lines to the host as { t:"cweb_console", level, msg }.
  function _intercept(level, orig) {
    return function() {
      if (typeof orig === 'function') orig.apply(console, arguments);
      if (!_post) return;
      try {
        var parts = [];
        for (var i = 0; i < arguments.length; i++) {
          try {
            var a = arguments[i];
            parts.push(typeof a === 'object' ? JSON.stringify(a) : String(a));
          } catch(e) { parts.push('[?]'); }
        }
        var msg = parts.join(' ');
        if (msg.length > 1800) msg = msg.slice(0, 1800) + '…';
        _post(JSON.stringify({ t: 'cweb_console', level: level, msg: msg }));
      } catch(e) {}
    };
  }
  console.log   = _intercept('log',   console.log);
  console.warn  = _intercept('warn',  console.warn);
  console.error = _intercept('error', console.error);
  console.info  = _intercept('info',  console.info);

  // Host → web: queue until the page registers cwWebSetHostMessageHandler
  // (cwWebOnHostMessage is a legacy alias for the same handler).
  var _hostHandler = null;
  var _pendingHostMessages = [];
  window.cwWebSetHostMessageHandler = function(fn) {
    _hostHandler = (typeof fn === 'function') ? fn : null;
    if (!_hostHandler) return;
    var pending = _pendingHostMessages.splice(0);
    for (var i = 0; i < pending.length; i++) {
      try { _hostHandler(pending[i]); } catch(e) {}
    }
  };
  try {
    Object.defineProperty(window, 'cwWebOnHostMessage', {
      configurable: true,
      enumerable: true,
      get: function() { return _hostHandler; },
      set: function(fn) { window.cwWebSetHostMessageHandler(fn); }
    });
  } catch(e) {}
  var _wv2 = window.chrome && window.chrome.webview;
  if (_wv2 && typeof _wv2.addEventListener === 'function') {
    _wv2.addEventListener('message', function(ev) {
      try {
        var data = (typeof ev.data === 'string') ? JSON.parse(ev.data) : ev.data;
        if (typeof _hostHandler === 'function')
          _hostHandler(data);
        else
          _pendingHostMessages.push(data);
      } catch(e) {}
    });
  }
})();
)js";

// ── Impl ─────────────────────────────────────────────────────────────────────

struct CWebView::Impl {
    HWND hwnd = nullptr;

    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    EventRegistrationToken          msgToken{};
    EventRegistrationToken          navCompletedToken{};
    EventRegistrationToken          acceleratorToken{};
    bool                            msg_hook_registered  = false;
    bool                            nav_hook_registered  = false;
    bool                            accelerator_hook_registered = false;

    bool ready     = false;  ///< WebView2 controller + webview obtained and configured.
    bool navigated = false;  ///< Initial navigation has been dispatched.
    bool navigationComplete = false; ///< Current document can receive PostWebMessageAsString.
    bool zero_rect_defer_logged = false;
    DWORD64 create_start_ms = 0;
    DWORD64 env_ready_ms = 0;
    DWORD64 nav_start_ms = 0;

    // Pending queue: scripts that arrived before `ready`.
    std::vector<std::wstring> pendingScripts;
    std::vector<std::string>  pendingWebMessages;

    // Navigation target (set by Navigate / NavigateHtml / SetOptions before ready).
    std::wstring  pendingUrl;
    std::string   pendingHtml;
    bool          pendingIsHtml = false;

    // Current options.
    CWebViewOptions opts;

    // Native shell CF_HDROP support (IDropTarget on the Chromium child hwnd).
    HWND         shell_drop_hwnd   = nullptr;
    HWND         shell_drop_foreign_hwnd = nullptr;
    IDropTarget* shell_drop_target = nullptr;
    bool         shell_drag_valid  = false;
    HWND         modal_owner_hwnd  = nullptr;

    void PostDropDebug(const std::string& phase, bool valid, int count = -1)
    {
        nlohmann::json j;
        j["t"] = "debug";
        j["kind"] = "drop";
        j["phase"] = phase;
        j["valid"] = valid;
        if (count >= 0) j["count"] = count;
        const std::string dump = j.dump();
        if (opts.onMessage) opts.onMessage(dump);
        else cweb_log_trace(std::string("[cweb] drop-debug: ") + dump);
    }

    bool AcceptsDropAt(POINT ptHost) const
    {
        // "valid drag event" gate: in pure mode accept anywhere; in titled mode
        // avoid hijacking the non-client title row where host buttons/drag live.
        if (opts.mode == CWebViewOptions::Mode::PopupTransparent) {
            return ptHost.y >= kWvTitleH;
        }
        return true;
    }

    void PostDroppedPathsToWeb(const std::vector<std::wstring>& paths)
    {
        if (!webview || paths.empty()) return;
        nlohmann::json j;
        j["t"] = "host_file_drop";
        j["paths"] = nlohmann::json::array();
        for (const auto& p : paths)
            j["paths"].push_back(cweb_u8_from_wide(p));
        const std::wstring w = pmui::utf8_to_wide(j.dump());
        (void)webview->PostWebMessageAsString(w.c_str());
    }

    void RunJsNow(const std::wstring& js)
    {
        if (!webview) return;
        webview->ExecuteScript(js.c_str(), nullptr);
    }

    void QueueOrRunJs(const std::wstring& js)
    {
        if (ready) RunJsNow(js);
        else pendingScripts.push_back(js);
    }

    void FlushPendingScripts()
    {
        for (const auto& js : pendingScripts)
            RunJsNow(js);
        pendingScripts.clear();
    }

    void FlushPendingWebMessages()
    {
        if (!webview || pendingWebMessages.empty())
            return;
        auto pending = std::move(pendingWebMessages);
        pendingWebMessages.clear();
        for (const auto& msg : pending) {
            const std::wstring w = pmui::utf8_to_wide(msg);
            (void)webview->PostWebMessageAsString(w.c_str());
        }
    }

    /// Apply virtual-host mapping (localFolder → vhostName) to the webview.
    void ApplyVhostMapping()
    {
        if (!webview || opts.localFolder.empty() || opts.vhostName.empty())
            return;
        ComPtr<ICoreWebView2>   w0(webview);
        ComPtr<ICoreWebView2_3> v3;
        if (FAILED(w0.As(&v3)) || !v3)
            return;
        const HRESULT hr = v3->SetVirtualHostNameToFolderMapping(
            opts.vhostName.c_str(),
            opts.localFolder.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        if (FAILED(hr))
            cweb_log_warn(std::string("[cweb] SetVirtualHostNameToFolderMapping failed: 0x")
                + std::to_string(static_cast<uint32_t>(hr)));
        else
            cweb_log_trace(std::string("[cweb] vhost mapped: ")
                + pmui::wide_to_utf8(opts.vhostName));
    }

    void ApplyFixedDriveFileHostMappings()
    {
        if (!webview || !opts.mapFixedDriveFileHosts)
            return;
        ComPtr<ICoreWebView2>   w0(webview);
        ComPtr<ICoreWebView2_3> v3;
        if (FAILED(w0.As(&v3)) || !v3)
            return;
        const DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (!(mask & (1u << i)))
                continue;
            wchar_t root[4] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', 0 };
            const UINT driveType = ::GetDriveTypeW(root);
            if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOTE)
                continue;
            wchar_t host[24] = {};
            swprintf_s(host, L"pm-files-%c.local", static_cast<wchar_t>(L'a' + i));
            const HRESULT hr = v3->SetVirtualHostNameToFolderMapping(
                host, root, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
            if (FAILED(hr)) {
                cweb_log_warn(std::string("[cweb] pm-files drive mapping failed for ")
                    + static_cast<char>('A' + i) + ": 0x" + std::to_string(static_cast<uint32_t>(hr)));
            }
        }
    }

    /// Clear the virtual-host mapping when options change.
    void ClearVhostMapping(const std::wstring& vhostName)
    {
        if (!webview || vhostName.empty())
            return;
        ComPtr<ICoreWebView2>   w0(webview);
        ComPtr<ICoreWebView2_3> v3;
        if (FAILED(w0.As(&v3)) || !v3)
            return;
        (void)v3->ClearVirtualHostNameToFolderMapping(vhostName.c_str());
    }

    void DoNavigate()
    {
        if (!webview || navigated) return;

        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        if (rc.right <= 1 || rc.bottom <= 1) {
            if (!zero_rect_defer_logged) {
                zero_rect_defer_logged = true;
                cweb_log_trace(std::string("[cweb-open] DoNavigate deferred: zero/small rect ")
                    + std::to_string(rc.right) + "x" + std::to_string(rc.bottom)
                    + " +" + std::to_string(cweb_now_ms() - create_start_ms) + " ms");
            }
            return;  // defer until we have a real rect
        }

        if (controller) {
            cweb_apply_bounds(controller.Get(), hwnd, cweb_controller_inset(opts));
            controller->put_IsVisible(TRUE);
        }

        nav_start_ms = cweb_now_ms();
        if (!pendingIsHtml && !pendingUrl.empty()) {
            cweb_log_trace(std::string("[cweb-open] Navigate → ")
                + cweb_u8_clip(pmui::wide_to_utf8(pendingUrl), 300)
                + " rect=" + std::to_string(rc.right) + "x" + std::to_string(rc.bottom)
                + " +" + std::to_string(nav_start_ms - create_start_ms) + " ms");
            webview->Navigate(pendingUrl.c_str());
        } else if (pendingIsHtml && !pendingHtml.empty()) {
            cweb_log_trace(std::string("[cweb-open] NavigateToString inline HTML bytes=")
                + std::to_string(pendingHtml.size())
                + " rect=" + std::to_string(rc.right) + "x" + std::to_string(rc.bottom)
                + " +" + std::to_string(nav_start_ms - create_start_ms) + " ms");
            const std::wstring whtml = pmui::utf8_to_wide(pendingHtml);
            webview->NavigateToString(whtml.c_str());
        } else {
            cweb_log_trace("[cweb] DoNavigate: nothing to navigate to");
            return;
        }
        navigated = true;
        navigationComplete = false;
    }

    void EnsureShellFileDropTarget()
    {
        if (!hwnd || !::IsWindow(hwnd) || !controller) return;
        if (shell_drop_hwnd && !::IsWindow(shell_drop_hwnd)) {
            shell_drop_hwnd = nullptr;
        }
        HWND dropHwnd = cweb_find_largest_chromium_render_widget_under(hwnd);
        if (!dropHwnd) return;
        if (dropHwnd == shell_drop_hwnd && shell_drop_target) return;
        if (dropHwnd == shell_drop_foreign_hwnd) return;
        if (shell_drop_hwnd && ::IsWindow(shell_drop_hwnd)) {
            (void)::RevokeDragDrop(shell_drop_hwnd);
            shell_drop_hwnd = nullptr;
        }
        if (!shell_drop_target) {
            class CWebShellDropTarget final : public IDropTarget {
            public:
                explicit CWebShellDropTarget(Impl* impl) : impl_(impl) {}
                HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
                {
                    if (!ppv) return E_POINTER;
                    *ppv = nullptr;
                    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
                        *ppv = static_cast<IDropTarget*>(this);
                        AddRef();
                        return S_OK;
                    }
                    return E_NOINTERFACE;
                }
                ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
                ULONG STDMETHODCALLTYPE Release() override
                {
                    const LONG c = InterlockedDecrement(&ref_);
                    if (c == 0) delete this;
                    return static_cast<ULONG>(c);
                }
                HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD, POINTL ptl, DWORD* eff) override
                {
                    if (!eff) return E_INVALIDARG;
                    *eff = DROPEFFECT_NONE;
                    has_hdrop_ = false;
                    if (!obj || !impl_) return S_OK;
                    FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
                    if (obj->QueryGetData(&fe) == S_OK) has_hdrop_ = true;
                    apply_drag_pt(ptl, eff);
                    return S_OK;
                }
                HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL ptl, DWORD* eff) override
                {
                    if (!eff) return E_INVALIDARG;
                    apply_drag_pt(ptl, eff);
                    return S_OK;
                }
                HRESULT STDMETHODCALLTYPE DragLeave() override
                {
                    has_hdrop_ = false;
                    if (impl_) impl_->shell_drag_valid = false;
                    if (impl_) impl_->PostDropDebug("leave", false);
                    return S_OK;
                }
                HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD, POINTL ptl, DWORD* eff) override
                {
                    if (!eff) return E_INVALIDARG;
                    *eff = DROPEFFECT_NONE;
                    if (!obj || !impl_) return S_OK;
                    POINT pt{ptl.x, ptl.y};
                    ::ScreenToClient(impl_->hwnd, &pt);
                    const bool valid = has_hdrop_ && impl_->AcceptsDropAt(pt);
                    impl_->shell_drag_valid = valid;
                    if (!valid) {
                        impl_->PostDropDebug("drop_reject", false);
                        has_hdrop_ = false;
                        return S_OK;
                    }
                    FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
                    STGMEDIUM st{};
                    if (FAILED(obj->GetData(&fe, &st))) {
                        impl_->PostDropDebug("drop_nodata", false);
                        has_hdrop_ = false;
                        return S_OK;
                    }
                    std::vector<std::wstring> paths;
                    if (st.tymed == TYMED_HGLOBAL && st.hGlobal) {
                        if (void* locked = ::GlobalLock(st.hGlobal)) {
                            const auto hdrop = static_cast<HDROP>(locked);
                            const UINT n = ::DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
                            paths.reserve(static_cast<size_t>(n));
                            for (UINT i = 0; i < n; ++i) {
                                wchar_t buf[MAX_PATH * 4]{};
                                if (::DragQueryFileW(hdrop, i, buf, static_cast<UINT>(std::size(buf))))
                                    paths.emplace_back(buf);
                            }
                            ::GlobalUnlock(st.hGlobal);
                        }
                    }
                    ::ReleaseStgMedium(&st);
                    has_hdrop_ = false;
                    if (paths.empty()) {
                        impl_->PostDropDebug("drop_empty", false);
                        return S_OK;
                    }
                    impl_->PostDropDebug("drop_accept", true, static_cast<int>(paths.size()));
                    impl_->PostDroppedPathsToWeb(paths);
                    *eff = DROPEFFECT_COPY;
                    return S_OK;
                }
            private:
                void apply_drag_pt(POINTL ptl, DWORD* eff)
                {
                    *eff = DROPEFFECT_NONE;
                    if (!impl_) return;
                    POINT pt{ptl.x, ptl.y};
                    ::ScreenToClient(impl_->hwnd, &pt);
                    const bool valid = has_hdrop_ && impl_->AcceptsDropAt(pt);
                    impl_->shell_drag_valid = valid;
                    if (valid) *eff = DROPEFFECT_COPY;
                    impl_->PostDropDebug("over", valid);
                }
                LONG  ref_ = 1;
                Impl* impl_ = nullptr;
                bool  has_hdrop_ = false;
            };
            shell_drop_target = new CWebShellDropTarget(this);
        }
        const HRESULT hr = ::RegisterDragDrop(dropHwnd, shell_drop_target);
        if (FAILED(hr)) {
            if (hr == DRAGDROP_E_ALREADYREGISTERED) {
                shell_drop_foreign_hwnd = dropHwnd;
                cweb_log_trace("[cweb] native shell drop target skipped: Chromium child already registered");
            } else {
                char buf[96];
                snprintf(buf, sizeof(buf), "[cweb] RegisterDragDrop failed: 0x%08lX",
                    static_cast<unsigned long>(static_cast<uint32_t>(hr)));
                cweb_log_warn(buf);
            }
            return;
        }
        shell_drop_hwnd = dropHwnd;
        shell_drop_foreign_hwnd = nullptr;
    }

    void RevokeShellFileDropTarget()
    {
        if (shell_drop_hwnd && ::IsWindow(shell_drop_hwnd))
            (void)::RevokeDragDrop(shell_drop_hwnd);
        shell_drop_hwnd = nullptr;
        shell_drop_foreign_hwnd = nullptr;
        if (shell_drop_target) {
            shell_drop_target->Release();
            shell_drop_target = nullptr;
        }
    }
};

// ── CWebView ──────────────────────────────────────────────────────────────────

CWebView::CWebView() : m_impl(std::make_unique<Impl>())
{
    char buf[64]; snprintf(buf, sizeof(buf), "[cweb] ctor @%p", static_cast<void*>(this));
    cweb_log_trace(buf);
}

CWebView::~CWebView()
{
    char buf[64]; snprintf(buf, sizeof(buf), "[cweb] dtor @%p", static_cast<void*>(this));
    cweb_log_trace(buf);

    if (HWND h = GetHwnd(); h && ::IsWindow(h))
        ::KillTimer(h, kTimerChromeResync);

    if (m_impl) {
        if (m_impl->modal_owner_hwnd && ::IsWindow(m_impl->modal_owner_hwnd)) {
            ::EnableWindow(m_impl->modal_owner_hwnd, TRUE);
            if (::IsIconic(m_impl->modal_owner_hwnd))
                ::ShowWindow(m_impl->modal_owner_hwnd, SW_RESTORE);
            ::SetWindowPos(m_impl->modal_owner_hwnd, HWND_TOP, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            ::SetForegroundWindow(m_impl->modal_owner_hwnd);
            ::SetActiveWindow(m_impl->modal_owner_hwnd);
            m_impl->modal_owner_hwnd = nullptr;
        }
        m_impl->RevokeShellFileDropTarget();
        if (m_impl->webview) {
            if (m_impl->msg_hook_registered)
                m_impl->webview->remove_WebMessageReceived(m_impl->msgToken);
            if (m_impl->nav_hook_registered)
                m_impl->webview->remove_NavigationCompleted(m_impl->navCompletedToken);
            m_impl->webview->Stop();
        }
        if (m_impl->controller && m_impl->accelerator_hook_registered)
            (void)m_impl->controller->remove_AcceleratorKeyPressed(m_impl->acceleratorToken);
        if (m_impl->controller)
            m_impl->controller->Close();
    }
}

void CWebView::SetOptions(CWebViewOptions opts)
{
    const std::wstring oldVhost = m_impl->opts.vhostName;

    // If vhost changed and WebView2 is already ready, clear the old mapping.
    if (m_impl->ready && !oldVhost.empty() && oldVhost != opts.vhostName)
        m_impl->ClearVhostMapping(oldVhost);

    m_impl->opts = std::move(opts);

    // Seed initial navigation target from new options if not already set.
    if (!m_impl->opts.url.empty()) {
        m_impl->pendingUrl    = m_impl->opts.url;
        m_impl->pendingIsHtml = false;
    } else if (!m_impl->opts.html.empty()) {
        m_impl->pendingHtml   = m_impl->opts.html;
        m_impl->pendingIsHtml = true;
    }

    if (m_impl->ready) {
        m_impl->ApplyVhostMapping();
        m_impl->navigated = false;
        m_impl->navigationComplete = false;
        m_impl->DoNavigate();
    }
}

void CWebView::Navigate(const std::wstring& url)
{
    m_impl->pendingUrl    = url;
    m_impl->pendingIsHtml = false;
    if (m_impl->ready) {
        m_impl->navigated = false;
        m_impl->navigationComplete = false;
        m_impl->DoNavigate();
    }
}

void CWebView::NavigateHtml(const std::string& html_utf8)
{
    m_impl->pendingHtml   = html_utf8;
    m_impl->pendingIsHtml = true;
    if (m_impl->ready) {
        m_impl->navigated = false;
        m_impl->navigationComplete = false;
        m_impl->DoNavigate();
    }
}

void CWebView::PostToWeb(const std::string& json_utf8)
{
    if (!m_impl->webview || !m_impl->navigationComplete) {
        m_impl->pendingWebMessages.push_back(json_utf8);
        return;
    }
    const std::wstring w = pmui::utf8_to_wide(json_utf8);
    (void)m_impl->webview->PostWebMessageAsString(w.c_str());
}

void CWebView::PostToWebFromAnyThread(std::string json_utf8)
{
    const HWND hwnd = GetHwnd();
    if (!hwnd || !::IsWindow(hwnd))
        return;
    auto* payload = new std::string(std::move(json_utf8));
    if (!::PostMessageW(hwnd, UWM_CWEB_POST_WEB_MSG, 0, reinterpret_cast<LPARAM>(payload)))
        delete payload;
}

void CWebView::RunScript(const std::wstring& js)
{
    m_impl->QueueOrRunJs(js);
}

void CWebView::RefreshChromeForTheme()
{
    if (!m_impl || !m_impl->controller) return;
    const bool isPopup  = (m_impl->opts.mode != CWebViewOptions::Mode::Docked);
    const bool isBorderless = cweb_is_borderless(m_impl->opts.mode);
    const bool transparentBg = cweb_uses_colorkey(m_impl->opts.mode);
    cweb_full_chrome_resync(m_impl->controller.Get(), GetHwnd(), isPopup, isBorderless && m_impl->opts.frame, transparentBg,
        cweb_uses_colorkey(m_impl->opts.mode));
}

void CWebView::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);

    const CWebViewOptions::Mode mode = m_impl ? m_impl->opts.mode : CWebViewOptions::Mode::Docked;
    const HWND requestedOwner = cs.hwndParent;
    const bool useModalOwner = m_impl && m_impl->opts.modal && requestedOwner && ::IsWindow(requestedOwner);

    if (mode == CWebViewOptions::Mode::Docked) {
        // Child window: strip all visible decoration so the WebView2 surface is flush.
        cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
        cs.style     &= ~(WS_BORDER | WS_DLGFRAME);
    } else if (mode == CWebViewOptions::Mode::Popup) {
        // Standard overlapped window — system title bar, resize border, taskbar button.
        cs.style      = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        cs.dwExStyle  = WS_EX_APPWINDOW;
        cs.hwndParent = useModalOwner ? requestedOwner : nullptr;
        if (useModalOwner)
            cs.dwExStyle &= ~WS_EX_APPWINDOW;
        if (!cs.lpszName || cs.lpszName[0] == L'\0')
            cs.lpszName = L"Web Browser";
    } else {
        // PopupTransparent / PopupPure — borderless resizable popup.
        // PopupTransparent adds WS_MINIMIZEBOX|WS_MAXIMIZEBOX because the HTML title bar
        // includes those buttons. PopupPure omits them (web app owns all chrome).
        const bool hasCaption = (mode == CWebViewOptions::Mode::PopupTransparent);
        const bool useColorKey = cweb_uses_colorkey(mode);
        const bool useFrame = m_impl && m_impl->opts.frame;
        const bool useClickThrough = m_impl && m_impl->opts.clickthrough && !(m_impl->opts.modal);
        cs.style      = WS_POPUP | (useFrame ? WS_THICKFRAME : 0u) | WS_SYSMENU
                      | (useColorKey ? 0u : (WS_CLIPCHILDREN | WS_CLIPSIBLINGS))
                      | (hasCaption ? (WS_MINIMIZEBOX | WS_MAXIMIZEBOX) : 0u);
        cs.dwExStyle  = WS_EX_APPWINDOW
                      | (useColorKey ? WS_EX_LAYERED : 0u)
                      | (useClickThrough ? WS_EX_TRANSPARENT : 0u);
        cs.hwndParent = useModalOwner ? requestedOwner : nullptr;
        if (useModalOwner)
            cs.dwExStyle &= ~WS_EX_APPWINDOW;
    }
}

int CWebView::OnCreate(CREATESTRUCT&)
{
    m_impl->hwnd = GetHwnd();
    m_impl->create_start_ms = cweb_now_ms();
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "[cweb-open] OnCreate begin impl=%p hwnd=%p",
            static_cast<void*>(m_impl.get()), reinterpret_cast<void*>(m_impl->hwnd));
        cweb_log_trace(buf);
    }

    if (m_impl->opts.modal) {
        HWND owner = ::GetWindow(GetHwnd(), GW_OWNER);
        if (!owner) owner = ::GetParent(GetHwnd());
        if (!owner) owner = ::GetAncestor(GetHwnd(), GA_ROOTOWNER);
        if (owner && owner != GetHwnd() && ::IsWindow(owner) && ::IsWindowEnabled(owner)) {
            m_impl->modal_owner_hwnd = owner;
            ::EnableWindow(owner, FALSE);
        } else {
            cweb_log_warn("[cweb] modal requested but no explicit owner; skipping owner-disable");
        }
    }

    // Non-COLORKEY popups use DWM chrome. COLORKEY popups avoid DWM composition
    // tweaks (mirroring the reference sample) to prevent color transform artifacts.
    if (m_impl->opts.mode != CWebViewOptions::Mode::Docked &&
        !cweb_uses_colorkey(m_impl->opts.mode)) {
        cweb_apply_dwm_chrome(GetHwnd(), m_impl->opts.keepDwmShadow);
        ::SetWindowPos(GetHwnd(), nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (cweb_uses_colorkey(m_impl->opts.mode)) {
        // Enforce layered style at runtime too (ref sample relies on this),
        // then apply COLORKEY transparency.
        const LONG_PTR ex = ::GetWindowLongPtrW(GetHwnd(), GWL_EXSTYLE);
        ::SetWindowLongPtrW(GetHwnd(), GWL_EXSTYLE, ex | WS_EX_LAYERED);
        ::SetLayeredWindowAttributes(GetHwnd(), kTransColorKey, 0, LWA_COLORKEY);
    }
    // modal + clickthrough is contradictory for interactive UIs.
    // When modal is requested, keep the popup clickable.
    if (m_impl->opts.clickthrough && !m_impl->opts.modal) {
        const LONG_PTR ex = ::GetWindowLongPtrW(GetHwnd(), GWL_EXSTYLE);
        ::SetWindowLongPtrW(GetHwnd(), GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    }

    const std::wstring userData = cweb_user_data_folder();
    if (userData.empty()) {
        cweb_log_error("[cweb] OnCreate: could not resolve WebView2 user-data folder");
        return 0;
    }
    cweb_log_trace(std::string("[cweb-open] user-data folder=")
        + cweb_u8_clip(pmui::wide_to_utf8(userData), 500)
        + " +" + std::to_string(cweb_now_ms() - m_impl->create_start_ms) + " ms");

    HWND  host = m_impl->hwnd;
    Impl* impl = m_impl.get();  // raw ptr — safe: WebView2 lifetime ⊆ CWebView lifetime

    const HRESULT hrEnv = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [host, impl](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                const DWORD64 now = cweb_now_ms();
                impl->env_ready_ms = now;
                cweb_log_trace(std::string("[cweb-open] environment callback hr=0x")
                    + std::to_string(static_cast<uint32_t>(hr))
                    + " +" + std::to_string(now - impl->create_start_ms) + " ms");
                if (FAILED(hr) || !env) {
                    cweb_log_error(std::string("[cweb] CreateCoreWebView2Environment failed: 0x")
                        + std::to_string(static_cast<uint32_t>(hr)));
                    return S_OK;
                }
                return env->CreateCoreWebView2Controller(host,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [host, impl](HRESULT hr2, ICoreWebView2Controller* ctrl) -> HRESULT {
                            const DWORD64 controllerReady = cweb_now_ms();
                            cweb_log_trace(std::string("[cweb-open] controller callback hr=0x")
                                + std::to_string(static_cast<uint32_t>(hr2))
                                + " +" + std::to_string(controllerReady - impl->create_start_ms) + " ms"
                                + " env→controller=" + std::to_string(
                                    impl->env_ready_ms ? (controllerReady - impl->env_ready_ms) : 0)
                                + " ms");
                            if (FAILED(hr2) || !ctrl) {
                                cweb_log_error(std::string("[cweb] CreateCoreWebView2Controller failed: 0x")
                                    + std::to_string(static_cast<uint32_t>(hr2)));
                                return S_OK;
                            }
                            impl->controller = ctrl;
                            ctrl->get_CoreWebView2(&impl->webview);
                            if (!impl->webview) {
                                cweb_log_error("[cweb] get_CoreWebView2 returned null");
                                return S_OK;
                            }

                            // Allow drag-drop.
                            {
                                ComPtr<ICoreWebView2Controller4> c4;
                                if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&c4))) && c4)
                                    (void)c4->put_AllowExternalDrop(TRUE);
                            }

                                    {
                                        cweb_apply_bounds(ctrl, host, cweb_controller_inset(impl->opts));
                                        cweb_apply_theme_background(ctrl, cweb_uses_colorkey(impl->opts.mode),
                                            cweb_uses_colorkey(impl->opts.mode));
                                    }

                                    // Settings.
                            ComPtr<ICoreWebView2Settings> s;
                            impl->webview->get_Settings(&s);
                            if (s) {
                                s->put_AreDevToolsEnabled(impl->opts.devToolsEnabled ? TRUE : FALSE);
                                s->put_AreDefaultContextMenusEnabled(
                                    impl->opts.defaultContextMenusEnabled ? TRUE : FALSE);
                                s->put_IsZoomControlEnabled(TRUE);
                                s->put_IsStatusBarEnabled(FALSE);
                                s->put_AreHostObjectsAllowed(FALSE);
                                ComPtr<ICoreWebView2Settings6> s6;
                                if (SUCCEEDED(s.As(&s6)) && s6)
                                    (void)s6->put_IsWebMessageEnabled(TRUE);

                                // Borderless popup: enable CSS -webkit-app-region so
                                // the HTML title bar (with -webkit-app-region:drag)
                                // forwards WM_NCLBUTTONDOWN(HTCAPTION) to this host
                                // window → OS handles drag natively.
                                if (cweb_is_borderless(impl->opts.mode)) {
                                    ComPtr<ICoreWebView2Settings9> s9;
                                    if (SUCCEEDED(s.As(&s9)) && s9)
                                        (void)s9->put_IsNonClientRegionSupportEnabled(TRUE);
                                }
                            }

                            cweb_apply_color_scheme(impl->webview.Get());

                            // Inject console bridge into every document.
                            const std::wstring bridgeJs = pmui::utf8_to_wide(k_console_bridge_js);
                            impl->webview->AddScriptToExecuteOnDocumentCreated(
                                bridgeJs.c_str(), nullptr);

                            const std::wstring appFeaturesJs = pmui::webview_app_features_bootstrap_js();
                            impl->webview->AddScriptToExecuteOnDocumentCreated(
                                appFeaturesJs.c_str(), nullptr);

                            const std::wstring systemContextJs = pmui::webview_system_context_bootstrap_js();
                            impl->webview->AddScriptToExecuteOnDocumentCreated(
                                systemContextJs.c_str(), nullptr);

                            const std::wstring globalStyleJs =
                                cweb_global_style_bootstrap_js(cweb_uses_colorkey(impl->opts.mode));
                            impl->webview->AddScriptToExecuteOnDocumentCreated(
                                globalStyleJs.c_str(), nullptr);

                            // COLORKEY path: mirror ref implementation by forcing body/html
                            // to rgba(kTransColorKey, 0) on each document.
                            if (cweb_uses_colorkey(impl->opts.mode)) {
                                wchar_t set_bkgnd_color[256]{};
                                swprintf(set_bkgnd_color, 256,
                                    L"(function(){"
                                    L"var rs='rgba(%d,%d,%d,0)';"
                                    L"var d=document.documentElement;"
                                    L"var b=document.body;"
                                    L"if(d&&d.style){d.style.background=rs;d.style.backgroundColor=rs;}"
                                    L"if(b&&b.style){b.style.background=rs;b.style.backgroundColor=rs;}"
                                    L"})();",
                                    GetRValue(kTransColorKey),
                                    GetGValue(kTransColorKey),
                                    GetBValue(kTransColorKey));
                                impl->webview->AddScriptToExecuteOnDocumentCreated(
                                    set_bkgnd_color,
                                    nullptr);
                            }

                            // Optional auto-resize bridge: once content size is known, post
                            // a one-shot cweb_resize message to host.
                            if (impl->opts.allowWebResizeHostWindow
                                && impl->opts.autoResizeHostWindowFromWebContent) {
                                impl->webview->AddScriptToExecuteOnDocumentCreated(
                                    L"(function(){"
                                    L"if(window.__cwebAutoResizeInstalled) return;"
                                    L"window.__cwebAutoResizeInstalled = true;"
                                    L"var sent=false,lastW=0,lastH=0,tries=0,maxTries=80,t=null;"
                                    L"function sizeNow(){"
                                    L" var de=document.documentElement||{};"
                                    L" var b=document.body||{};"
                                    L" var w=Math.max(de.scrollWidth||0,de.offsetWidth||0,de.clientWidth||0,b.scrollWidth||0,b.offsetWidth||0,b.clientWidth||0);"
                                    L" var h=Math.max(de.scrollHeight||0,de.offsetHeight||0,de.clientHeight||0,b.scrollHeight||0,b.offsetHeight||0,b.clientHeight||0);"
                                    L" return {w:w|0,h:h|0};"
                                    L"}"
                                    L"function postSize(s,src){"
                                    L" try{"
                                    L"  if(window.chrome&&chrome.webview&&chrome.webview.postMessage){"
                                    L"   chrome.webview.postMessage(JSON.stringify({t:'cweb_resize',w:s.w,h:s.h,src:src||'auto'}));"
                                    L"  }"
                                    L" }catch(e){}"
                                    L"}"
                                    L"function tick(){"
                                    L" if(sent) return;"
                                    L" var s=sizeNow();"
                                    L" tries++;"
                                    L" if(s.w<16||s.h<16){ if(tries<maxTries) return; sent=true; return; }"
                                    L" if(s.w===lastW && s.h===lastH){"
                                    L"  postSize(s,'auto');"
                                    L"  sent=true;"
                                    L"  if(t){clearInterval(t);t=null;}"
                                    L"  return;"
                                    L" }"
                                    L" lastW=s.w; lastH=s.h;"
                                    L" if(tries>=maxTries){"
                                    L"  postSize(s,'auto_forced');"
                                    L"  sent=true;"
                                    L"  if(t){clearInterval(t);t=null;}"
                                    L" }"
                                    L"}"
                                    L"window.cwebAutoResizeNow=function(){"
                                    L" var s=sizeNow();"
                                    L" if(s.w>=16&&s.h>=16) postSize(s,'manual');"
                                    L"};"
                                    L"function start(){ if(t) return; t=setInterval(tick,50); tick(); }"
                                    L"if(document.readyState==='complete'||document.readyState==='interactive') start();"
                                    L"document.addEventListener('DOMContentLoaded', start, {once:true});"
                                    L"if(window.ResizeObserver){"
                                    L" try{"
                                    L"  var ro=new ResizeObserver(function(){ if(!sent) tick(); else ro.disconnect(); });"
                                    L"  ro.observe(document.documentElement);"
                                    L"  if(document.body) ro.observe(document.body);"
                                    L" }catch(e){}"
                                    L"}"
                                    L"setTimeout(start,60);"
                                    L"setTimeout(start,220);"
                                    L"})();",
                                    nullptr);
                            }

                            // Virtual host mappings for local folders and optional drive-backed file thumbnails.
                            impl->ApplyVhostMapping();
                            impl->ApplyFixedDriveFileHostMappings();

                            // Reliable ESC handling while focus is inside WebView content.
                            const HRESULT hrAcc = ctrl->add_AcceleratorKeyPressed(
                                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [impl](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                        if (!impl || !args || !impl->hwnd || !::IsWindow(impl->hwnd))
                                            return S_OK;
                                        COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                                        if (FAILED(args->get_KeyEventKind(&kind)))
                                            return S_OK;
                                        if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN
                                            && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                                            return S_OK;
                                        UINT vk = 0;
                                        if (FAILED(args->get_VirtualKey(&vk)))
                                            return S_OK;
                                        if (vk == VK_F4 && (::GetKeyState(VK_MENU) & 0x8000) != 0) {
                                            (void)args->put_Handled(TRUE);
                                            HWND closeTarget = ::GetAncestor(impl->hwnd, GA_ROOT);
                                            if (!closeTarget || !::IsWindow(closeTarget))
                                                closeTarget = impl->hwnd;
                                            ::PostMessageW(closeTarget, WM_CLOSE, 0, 0);
                                            return S_OK;
                                        }
                                        if (!impl->opts.close_on_esc || impl->opts.mode == CWebViewOptions::Mode::Docked || vk != VK_ESCAPE)
                                            return S_OK;
                                        (void)args->put_Handled(TRUE);
                                        ::PostMessageW(impl->hwnd, WM_CLOSE, 0, 0);
                                        return S_OK;
                                    }).Get(),
                                &impl->acceleratorToken);
                            if (SUCCEEDED(hrAcc))
                                impl->accelerator_hook_registered = true;
                            else
                                cweb_log_warn(std::string("[cweb] add_AcceleratorKeyPressed failed: 0x")
                                    + std::to_string(static_cast<uint32_t>(hrAcc)));

                            // Web-message handler (web → host).
                            impl->webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [impl](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw)
                                            return S_OK;
                                        std::wstring wmsg(raw);
                                        ::CoTaskMemFree(raw);
                                        const std::string u8 = pmui::wide_to_utf8(wmsg);
                                        try {
                                            const nlohmann::json j = nlohmann::json::parse(u8);
                                            const std::string    t = j.value("t", std::string{});

                                            // Console bridge messages.
                                            if (t == "cweb_console") {
                                                const std::string lvl = j.value("level", std::string{"log"});
                                                const std::string msg = j.value("msg",   std::string{});
                                                if (impl->opts.onConsole) {
                                                    impl->opts.onConsole(lvl, msg);
                                                } else {
                                                    const std::string line = "[cweb] js:" + lvl + " " + cweb_u8_clip(msg);
                                                    if (lvl == "error")      cweb_log_error(line);
                                                    else if (lvl == "warn")  cweb_log_warn(line);
                                                    else                     cweb_log_trace(line);
                                                }
                                                return S_OK;
                                            }

                                            // Built-in caption commands from the HTML title bar.
                                            if ((t == "cweb_min" || t == "cweb_max" || t == "cweb_close")
                                                && impl->hwnd && ::IsWindow(impl->hwnd)) {
                                                if (t == "cweb_close") {
                                                    // Always close this popup HWND directly; avoids any
                                                    // owner/system-command routing surprises in modal setups.
                                                    ::PostMessageW(impl->hwnd, WM_CLOSE, 0, 0);
                                                    return S_OK;
                                                }
                                                const WPARAM sc =
                                                    (t == "cweb_min")   ? SC_MINIMIZE  :
                                                    (::IsZoomed(impl->hwnd) ? SC_RESTORE : SC_MAXIMIZE);
                                                ::PostMessageW(impl->hwnd, WM_SYSCOMMAND, sc, 0);
                                                return S_OK;
                                            }

                                            // Host-driven move start (for Alt+drag or explicit drag handles in web UI).
                                            if (t == "cweb_begin_move" && impl->hwnd && ::IsWindow(impl->hwnd)) {
                                                ::ReleaseCapture();
                                                ::SendMessageW(impl->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                                                return S_OK;
                                            }

                                            // Built-in optional web-driven host resize:
                                            // { t:"cweb_resize", w:number, h:number } (CSS px / logical px).
                                            if (t == "cweb_resize"
                                                && impl->opts.allowWebResizeHostWindow
                                                && impl->hwnd && ::IsWindow(impl->hwnd)
                                                && impl->opts.mode != CWebViewOptions::Mode::Docked) {
                                                const int wReq = j.value("w", 0);
                                                const int hReq = j.value("h", 0);
                                                if (wReq > 0 && hReq > 0) {
                                                    // Web sends CSS px (DIPs). Convert to window px and map
                                                    // requested content size -> outer HWND size.
                                                    const UINT dpi = ::GetDpiForWindow(impl->hwnd);
                                                    const int  wDip = wReq;
                                                    const int  hDip = hReq;
                                                    const int  wPx  = ::MulDiv(wDip, static_cast<int>(dpi), 96);
                                                    const int  hPx  = ::MulDiv(hDip, static_cast<int>(dpi), 96);
                                                    const int  inset = cweb_controller_inset(impl->opts);
                                                    int w = wPx + inset * 2;
                                                    int h = hPx + inset * 2;

                                                    // Clamp to sane runtime bounds. WM_GETMINMAXINFO still
                                                    // enforces per-window mins during interactive resize.
                                                    w = (std::max)(320, (std::min)(w, 4096));
                                                    h = (std::max)(240, (std::min)(h, 4096));
                                                    RECT wr{};
                                                    ::GetWindowRect(impl->hwnd, &wr);
                                                    const int curW = wr.right - wr.left;
                                                    const int curH = wr.bottom - wr.top;
                                                    const int x = wr.left + (curW - w) / 2;
                                                    const int y = wr.top  + (curH - h) / 2;
                                                    ::SetWindowPos(impl->hwnd, nullptr, x, y, w, h,
                                                        SWP_NOZORDER | SWP_NOACTIVATE);
                                                    if (impl->controller) {
                                                        cweb_apply_bounds(impl->controller.Get(), impl->hwnd, inset);
                                                        (void)impl->controller->put_IsVisible(TRUE);
                                                    }
                                                    ::InvalidateRect(impl->hwnd, nullptr, TRUE);
                                                    ::UpdateWindow(impl->hwnd);
                                                    RECT wr2{}, cr2{}, br2{};
                                                    ::GetWindowRect(impl->hwnd, &wr2);
                                                    ::GetClientRect(impl->hwnd, &cr2);
                                                    if (impl->controller) {
                                                        (void)impl->controller->get_Bounds(&br2);
                                                    }
                                                }
                                                return S_OK;
                                            }

                                            if (t == "cweb_nav" && impl->webview) {
                                                const std::string u = j.value("url", std::string{});
                                                if (!u.empty()) {
                                                    const std::wstring wu = pmui::utf8_to_wide(u);
                                                    impl->webview->Navigate(wu.c_str());
                                                }
                                                return S_OK;
                                            }

                                            if (t == "debug_sizes" && impl->hwnd && ::IsWindow(impl->hwnd)) {
                                                RECT wr{}, cr{}, br{};
                                                ::GetWindowRect(impl->hwnd, &wr);
                                                ::GetClientRect(impl->hwnd, &cr);
                                                if (impl->controller)
                                                    (void)impl->controller->get_Bounds(&br);
                                                std::string webPart;
                                                if (j.contains("web")) webPart = j["web"].dump();                                                
                                                return S_OK;
                                            }

                                            // Arbitrary app messages: fire user callback first.
                                            if (impl->opts.onMessage)
                                                impl->opts.onMessage(u8);                                            

                                        } catch (...) {
                                            cweb_log_trace(std::string("[cweb] non-JSON message: ")
                                                + cweb_u8_clip(u8, 400));
                                        }
                                        return S_OK;
                                    }).Get(),
                                &impl->msgToken);
                            impl->msg_hook_registered = true;

                            // Navigation-completed handler: flush pending scripts + resize.
                            impl->webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [host, impl](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL ok = TRUE;
                                        if (args) (void)args->get_IsSuccess(&ok);
                                        if (!ok) {
                                            COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                            ComPtr<ICoreWebView2NavigationCompletedEventArgs2> a2;
                                            if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&a2))) && a2)
                                                (void)a2->get_WebErrorStatus(&st);
                                            cweb_log_warn(std::string("[cweb] NavigationCompleted failed; status=")
                                                + std::to_string(static_cast<int>(st)));
                                        }
                                        const DWORD64 done = cweb_now_ms();
                                        cweb_log_trace(std::string("[cweb-open] NavigationCompleted ok=")
                                            + (ok ? "1" : "0")
                                            + " nav→done=" + std::to_string(
                                                impl->nav_start_ms ? (done - impl->nav_start_ms) : 0)
                                            + " ms total=+" + std::to_string(done - impl->create_start_ms)
                                            + " ms");
                                        // Re-apply theme after navigation (color-scheme may need reset).
                                        if (host && ::IsWindow(host))
                                            ::SetTimer(host, kTimerChromeResync, 120, nullptr);
                                        impl->navigationComplete = ok != FALSE;
                                        if (impl->navigationComplete)
                                            impl->FlushPendingWebMessages();
                                        return S_OK;
                                    }).Get(),
                                &impl->navCompletedToken);
                            impl->nav_hook_registered = true;

                            // Mark ready, flush queued scripts, kick navigation.
                            impl->ready = true;
                            impl->FlushPendingScripts();
                            impl->DoNavigate();
                            impl->EnsureShellFileDropTarget();
                            
                            return S_OK;
                        }).Get());
            }).Get());

    if (FAILED(hrEnv))
        cweb_log_error(std::string("[cweb] CreateCoreWebView2EnvironmentWithOptions failed: 0x")
            + std::to_string(static_cast<uint32_t>(hrEnv)));
    return 0;
}

LRESULT CWebView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_SIZE: {
            if (m_impl && m_impl->controller) {
                cweb_apply_bounds(m_impl->controller.Get(), GetHwnd(), cweb_controller_inset(m_impl->opts));
                m_impl->controller->put_IsVisible(TRUE);
                // If WebView2 is ready but not yet navigated (e.g. was invisible at OnCreate),
                // try navigation now that we have a real client rect.
                if (m_impl->ready && !m_impl->navigated)
                    m_impl->DoNavigate();
                // PopupTransparent: keep the HTML max/restore button glyph in sync.
                // PopupPure has no title bar so no glyph to update.
                if (m_impl->opts.mode == CWebViewOptions::Mode::PopupTransparent && m_impl->ready) {
                    const bool zoomed = ::IsZoomed(GetHwnd()) != FALSE;
                    PostToWeb(std::string("{\"t\":\"cweb_wndstate\",\"maximized\":") +
                        (zoomed ? "true" : "false") + "}");
                }
                m_impl->EnsureShellFileDropTarget();
            }
            return 0;
        }

        case WM_SHOWWINDOW:
            if (wparam && m_impl && m_impl->ready) {
                if (!m_impl->navigated)
                    m_impl->DoNavigate();
                m_impl->EnsureShellFileDropTarget();
            }
            break;

        case WM_TIMER:
            if (wparam == kTimerChromeResync) {
                ::KillTimer(GetHwnd(), kTimerChromeResync);
                if (m_impl && m_impl->controller) {
                    const bool isPopup  = (m_impl->opts.mode != CWebViewOptions::Mode::Docked);
                    const bool isBorderless = cweb_is_borderless(m_impl->opts.mode);
                    const bool transparentBg = cweb_uses_colorkey(m_impl->opts.mode);
                    cweb_full_chrome_resync(m_impl->controller.Get(), GetHwnd(), isPopup, isBorderless && m_impl->opts.frame, transparentBg,
                        cweb_uses_colorkey(m_impl->opts.mode));
                }
                return 0;
            }
            break;

        case UWM_CWEB_POST_WEB_MSG: {
            std::unique_ptr<std::string> payload(reinterpret_cast<std::string*>(lparam));
            if (payload)
                PostToWeb(*payload);
            return 0;
        }

        case WM_NCCALCSIZE:
            // Borderless popup: make client rect == full window rect so WebView2 covers
            // everything, then provide hit-codes manually from WM_NCHITTEST.
            if (wparam && m_impl && cweb_is_borderless(m_impl->opts.mode)) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                if (::IsZoomed(GetHwnd())) {
                    // Maximised: clip to working area so the taskbar stays visible.
                    HMONITOR mon = ::MonitorFromWindow(GetHwnd(), MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi{sizeof(mi)};
                    if (mon && ::GetMonitorInfoW(mon, &mi)) {
                        p->rgrc[0] = mi.rcWork;
                        return 0;
                    }
                } else {
                    // Save proposed window rect, let DefWindowProc apply WS_THICKFRAME
                    // insets, then restore so the frame is invisible (borderless look).
                    const RECT saved = p->rgrc[0];
                    const LRESULT lr = ::DefWindowProcW(GetHwnd(), WM_NCCALCSIZE, wparam, lparam);
                    p->rgrc[0] = saved;
                    return lr;
                }
            }
            break;

        case WM_GETMINMAXINFO:
            if (m_impl && cweb_is_borderless(m_impl->opts.mode)) {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
                mmi->ptMinTrackSize.x = 360;
                mmi->ptMinTrackSize.y = 240;
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            // Borderless popup: fill with the theme surface color so transparent HTML
            // areas show the correct background while WebView2 is initialising.
            if (m_impl && cweb_is_borderless(m_impl->opts.mode)) {
                RECT rc{};
                ::GetClientRect(GetHwnd(), &rc);
                const COLORREF bg = cweb_uses_colorkey(m_impl->opts.mode)
                    ? kTransColorKey
                    : pmui::theme_palette().web_surface_bg;
                HBRUSH br = ::CreateSolidBrush(bg);
                ::FillRect(reinterpret_cast<HDC>(wparam), &rc, br);
                ::DeleteObject(br);
                return 1;
            }
            if (m_impl && m_impl->controller)
                return 1;
            break;

        case WM_NCACTIVATE:
            // Alt+Tab (or activation changes) can cause default NC painting to
            // re-assert thick-frame visuals. For all borderless modes we keep NC
            // activation handled to preserve custom chrome while resize is driven
            // by WM_NCHITTEST + WS_THICKFRAME.
            if (m_impl && cweb_is_borderless(m_impl->opts.mode))
                return TRUE;
            break;

        case WM_NCPAINT:
            // Block NC paint in borderless mode to prevent sporadic thick-frame
            // flashes after activation/state changes.
            if (m_impl && cweb_is_borderless(m_impl->opts.mode))
                return 0;
            break;

        case WM_NCHITTEST: {
            if (m_impl && cweb_is_borderless(m_impl->opts.mode)) {
                const POINT screenPt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};

                // Resize edges — checked for both borderless modes (disabled when maximised).
                if (m_impl->opts.frame && !::IsZoomed(GetHwnd())) {
                    RECT wr{};
                    ::GetWindowRect(GetHwnd(), &wr);
                    const bool left   = screenPt.x <  wr.left   + kWvBorderPx;
                    const bool right  = screenPt.x >= wr.right  - kWvBorderPx;
                    const bool top    = screenPt.y <  wr.top    + kWvBorderPx;
                    const bool bottom = screenPt.y >= wr.bottom - kWvBorderPx;
                    if (left  && top)    return HTTOPLEFT;
                    if (right && top)    return HTTOPRIGHT;
                    if (left  && bottom) return HTBOTTOMLEFT;
                    if (right && bottom) return HTBOTTOMRIGHT;
                    if (left)            return HTLEFT;
                    if (right)           return HTRIGHT;
                    if (bottom)          return HTBOTTOM;
                    if (top)             return HTTOP;
                }

                // PopupTransparent: title-bar row → caption-button zones + HTCAPTION.
                if (m_impl->opts.mode == CWebViewOptions::Mode::PopupTransparent) {
                    POINT pt = screenPt;
                    ::ScreenToClient(GetHwnd(), &pt);
                    RECT rc{};
                    ::GetClientRect(GetHwnd(), &rc);
                    if (pt.y >= 0 && pt.y < kWvTitleH && rc.right > 0) {
                        if (pt.x >= rc.right - kWvBtnW)      return HTCLOSE;
                        if (pt.x >= rc.right - kWvBtnW * 2)  return HTMAXBUTTON;
                        if (pt.x >= rc.right - kWvBtnW * 3)  return HTMINBUTTON;
                        return HTCAPTION;
                    }
                }

                // PopupPure: no title bar — web app owns the surface entirely.
                return HTCLIENT;
            }
            break;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (m_impl
                && wparam == VK_ESCAPE
                && m_impl->opts.close_on_esc
                && m_impl->opts.mode != CWebViewOptions::Mode::Docked
                && GetHwnd() && ::IsWindow(GetHwnd())) {
                ::PostMessageW(GetHwnd(), WM_CLOSE, 0, 0);
                return 0;
            }
            break;

        case WM_NCLBUTTONDOWN:
            if (m_impl && m_impl->opts.mode == CWebViewOptions::Mode::PopupTransparent) {
                if (wparam == HTMINBUTTON) {
                    ::PostMessageW(GetHwnd(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    return 0;
                }
                if (wparam == HTMAXBUTTON) {
                    ::PostMessageW(GetHwnd(), WM_SYSCOMMAND,
                        ::IsZoomed(GetHwnd()) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    return 0;
                }
                if (wparam == HTCLOSE) {
                    ::PostMessageW(GetHwnd(), WM_SYSCOMMAND, SC_CLOSE, 0);
                    return 0;
                }
            }
            break;

        case WM_NCLBUTTONDBLCLK:
            if (m_impl && m_impl->opts.mode == CWebViewOptions::Mode::PopupTransparent) {
                if (wparam == HTCAPTION || wparam == HTMAXBUTTON) {
                    ::PostMessageW(GetHwnd(), WM_SYSCOMMAND,
                        ::IsZoomed(GetHwnd()) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    return 0;
                }
            }
            break;

        case WM_NCMOUSEMOVE: {
            // Forward caption-button hover state to the HTML page so it can highlight
            // the correct button. CSS :hover can't fire for NC-area mouse events.
            if (m_impl && m_impl->opts.mode == CWebViewOptions::Mode::PopupTransparent) {
                int btn = -1;
                if      (wparam == HTMINBUTTON) btn = 0;
                else if (wparam == HTMAXBUTTON) btn = 1;
                else if (wparam == HTCLOSE)     btn = 2;
                PostToWeb("{\"t\":\"cweb_btnhover\",\"btn\":" + std::to_string(btn) + "}");
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, GetHwnd(), 0};
                ::TrackMouseEvent(&tme);
            }
            break;
        }

        case WM_NCMOUSELEAVE:
            if (m_impl && m_impl->opts.mode == CWebViewOptions::Mode::PopupTransparent)
                PostToWeb("{\"t\":\"cweb_btnhover\",\"btn\":-1}");
            break;

        case WM_DESTROY:
            {
                char buf[80];
                snprintf(buf, sizeof(buf), "[cweb] WM_DESTROY @%p hwnd=%p — closing WebView2",
                    static_cast<void*>(this), reinterpret_cast<void*>(GetHwnd()));
                cweb_log_trace(buf);
            }
            ::KillTimer(GetHwnd(), kTimerChromeResync);
            // Tear down WebView2 while the HWND is still valid — calling Close()
            // after the HWND is gone causes the browser process to linger.
            if (m_impl) {
                if (m_impl->modal_owner_hwnd && ::IsWindow(m_impl->modal_owner_hwnd)) {
                    ::EnableWindow(m_impl->modal_owner_hwnd, TRUE);
                    if (::IsIconic(m_impl->modal_owner_hwnd))
                        ::ShowWindow(m_impl->modal_owner_hwnd, SW_RESTORE);
                    ::SetWindowPos(m_impl->modal_owner_hwnd, HWND_TOP, 0, 0, 0, 0,
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                    ::SetForegroundWindow(m_impl->modal_owner_hwnd);
                    ::SetActiveWindow(m_impl->modal_owner_hwnd);
                    m_impl->modal_owner_hwnd = nullptr;
                }
                m_impl->RevokeShellFileDropTarget();
                if (m_impl->webview) {
                    if (m_impl->msg_hook_registered) {
                        m_impl->webview->remove_WebMessageReceived(m_impl->msgToken);
                        m_impl->msg_hook_registered = false;
                    }
                    if (m_impl->nav_hook_registered) {
                        m_impl->webview->remove_NavigationCompleted(m_impl->navCompletedToken);
                        m_impl->nav_hook_registered = false;
                    }
                    m_impl->webview->Stop();
                    m_impl->webview.Reset();
                }
                if (m_impl->controller) {
                    if (m_impl->accelerator_hook_registered) {
                        (void)m_impl->controller->remove_AcceleratorKeyPressed(m_impl->acceleratorToken);
                        m_impl->accelerator_hook_registered = false;
                    }
                    m_impl->controller->Close();
                    m_impl->controller.Reset();
                }
                m_impl->ready = false;
            }
            break;

        default:
            break;
        }
    } catch (...) {}
    return CWnd::WndProc(msg, wparam, lparam);
}

// ── Hello pages ──────────────────────────────────────────────────────────────

static const char k_hello_html[] =
    "<!DOCTYPE html>"
    "<html><head><meta charset=\"utf-8\">"
    "<title>Web Browser</title>"
    "<style>"
    "*, *::before, *::after { box-sizing: border-box; }"
    "html, body { margin: 0; height: 100%; }"
    "body {"
    "  display: flex; flex-direction: column;"
    "  align-items: center; justify-content: center;"
    "  background: #16181d; color: #c8ccd4;"
    "  font-family: 'Segoe UI', system-ui, sans-serif; font-size: 14px;"
    "  user-select: none;"
    "}"
    "h1 { margin: 0 0 8px; font-size: 18px; font-weight: 500; color: #e2e5ec; }"
    "p  { margin: 0; font-size: 13px; color: #666c7a; }"
    "</style></head>"
    "<body>"
    "<h1>Web Browser</h1>"
    "<p>Navigate or pass a URL via <code>CWebView::Navigate()</code>.</p>"
    "</body></html>";

// Borderless hello — HTML provides the visual title bar (drag, min/max/close glyphs).
// The OS handles all behaviour: WM_NCHITTEST returns HTCAPTION / HTMINBUTTON /
// HTMAXBUTTON / HTCLOSE so the system drives drag, resize and SC_* commands.
// Button hover is forwarded from WM_NCMOUSEMOVE via cweb_btnhover messages because
// CSS :hover does not fire while the cursor is in NC territory.
static const char k_transparent_hello_html[] =
    "<!DOCTYPE html>"
    "<html><head><meta charset=\"utf-8\"><title>CWebView</title>"
    "<style>"
    "*, *::before, *::after { box-sizing:border-box; margin:0; padding:0; }"
    "html, body {"
    "  width:100%; height:100%;"
    "  background:transparent;"
    "  font-family:'Segoe UI',system-ui,sans-serif;"
    "  color:#c8ccd4; overflow:hidden;"
    "}"
    "body { display:flex; flex-direction:column; }"
    // ── title bar (kWvTitleH = 32 px) ──
    "#titlebar {"
    "  height:32px; display:flex; align-items:center; justify-content:space-between;"
    "  padding-left:12px; flex-shrink:0;"
    "  background:#12141a;"
    "  -webkit-app-region:drag;"
    "  user-select:none;"
    "}"
    "#tb-title { font-size:12px; font-weight:500; color:#9aa0ad; }"
    // ── caption buttons (kWvBtnW = 46 px each, 3 buttons = 138 px) ──
    "#tb-btns {"
    "  display:flex; align-items:stretch; height:32px;"
    "  -webkit-app-region:no-drag;"
    "}"
    ".wbtn {"
    "  width:46px; height:32px; border:none; background:transparent;"
    "  color:#9aa0ad; font-size:14px; cursor:default;"
    "  display:flex; align-items:center; justify-content:center;"
    "  transition:background .1s, color .1s;"
    "}"
    ".wbtn.hovered { background:rgba(255,255,255,0.10); }"
    "#wbtn-close.hovered { background:rgba(232,17,35,0.88); color:#fff; }"
    // ── content area ──
    "#content {"
    "  flex:1; display:flex; align-items:center; justify-content:center;"
    "  background:transparent;"
    "}"
    ".card {"
    "  background:#16181e;"
    "  border:1px solid rgba(255,255,255,0.09);"
    "  border-radius:12px; padding:28px 36px; text-align:center;"
    "  box-shadow:0 8px 32px rgba(0,0,0,0.55);"
    "}"
    "h1 { font-size:16px; font-weight:500; color:#e2e5ec; margin-bottom:5px; }"
    "p  { font-size:13px; color:#6b7280; margin-bottom:18px; }"
    ".act-btn {"
    "  padding:7px 22px; border-radius:6px; border:none; cursor:pointer;"
    "  background:#3b82f6; color:#fff; font-size:13px; font-weight:500;"
    "  transition:background .15s;"
    "}"
    ".act-btn:hover { background:#2563eb; }"
    "</style></head>"
    "<body>"
    "<div id=\"titlebar\">"
    "  <span id=\"tb-title\">CWebView</span>"
    "  <div id=\"tb-btns\">"
    "    <div class=\"wbtn\" id=\"wbtn-min\" title=\"Minimize\""
    "      onclick=\"window.chrome&&chrome.webview.postMessage(JSON.stringify({t:'cweb_min'}))\""
    "    >&#x2013;</div>"
    "    <div class=\"wbtn\" id=\"wbtn-max\" title=\"Maximize\""
    "      onclick=\"window.chrome&&chrome.webview.postMessage(JSON.stringify({t:'cweb_max'}))\""
    "    >&#x25A1;</div>"
    "    <div class=\"wbtn\" id=\"wbtn-close\" title=\"Close\""
    "      onclick=\"window.chrome&&chrome.webview.postMessage(JSON.stringify({t:'cweb_close'}))\""
    "    >&#x2715;</div>"
    "  </div>"
    "</div>"
    "<div id=\"content\">"
    "  <div class=\"card\">"
    "    <h1>Borderless popup</h1>"
    "    <p>DWM chrome &#183; OS drag &amp; resize &#183; native min / max / close.</p>"
    "    <button class=\"act-btn\""
    "      onclick=\"window.chrome&&chrome.webview.postMessage(JSON.stringify({t:'hello',msg:'button clicked'}))\""
    "    >Send host message</button>"
    "  </div>"
    "</div>"
    "<script>"
    // cweb_btnhover: { t:'cweb_btnhover', btn: 0=min 1=max 2=close -1=none }
    "window.cwWebOnHostMessage = function(d) {"
    "  if (!d) return;"
    "  if (d.t === 'cweb_btnhover') {"
    "    var ids = ['wbtn-min','wbtn-max','wbtn-close'];"
    "    ids.forEach(function(id,i){"
    "      var el = document.getElementById(id);"
    "      if (el) el.classList.toggle('hovered', i === d.btn);"
    "    });"
    "  } else if (d.t === 'cweb_wndstate') {"
    "    var mx = document.getElementById('wbtn-max');"
    "    if (mx) mx.innerHTML = d.maximized ? '&#x2752;' : '&#x25A1;';"
    "    var tb = document.getElementById('titlebar');"
    "    if (tb) tb.style.borderRadius = d.maximized ? '0' : '';"
    "  }"
    "};"
    "</script>"
    "</body></html>";

// Pure playground — no title bar; web app owns all chrome.
static const char k_playground_html[] =
    "<!DOCTYPE html>"
    "<html><head><meta charset=\"utf-8\"><title>CWebView</title>"
    "<style>"
    "*, *::before, *::after { box-sizing:border-box; margin:0; padding:0; }"
    "html, body { width:100%; height:100%; background:transparent; }"
    "body {"
    "  font-family:'Segoe UI',system-ui,sans-serif;"
    "  color:#c8ccd4; overflow:hidden;"
    "  display:flex; align-items:center; justify-content:center;"
    "  -webkit-app-region:drag;"
    "  user-select:none;"
    "}"
    "#close {"
    "  width:28px; height:28px; border-radius:6px; border:none;"
    "  background:rgba(255,255,255,0.07); color:#9aa0ad; font-size:14px;"
    "  cursor:pointer; display:flex; align-items:center; justify-content:center;"
    "  transition:background .12s, color .12s;"
    "  -webkit-app-region:no-drag;"
    "}"
    "#close:hover { background:rgba(232,17,35,0.88); color:#fff; }"
    ".card {"
    "  background:#1e2028; border:1px solid rgba(255,255,255,0.08);"
    "  border-radius:10px; padding:28px 36px; text-align:center;"
    "  -webkit-app-region:no-drag;"
    "}"
    ".card-head { display:flex; align-items:center; justify-content:space-between; margin-bottom:8px; }"
    ".card-head h1 { margin:0; }"
    ".row { display:flex; gap:8px; align-items:center; justify-content:center; margin-top:12px; flex-wrap:wrap; }"
    ".addr {"
    "  width:380px; max-width:76vw; padding:7px 10px; border-radius:7px;"
    "  border:1px solid rgba(255,255,255,0.16); background:rgba(0,0,0,0.22);"
    "  color:#d9deea; font-size:12px; outline:none;"
    "  -webkit-app-region:no-drag;"
    "}"
    ".addr:focus { border-color:#3b82f6; box-shadow:0 0 0 1px rgba(59,130,246,.35); }"
    ".dbg-btn {"
    "  margin-top:14px; padding:7px 18px; border-radius:6px; border:none; cursor:pointer;"
    "  background:#3b82f6; color:#fff; font-size:12px; font-weight:600;"
    "  -webkit-app-region:no-drag;"
    "}"
    ".dbg-btn:hover { background:#2563eb; }"
    ".rx-wrap { margin-top:12px; text-align:left; }"
    ".rx-title { font-size:11px; color:#9aa3b2; margin-bottom:6px; }"
    "#rxLog {"
    "  max-height:120px; overflow:auto; border-radius:6px; padding:8px;"
    "  border:1px solid rgba(255,255,255,0.10); background:rgba(0,0,0,0.22);"
    "  font-family:Consolas, 'Courier New', monospace; font-size:11px; line-height:1.35;"
    "  color:#d3d8e2; white-space:pre-wrap; word-break:break-word;"
    "}"
    ".rx-line { margin:0; }"
    "h1 { font-size:16px; font-weight:500; color:#e2e5ec; margin-bottom:6px; }"
    "p  { font-size:13px; color:#6b7280; }"
    "</style></head>"
    "<body>"
    "<div class=\"card\">"
    "  <div class=\"card-head\">"
    "    <h1>Pure popup</h1>"
    "    <button id=\"close\" title=\"Close\""
    "      onclick=\"window.chrome&&chrome.webview.postMessage(JSON.stringify({t:'cweb_close'}))\""
    "    >&#x2715;</button>"
    "  </div>"
    "  <p>No title bar &#183; drag from background &#183; resize edges active.</p>"
    "  <div class=\"row\">"
    "    <input id=\"addr\" class=\"addr\" type=\"text\" value=\"https://example.com\" />"
    "    <button class=\"dbg-btn\" id=\"go\">Go</button>"
    "  </div>"
    "  <div class=\"row\">"
    "    <button class=\"dbg-btn\" id=\"dbgPing\">Debug ping</button>"
    "    <button class=\"dbg-btn\" id=\"dbgSize\">Debug sizes</button>"
    "    <button class=\"dbg-btn\" id=\"autoSize\">Auto-size host now</button>"
    "    <button class=\"dbg-btn\" id=\"openPopup\">Open another popup</button>"
    "    <button class=\"dbg-btn\" id=\"pingChild\">Ping last child</button>"
    "  </div>"
    "  <div class=\"rx-wrap\">"
    "    <div class=\"rx-title\">Received messages</div>"
    "    <div id=\"rxLog\" aria-live=\"polite\"></div>"
    "  </div>"
    "</div>"
    "<script>"
    "if (window.chrome && chrome.webview) {"
    "  var _lastChildId='';"
    "  var _theme='';"
    "  function _rx(line){"
    "    var box=document.getElementById('rxLog');"
    "    if(!box) return;"
    "    var el=document.createElement('div');"
    "    el.className='rx-line';"
    "    el.textContent=line;"
    "    box.appendChild(el);"
    "    while(box.childNodes.length>24) box.removeChild(box.firstChild);"
    "    box.scrollTop=box.scrollHeight;"
    "  }"
    "  function _post(o){ try{ chrome.webview.postMessage(JSON.stringify(o)); }catch(e){} }"
    "  function _isInteractive(el){"
    "    for(var n=el;n&&n!==document.body;n=n.parentElement){"
    "      if(!n||!n.tagName) continue;"
    "      var tag=n.tagName.toLowerCase();"
    "      if(tag==='input'||tag==='textarea'||tag==='select'||tag==='button'||tag==='a') return true;"
    "      if(n.isContentEditable) return true;"
    "      if(n.getAttribute&&n.getAttribute('role')==='textbox') return true;"
    "      if(n.closest&&n.closest('[data-no-drag], .no-drag')) return true;"
    "      var ar=(n.style&&n.style.webkitAppRegion)||'';"
    "      if(ar==='no-drag') return true;"
    "    }"
    "    return false;"
    "  }"
    "  function _sizes(){"
    "    var de=document.documentElement||{};"
    "    var b=document.body||{};"
    "    return {"
    "      innerW:window.innerWidth|0, innerH:window.innerHeight|0,"
    "      clientW:de.clientWidth|0, clientH:de.clientHeight|0,"
    "      scrollW:Math.max(de.scrollWidth||0,b.scrollWidth||0)|0,"
    "      scrollH:Math.max(de.scrollHeight||0,b.scrollHeight||0)|0,"
    "      dpr:(window.devicePixelRatio||1)"
    "    };"
    "  }"
    "  function _go(){"
    "    var v=(document.getElementById('addr')||{}).value||'';"
    "    v=v.trim();"
    "    if(!v) return;"
    "    if(!/^https?:\\/\\//i.test(v) && !/^file:\\/\\//i.test(v)) v='https://'+v;"
    "    _post({t:'cweb_nav',url:v});"
    "  }"
    "  var addr=document.getElementById('addr');"
    "  var go=document.getElementById('go');"
    "  if(go) go.onclick=_go;"
    "  if(addr) addr.addEventListener('keydown', function(e){ if(e.key==='Enter') _go(); });"
    "  var p=document.getElementById('dbgPing'); if(p) p.onclick=function(){ _post({t:'debug',msg:'playground debug ping'}); };"
    "  var s=document.getElementById('dbgSize'); if(s) s.onclick=function(){ _post({t:'debug_sizes',web:_sizes()}); };"
    "  var a=document.getElementById('autoSize'); if(a) a.onclick=function(){ if(window.cwebAutoResizeNow) window.cwebAutoResizeNow(); };"
    "  var o=document.getElementById('openPopup'); if(o) o.onclick=function(){ _post({t:'cweb_open_popup',kind:'playground'}); };"
    "  var pc=document.getElementById('pingChild'); if(pc) pc.onclick=function(){"
    "    if(!_lastChildId){ console.warn('[playground] no child id yet'); _rx('[warn] no child id yet'); return; }"
    "    console.log('[playground] ping -> '+_lastChildId);"
    "    _rx('[tx] ping -> '+_lastChildId);"
    "    _post({t:'cweb_bus',to:_lastChildId,kind:'ping',msg:'hello from opener',ts:Date.now()});"
    "  };"
    "  document.addEventListener('mousedown', function(e){"
    "    try {"
    "      if(!e.altKey) return;"
    "      if(e.button!==0) return;"
    "      if(_isInteractive(e.target)) return;"
    "      e.preventDefault();"
    "      _post({t:'cweb_begin_move',src:'alt_drag'});"
    "    } catch(ex) {}"
    "  }, true);"
    "  window.cwWebOnHostMessage = function(d){"
    "    try {"
    "      if(!d||typeof d!=='object') return;"
    "      if(d.t==='cweb_open_popup_result'){"
    "        _lastChildId = String(d.id||'');"
    "        console.info('[playground] child opened id='+_lastChildId+' hwnd='+String(d.hwnd||0));"
    "        _rx('[rx] child opened id='+_lastChildId+' hwnd='+String(d.hwnd||0));"
    "        return;"
    "      }"
    "      if(d.t==='cweb_theme'){"
    "        _theme=String(d.theme||'');"
    "        _rx('[rx] theme <- '+_theme);"
    "        return;"
    "      }"
    "      if(d.t==='cweb_bus'){"
    "        var from=String(d.from||'?');"
    "        var p=(d.payload&&typeof d.payload==='object')?d.payload:{};"
    "        if(p.kind==='ping'){"
    "          console.info('[playground] ping <- '+from+' msg='+(p.msg||''));"
    "          _rx('[rx] ping <- '+from+' msg='+(p.msg||''));"
    "          _post({t:'cweb_bus',to:from,kind:'pong',msg:'pong from receiver',ts:Date.now()});"
    "          _rx('[tx] pong -> '+from);"
    "          return;"
    "        }"
    "        if(p.kind==='pong'){"
    "          console.info('[playground] pong <- '+from+' msg='+(p.msg||''));"
    "          _rx('[rx] pong <- '+from+' msg='+(p.msg||''));"
    "          return;"
    "        }"
    "        console.log('[playground] bus <- '+from+' '+JSON.stringify(p));"
    "        _rx('[rx] bus <- '+from+' '+JSON.stringify(p));"
    "      }"
    "    } catch(e) {}"
    "  };"
    "  _rx('[ready] playground booted');"
    "  _post({t:'debug',msg:'playground ready'});"
    "}"
    "</script>"
    "</body></html>";

const char* cweb::default_hello_html() noexcept { return k_hello_html; }
const char* cweb::default_transparent_hello_html() noexcept { return k_transparent_hello_html; }
const char* cweb::default_playground_html() noexcept { return k_playground_html; }
const char* CWebViewOptions::cweb_default_hello_html_str() noexcept { return k_hello_html; }
const char* CWebViewOptions::cweb_default_transparent_hello_html_str() noexcept { return k_transparent_hello_html; }
const char* CWebViewOptions::cweb_playground_html_str() noexcept { return k_playground_html; }

std::wstring CWebViewOptions::cweb_exe_folder()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    buf.resize(n);
    return fs::path(buf).parent_path().wstring();
}

// ── CDockBrowserContainer ─────────────────────────────────────────────────────

CDockBrowserContainer::CDockBrowserContainer()
{
    SetTabText(L"Browser");
    SetDockCaption(L"Web Browser");

    CWebViewOptions opts;
    opts.html            = k_hello_html;
    opts.devToolsEnabled = true;  // F12 useful during development
    m_view.SetOptions(std::move(opts));

    SetView(m_view);
}

void CDockBrowserContainer::RefreshTabTheme()
{
    CDockContainerBase::RefreshTabTheme();
    m_view.RefreshChromeForTheme();
}

LRESULT CDockBrowserContainer::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        return CDockContainerBase::WndProc(msg, wparam, lparam);
    } catch (...) {}
    return 0;
}

// ── CDockWebBrowser ───────────────────────────────────────────────────────────

CDockWebBrowser::CDockWebBrowser()
{
    SetView(m_container);
}
