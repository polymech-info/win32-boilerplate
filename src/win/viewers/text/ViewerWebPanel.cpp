// ViewerWebPanel.cpp — WebView2 shell for apps/viewer-next (pluggable viewer kinds).
#include "stdafx.h"
#include "win/viewers/text/ViewerWebPanel.h"
#include "FileViewer.h"
#include "constants.hpp"
#include "win/viewers/ViewerWebResource.h"
#include "file_extensions.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/theme.hpp"
#include "win/settings_store.hpp"
#include "win/web/CWebViewManager.h"
#include "win/web/webview_bootstrap.hpp"
#include "logger/logger.h"

#include <CommCtrl.h>
#include <WebView2.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl.h>
#include <wrl/client.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shlwapi.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

/** Default spdlog (`logger::*`); mirrored to the Log panel / `pm-image.log` when the UI sink is active. */
static void vweb_log_info(const std::string& s)
{
    logger::info(s);
}
static void vweb_log_warn(const std::string& s)
{
    logger::warn(s);
}
static void vweb_log_error(const std::string& s)
{
    logger::error(s);
}

namespace {

static std::string vweb_utf8_limited(std::string_view u8, std::size_t max);
static std::string vweb_path_w_for_log(const std::wstring& w);

constexpr UINT_PTR kTimerViewerWebChromeResync = 305u;
/// Re-push `setStatus` after React installs `window.pmViewer` (first ExecuteScript often no-ops).
constexpr UINT_PTR kTimerViewerWebBridgeReplay = 306u;

static std::wstring viewer_webview2_user_data_folder()
{
    try {
        fs::path p = media::settings::get_config_dir() / "web-viewer";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p.wstring();
    } catch (...) {
        return {};
    }
}

static bool install_viewer_web_virtual_host(ICoreWebView2* webview, const std::string& utf8_html)
{
    (void)utf8_html;
    ComPtr<ICoreWebView2>   w0(webview);
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(w0.As(&v3)) || !v3) {
        vweb_log_warn("[viewer-web] install_viewer_web_virtual_host: ICoreWebView2_3 not available");
        return false;
    }

    const std::wstring folder = pmui::viewer_web_bundle_folder();
    if (folder.empty()) {
        vweb_log_warn("[viewer-web] install_viewer_web_virtual_host: dist/shared viewer bundle folder unresolved");
        return false;
    }

    const HRESULT    hrMap = v3->SetVirtualHostNameToFolderMapping(
        pm::brand::k_viewer_web_vhost_w, folder.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    if (FAILED(hrMap)) {
        vweb_log_warn(std::string("[viewer-web] SetVirtualHostNameToFolderMapping(viewer) failed: 0x")
                      + std::to_string(static_cast<uint32_t>(hrMap)));
        return false;
    }
    return true;
}

/// Percent-encode UTF-8 basename for a single path segment in `https://<vhost>/…`.
static void append_pct_encoded_utf8_path_segment(std::string& out, const std::wstring& wseg)
{
    const std::string u8 = pmui::wide_to_utf8(wseg);
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char c : std::string_view(u8)) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
            || c == '_' || c == '.' || c == '~';
        if (safe)
            out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
}

static void apply_webview2_controller_bounds(ICoreWebView2Controller* ctrl, HWND host)
{
    if (!ctrl || !host) return;
    RECT rc{};
    ::GetClientRect(host, &rc);
    ctrl->put_Bounds(rc);
}

static void apply_webview2_match_theme_background(ICoreWebView2Controller* ctrl)
{
    if (!ctrl) return;
    ComPtr<ICoreWebView2Controller2> c2;
    if (FAILED(ctrl->QueryInterface(IID_PPV_ARGS(&c2))) || !c2)
        return;
    const auto& pal = pmui::theme_palette();
    COREWEBVIEW2_COLOR col{};
    col.A = 255;
    col.R = GetRValue(pal.web_surface_bg);
    col.G = GetGValue(pal.web_surface_bg);
    col.B = GetBValue(pal.web_surface_bg);
    c2->put_DefaultBackgroundColor(col);
}

static void apply_webview2_preferred_color_scheme(ICoreWebView2* webview)
{
    if (!webview) return;
    ComPtr<ICoreWebView2_13>     w13;
    ComPtr<ICoreWebView2Profile> prof;
    if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&w13))) || !w13) return;
    if (FAILED(w13->get_Profile(&prof)) || !prof) return;
    const bool dark = pmui::theme_palette().dark;
    const COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme = dark
        ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
        : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
    (void)prof->put_PreferredColorScheme(scheme);
}

static void viewer_resync_webview2_chrome(ICoreWebView2Controller* ctrl, HWND host)
{
    if (!ctrl || !host || !::IsWindow(host)) return;
    ComPtr<ICoreWebView2> wv;
    if (SUCCEEDED(ctrl->get_CoreWebView2(&wv)) && wv)
        apply_webview2_preferred_color_scheme(wv.Get());
    apply_webview2_controller_bounds(ctrl, host);
    apply_webview2_match_theme_background(ctrl);
    const auto& pal = pmui::theme_palette();
    pmui::nuke_webview2_host_chrome(host, pal.web_surface_bg);
    pmui::flatten_webview_host_parent_chain(host, pal.web_surface_bg, 16);
    ::InvalidateRect(host, nullptr, TRUE);
}

static void viewer_schedule_chrome_resync(HWND host)
{
    if (!host || !::IsWindow(host)) return;
    ::SetTimer(host, kTimerViewerWebChromeResync, 100, nullptr);
}

/// Second `FlushContextToWeb` + `PushFontExtraToWeb` after the document is up. Only scheduled from
/// `NavigationCompleted` (cold start / `Reload()`): React + `window.pmViewer` can miss the first in-callback
/// `ExecuteScript`. Warm `Set*` updates already flush synchronously — repeating every preview change was redundant.
static void viewer_schedule_bridge_context_replay(HWND host)
{
    if (!host || !::IsWindow(host)) return;
    ::KillTimer(host, kTimerViewerWebBridgeReplay);
    ::SetTimer(host, kTimerViewerWebBridgeReplay, 420, nullptr);
}

/// Truncate UTF-8 for log lines (avoid huge markdown / paths in stderr / Log panel).
static std::string vweb_utf8_limited(std::string_view u8, std::size_t max)
{
    if (u8.size() <= max)
        return std::string(u8);
    return std::string(u8.substr(0, max)) + "…(" + std::to_string(u8.size()) + "B)";
}

static std::string vweb_path_w_for_log(const std::wstring& w)
{
    return vweb_utf8_limited(pmui::wide_to_utf8(w), 900);
}

static std::string vweb_selection_for_log(const std::vector<std::wstring>& sel)
{
    std::string o = "selectionCount=" + std::to_string(sel.size());
    const size_t n = (std::min)(sel.size(), static_cast<size_t>(4));
    for (size_t i = 0; i < n; ++i) {
        o += " |";
        o += vweb_utf8_limited(pmui::wide_to_utf8(sel[i]), 220);
    }
    if (sel.size() > n)
        o += " |…";
    return o;
}

} // namespace

struct CViewerWebPanel::Impl {
    HWND                        m_hostHwnd = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    CWebViewManager*                busManager = nullptr; // non-owning shared WebView bus.
    EventRegistrationToken          navCompletedToken{};
    EventRegistrationToken          webMessageToken{};
    EventRegistrationToken          navAcceleratorToken{};
    bool                            web_message_hook_registered = false;
    bool                            nav_accelerator_registered  = false;
    bool                            console_bridge_installed     = false;
    std::vector<HWND>              nav_mouse_subclassed;

    bool                      ready           = false;
    std::vector<std::wstring> pendingScripts;

    /// True while the viewer-next UI is showing the Monaco editor kind (`vw_editor_mode`).
    /// When set, `RegisterCentrePreviewNavAccelerator` suppresses arrow-key file navigation.
    bool                      editor_mode_active = false;

    std::string deferredHtml;
    bool        useViewerWebOrigin = false;
    bool        navigated          = false;

    std::vector<std::wstring> selection;
    std::wstring              folder;
    std::string               viewer_kind_utf8;
    std::string               ui_locale;
    std::string               markdown_embed_url_utf8;
    std::string               markdown_client_utf8;
    std::string               markdown_base_url_utf8;
    /// Absolute folder mapped to `k_markdown_assets_vhost_w` for https preview `<base href>`.
    std::wstring              markdown_assets_folder;
    /// Absolute path for `vw_hosted_read` bridge (matches `hostedFileUrl` / `threeModelUrl` basename).
    std::wstring              native_hosted_read_abs_path_w;

    /// Normalized absolute folder key for the last hosted preview (`SetVirtualHostNameToFolderMapping` target).
    /// Empty until a hosted preview commits; cleared when `SetContext` abandons selection.
    std::wstring web_last_preview_folder_key;
    /// Set when selection is cleared (e.g. image mode). Next hosted preview `Reload()`s so JS state matches the new folder.
    bool web_hosted_context_reset_pending = false;

    std::string   three_model_url_utf8;
    std::string   three_file_name_utf8;
    std::uint64_t three_file_size_bytes = 0;
    std::uint64_t three_max_bytes       = 0;
    std::string   three_error_utf8;
    std::string   openscad_source_text_utf8;

    std::string   hosted_file_url_utf8;
    std::string   hosted_file_text_utf8;
    bool          hosted_file_embed_text = false;
    std::string   hosted_file_name_utf8;
    std::uint64_t hosted_file_size_bytes = 0;
    std::uint64_t hosted_file_max_bytes  = 0;
    std::string   hosted_file_error_utf8;

    void RunJsNow(const std::wstring& js)
    {
        if (!webview) return;
        webview->ExecuteScript(js.c_str(), nullptr);
    }

    void FlushContextToWeb()
    {
        nlohmann::json j;
        j["selection"] = nlohmann::json::array();
        for (const auto& w : selection)
            j["selection"].push_back(pmui::wide_to_utf8(w));
        j["folder"]      = pmui::wide_to_utf8(folder);
        j["viewerKind"]  = viewer_kind_utf8;
        j["features"]    = nlohmann::json::object();
        if (!markdown_client_utf8.empty())
            j["features"]["markdownText"] = markdown_client_utf8;
        if (!markdown_base_url_utf8.empty())
            j["features"]["markdownBaseUrl"] = markdown_base_url_utf8;
        if (!markdown_embed_url_utf8.empty())
            j["features"]["markdownEmbedUrl"] = markdown_embed_url_utf8;
        if (!three_error_utf8.empty())
            j["features"]["threeError"] = three_error_utf8;
        if (!three_model_url_utf8.empty())
            j["features"]["threeModelUrl"] = three_model_url_utf8;
        if (!three_file_name_utf8.empty())
            j["features"]["threeFileName"] = three_file_name_utf8;
        if (three_file_size_bytes > 0)
            j["features"]["threeFileSizeBytes"] = three_file_size_bytes;
        if (three_max_bytes > 0)
            j["features"]["threeMaxBytes"] = three_max_bytes;
        if (!openscad_source_text_utf8.empty())
            j["features"]["openscadSourceText"] = openscad_source_text_utf8;
        if (!hosted_file_error_utf8.empty())
            j["features"]["hostedFileError"] = hosted_file_error_utf8;
        if (!hosted_file_url_utf8.empty())
            j["features"]["hostedFileUrl"] = hosted_file_url_utf8;
        if (hosted_file_embed_text)
            j["features"]["hostedFileText"] = hosted_file_text_utf8;
        if (!hosted_file_name_utf8.empty())
            j["features"]["hostedFileName"] = hosted_file_name_utf8;
        if (hosted_file_size_bytes > 0)
            j["features"]["hostedFileSizeBytes"] = hosted_file_size_bytes;
        if (hosted_file_max_bytes > 0)
            j["features"]["hostedFileMaxBytes"] = hosted_file_max_bytes;
        const auto& pal = pmui::theme_palette();
        j["features"]["theme"] = pal.dark ? "dark" : "light";
        std::wstring js = L"if(window.pmViewer){";
        if (!ui_locale.empty())
            js += L"window.pmViewer.setLocale("
                + pmui::utf8_to_wide(nlohmann::json(ui_locale).dump()) + L");";
        js += L"window.pmViewer.setStatus(" + pmui::utf8_to_wide(j.dump()) + L");}";
        const std::string dump = j.dump();
        if (ready) RunJsNow(js);
        else pendingScripts.push_back(js);
    }

    void PushFontExtraToWeb()
    {
        media::settings::AppearanceSettings appearance;
        std::string                         aerr;
        if (!media::settings::load_appearance(appearance, aerr)) return;
        int fi = appearance.font_size_extra_pt;
        if (fi < 0) fi = 0;
        if (fi > 4) fi = 4;
        const std::wstring js = L"if(window.pmViewer)window.pmViewer.setFontExtraPt("
            + std::to_wstring(fi) + L");";
        if (ready) RunJsNow(js);
        else pendingScripts.push_back(js);
    }

    void ApplyMarkdownAssetsFolderMapping(const char* reason, const std::wstring& cleared_folder_snapshot)
    {
        if (!webview)
            return;
        ComPtr<ICoreWebView2>   w0(webview);
        ComPtr<ICoreWebView2_3> v3;
        if (FAILED(w0.As(&v3)) || !v3)
            return;
        const std::string vhost_u8 = pmui::wide_to_utf8(std::wstring(pm::brand::k_markdown_assets_vhost_w));
        if (markdown_assets_folder.empty()) {
            (void)v3->ClearVirtualHostNameToFolderMapping(pm::brand::k_markdown_assets_vhost_w);
            return;
        }
        const HRESULT hrMap = v3->SetVirtualHostNameToFolderMapping(
            pm::brand::k_markdown_assets_vhost_w, markdown_assets_folder.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        if (FAILED(hrMap)) {
            vweb_log_warn(std::string("[viewer-web] SetVirtualHostNameToFolderMapping(assets) failed: 0x")
                          + std::to_string(static_cast<uint32_t>(hrMap)) + " reason=" + (reason ? reason : "?")
                          + " folderUtf8=" + vweb_path_w_for_log(markdown_assets_folder));
        }        
    }

    void EnsureNavigated()
    {
        if (!webview || navigated || !m_hostHwnd) return;
        if (!useViewerWebOrigin && deferredHtml.empty()) return;
        if (!::IsWindowVisible(m_hostHwnd)) {
            vweb_log_info("[viewer-web] EnsureNavigated: host not visible yet (defer until WM_SHOWWINDOW / WM_SIZE)");
            return;
        }
        RECT rc{};
        ::GetClientRect(m_hostHwnd, &rc);
        if (rc.right <= 1 || rc.bottom <= 1) {
            vweb_log_info("[viewer-web] EnsureNavigated: host client rect too small (defer)");
            return;
        }
        if (controller) {
            apply_webview2_controller_bounds(controller.Get(), m_hostHwnd);
            controller->put_IsVisible(TRUE);
        }
        if (useViewerWebOrigin) {
            const std::wstring nav = std::wstring(L"https://") + std::wstring(pm::brand::k_viewer_web_vhost_w)
                + L"/viewer.html";
            webview->Navigate(nav.c_str());
        } else {
            const std::wstring whtml = pmui::utf8_to_wide(deferredHtml);
            webview->NavigateToString(whtml.c_str());
        }
        navigated = true;
    }

    /// JS `fetch()` cannot cross `pm-vw` → `pm-md` (CORS). `vw_hosted_read` reads the mapped file on the host.
    static void post_web_message_json(Impl* impl, const nlohmann::json& o);
    static void handle_vw_hosted_read(Impl* impl, const nlohmann::json& j);
    /// Write editor content back to `native_hosted_read_abs_path_w`; replies with `vw_save_file_result`.
    static void handle_vw_save_file(Impl* impl, const nlohmann::json& j);
    /// Apply OpenSCAD preview defines and trigger host-side recompile (`vw_openscad_preview_result`).
    static void handle_vw_openscad_preview(Impl* impl, const nlohmann::json& j);

    /// `Reload()` when the explorer / assets folder changes, or after leaving web preview. Returns true if
    /// the caller must skip `FlushContextToWeb` (deferred to `NavigationCompleted`).
    bool reload_if_mapped_preview_folder_changed(const std::wstring& folder_candidate);
};

namespace {

static std::string vweb_b64_encode_bytes(const std::uint8_t* p, std::size_t len)
{
    static constexpr char kTbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string           out;
    out.reserve((len + 2) / 3 * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        const unsigned n = (static_cast<unsigned>(p[i]) << 16)
            | ((i + 1 < len) ? (static_cast<unsigned>(p[i + 1]) << 8) : 0u)
            | ((i + 2 < len) ? static_cast<unsigned>(p[i + 2]) : 0u);
        out.push_back(kTbl[(n >> 18) & 63u]);
        out.push_back(kTbl[(n >> 12) & 63u]);
        if (i + 1 < len)
            out.push_back(kTbl[(n >> 6) & 63u]);
        else
            out.push_back('=');
        if (i + 2 < len)
            out.push_back(kTbl[n & 63u]);
        else
            out.push_back('=');
    }
    return out;
}

static std::wstring vweb_norm_preview_folder_key(const std::wstring& w)
{
    if (w.empty())
        return {};
    std::error_code ec;
    const fs::path abs = fs::absolute(fs::path(w), ec);
    if (ec)
        return w;
    return abs.lexically_normal().wstring();
}

} // namespace

bool CViewerWebPanel::Impl::reload_if_mapped_preview_folder_changed(const std::wstring& folder_candidate)
{
    const std::wstring key = vweb_norm_preview_folder_key(folder_candidate);
    if (key.empty() || !webview || !ready || !navigated)
        return false;

    if (web_hosted_context_reset_pending) {
        web_hosted_context_reset_pending = false;
        web_last_preview_folder_key = key;
        const HRESULT hr = webview->Reload();
        vweb_log_info(std::string("[viewer-web] Reload() after web preview context clear; next folder keyUtf8Bytes=")
                      + std::to_string(pmui::wide_to_utf8(key).size()));
        (void)hr;
        return true;
    }

    if (web_last_preview_folder_key == key)
        return false;

    const bool had_prior_folder = !web_last_preview_folder_key.empty();
    web_last_preview_folder_key = key;
    if (!had_prior_folder)
        return false;

    const HRESULT hr = webview->Reload();
    vweb_log_info(std::string("[viewer-web] Reload() for explorer preview folder change; newFolderUtf8Bytes=")
                  + std::to_string(pmui::wide_to_utf8(key).size()));
    (void)hr;
    return true;
}

void CViewerWebPanel::Impl::post_web_message_json(Impl* impl, const nlohmann::json& o)
{
    if (!impl || !impl->webview)
        return;
    const std::wstring w = pmui::utf8_to_wide(o.dump());
    (void)impl->webview->PostWebMessageAsString(w.c_str());
}

void CViewerWebPanel::Impl::handle_vw_hosted_read(Impl* impl, const nlohmann::json& j)
{
    const std::string id  = j.value("id", std::string{});
    const std::string url = j.value("url", std::string{});
    auto              reply_err = [&](const char* code) {
        nlohmann::json o;
        o["t"]   = "vw_hosted_read_err";
        o["id"]  = id;
        o["err"] = code;
        CViewerWebPanel::Impl::post_web_message_json(impl, o);
    };
    if (id.empty() || !impl)
        return;
    if (url != impl->hosted_file_url_utf8 && url != impl->three_model_url_utf8) {
        reply_err("url_mismatch");
        return;
    }
    if (impl->native_hosted_read_abs_path_w.empty()) {
        reply_err("no_path");
        return;
    }
    std::error_code ec;
    const fs::path  fp(impl->native_hosted_read_abs_path_w);
    if (!fs::is_regular_file(fp, ec) || ec) {
        reply_err("not_file");
        return;
    }
    const std::uintmax_t sz_um = fs::file_size(fp, ec);
    if (ec || sz_um > static_cast<std::uintmax_t>(UINT64_MAX)) {
        reply_err("stat_failed");
        return;
    }
    const std::uint64_t sz = static_cast<std::uint64_t>(sz_um);
    std::uint64_t       cap = 0;
    if (url == impl->hosted_file_url_utf8)
        cap = impl->hosted_file_max_bytes;
    else
        cap = impl->three_max_bytes;
    if (cap == 0)
        cap = 64ull * 1024 * 1024;
    if (sz > cap) {
        reply_err("too_large");
        return;
    }

    std::ifstream in(impl->native_hosted_read_abs_path_w, std::ios::binary);
    if (!in) {
        reply_err("open_failed");
        return;
    }

    nlohmann::json meta;
    meta["t"]    = "vw_hosted_read_meta";
    meta["id"]   = id;
    meta["size"] = sz;
    CViewerWebPanel::Impl::post_web_message_json(impl, meta);

    static constexpr std::size_t kRawChunk = 512u * 1024u;
    std::vector<std::uint8_t>    buf(kRawChunk);
    std::uint64_t                off = 0;
    while (off < sz) {
        const std::size_t n = static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(kRawChunk), sz - off));
        if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n))) {
            reply_err("read_failed");
            return;
        }
        nlohmann::json part;
        part["t"]   = "vw_hosted_read_chunk";
        part["id"]  = id;
        part["off"] = off;
        part["b64"] = vweb_b64_encode_bytes(buf.data(), n);
        CViewerWebPanel::Impl::post_web_message_json(impl, part);
        off += static_cast<std::uint64_t>(n);
    }

    nlohmann::json done;
    done["t"]  = "vw_hosted_read_done";
    done["id"] = id;
    CViewerWebPanel::Impl::post_web_message_json(impl, done);
}

void CViewerWebPanel::Impl::handle_vw_save_file(Impl* impl, const nlohmann::json& j)
{
    auto reply = [&](bool ok, const char* err_code = nullptr) {
        nlohmann::json o;
        o["t"]  = "vw_save_file_result";
        o["ok"] = ok;
        if (!ok && err_code && *err_code)
            o["err"] = err_code;
        post_web_message_json(impl, o);
    };

    if (!impl) {
        reply(false, "no_impl");
        return;
    }

    const std::string content_utf8 = j.value("content", std::string{});

    // Resolve save path. `native_hosted_read_abs_path_w` is only set for large / URL-hosted
    // files (PDF, 3D, large text). For markdown client-preview and inline-text paths it is
    // intentionally cleared. Fall back to `selection[0]`, which SetContext always populates
    // with the raw file path regardless of preview mode.
    std::wstring save_path_w = impl->native_hosted_read_abs_path_w;
    if (save_path_w.empty() && !impl->selection.empty() && !impl->selection[0].empty()) {
        std::error_code ec;
        const fs::path  abs = fs::absolute(fs::path(impl->selection[0]), ec);
        save_path_w = ec ? impl->selection[0] : abs.lexically_normal().wstring();
    }
    if (save_path_w.empty()) {
        vweb_log_warn("[viewer-web] vw_save_file: no save path (no hostedReadPath, no selection)");
        reply(false, "no_path");
        return;
    }

    const fs::path fp(save_path_w);
    std::error_code ec;
    if (!fs::is_regular_file(fp, ec) || ec) {
        vweb_log_warn(std::string("[viewer-web] vw_save_file: not a regular file: ")
                      + vweb_path_w_for_log(save_path_w));
        reply(false, "not_file");
        return;
    }

    std::ofstream out(fp, std::ios::binary | std::ios::trunc);
    if (!out) {
        vweb_log_warn(std::string("[viewer-web] vw_save_file: open failed: ")
                      + vweb_path_w_for_log(save_path_w));
        reply(false, "open_failed");
        return;
    }
    out.write(content_utf8.data(), static_cast<std::streamsize>(content_utf8.size()));
    out.close();
    if (out.fail()) {
        vweb_log_warn(std::string("[viewer-web] vw_save_file: write failed: ")
                      + vweb_path_w_for_log(save_path_w));
        reply(false, "write_failed");
        return;
    }
    vweb_log_info(std::string("[viewer-web] vw_save_file: saved ")
                  + vweb_path_w_for_log(save_path_w)
                  + " (" + std::to_string(content_utf8.size()) + " B)");
    reply(true);
}

void CViewerWebPanel::Impl::handle_vw_openscad_preview(Impl* impl, const nlohmann::json& j)
{
    auto reply = [&](bool ok, const char* err_code = nullptr) {
        nlohmann::json o;
        o["t"]  = "vw_openscad_preview_result";
        o["ok"] = ok;
        if (!ok && err_code && *err_code)
            o["err"] = err_code;
        post_web_message_json(impl, o);
    };

    if (!impl) {
        reply(false, "no_impl");
        return;
    }

    const std::string defines_utf8 = j.value("defines", std::string{});
    const std::wstring defines_w = pmui::utf8_to_wide(defines_utf8);

    if (!impl->m_hostHwnd || !::IsWindow(impl->m_hostHwnd)) {
        reply(false, "no_host");
        return;
    }
    HWND fvHwnd = ::GetParent(impl->m_hostHwnd);
    if (!fvHwnd || !::IsWindow(fvHwnd)) {
        reply(false, "no_fileviewer");
        return;
    }
    CWnd* pw = CWnd::GetCWndPtr(fvHwnd);
    auto* fv = dynamic_cast<CFileViewer*>(pw);
    if (!fv) {
        reply(false, "no_fileviewer");
        return;
    }
    const bool started = fv->ReloadOpenSCADWithDefines(defines_w);
    if (!started) {
        reply(false, "reload_failed");
    }
}

CViewerWebPanel::CViewerWebPanel() : m_impl(std::make_unique<Impl>())
{
    m_impl->viewer_kind_utf8 = std::string(pmui::viewer_web::k_viewer_kind_markdown);
    char buf[64]; snprintf(buf, sizeof(buf), "[viewer-web] ctor @%p", static_cast<void*>(this));
    vweb_log_info(buf);
}

CViewerWebPanel::~CViewerWebPanel()
{
    {
        char buf[64]; snprintf(buf, sizeof(buf), "[viewer-web] dtor @%p", static_cast<void*>(this));
        vweb_log_info(buf);
    }
    if (HWND h = GetHwnd(); h && ::IsWindow(h)) {
        ::KillTimer(h, kTimerViewerWebChromeResync);
        ::KillTimer(h, kTimerViewerWebBridgeReplay);
    }
    if (m_impl) {
        if (m_impl->busManager) {
            m_impl->busManager->UnregisterExternal("preview", this);
            m_impl->busManager = nullptr;
        }
        RemoveChromeNavMouseSubclass();
        if (m_impl->controller && m_impl->nav_accelerator_registered) {
            (void)m_impl->controller->remove_AcceleratorKeyPressed(m_impl->navAcceleratorToken);
            m_impl->nav_accelerator_registered = false;
        }
        if (m_impl->webview) {
            if (m_impl->web_message_hook_registered)
                m_impl->webview->remove_WebMessageReceived(m_impl->webMessageToken);
            m_impl->webview->remove_NavigationCompleted(m_impl->navCompletedToken);
            m_impl->webview->Stop();
        }
        if (m_impl->controller)
            m_impl->controller->Close();
    }
}

void CViewerWebPanel::ResyncWebViewChrome(ICoreWebView2Controller* ctrl, HWND host)
{
    viewer_resync_webview2_chrome(ctrl, host);
    SyncChromeNavMouseSubclassOnHost();
}

void CViewerWebPanel::RemoveChromeNavMouseSubclass()
{
    if (!m_impl)
        return;
    for (HWND w : m_impl->nav_mouse_subclassed) {
        if (w && ::IsWindow(w))
            (void)::RemoveWindowSubclass(w, &CViewerWebPanel::ChromeNavMouseSubclassProc, kChromeNavMouseSubclassId);
    }
    m_impl->nav_mouse_subclassed.clear();
}

void CViewerWebPanel::SyncChromeNavMouseSubclassOnHost()
{
    RemoveChromeNavMouseSubclass();
    HWND h = GetHwnd();
    if (!m_impl || !h || !::IsWindow(h))
        return;
    (void)::EnumChildWindows(h, &CViewerWebPanel::EnumChromeNavMouseSubclass, reinterpret_cast<LPARAM>(this));
}

bool CViewerWebPanel::TryConsumeCentrePreviewNavFromHostMouse(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& lrOut)
{
    lrOut = 0;
    unsigned xbtn = 0;
    bool     app_cmd = false;
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) {
        xbtn = GET_XBUTTON_WPARAM(wParam);
    } else if (msg == WM_APPCOMMAND) {
        app_cmd = true;
        const UINT cmd = static_cast<UINT>(GET_APPCOMMAND_LPARAM(lParam));
        if (cmd == APPCOMMAND_BROWSER_BACKWARD)
            xbtn = XBUTTON1;
        else if (cmd == APPCOMMAND_BROWSER_FORWARD)
            xbtn = XBUTTON2;
        else
            return false;
    } else {
        return false;
    }

    if (xbtn != XBUTTON1 && xbtn != XBUTTON2)
        return false;

    HWND fvHwnd = ::GetParent(GetHwnd());
    if (!fvHwnd || !::IsWindow(fvHwnd))
        return false;
    CWnd* pw = CWnd::GetCWndPtr(fvHwnd);
    auto* fv = dynamic_cast<CFileViewer*>(pw);
    if (!fv || !fv->TryConsumeImageNavXButtons(xbtn))
        return false;

    lrOut = app_cmd ? TRUE : 0;
    return true;
}

void CViewerWebPanel::RegisterCentrePreviewNavAccelerator(ICoreWebView2Controller* ctrl)
{
    if (!ctrl || !m_impl)
        return;
    const HWND host = GetHwnd();
    Impl* impl = m_impl.get(); // raw ptr — safe: WebView2 lifetime ⊆ panel lifetime
    const HRESULT hrAcc = ctrl->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [host, impl](ICoreWebView2Controller* /*sender*/, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (!args || !host)
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
                // While the Monaco editor is active all cursor/typing keys belong to the editor.
                if (impl->editor_mode_active)
                    return S_OK;
                const bool ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool altDown  = (::GetKeyState(VK_MENU) & 0x8000) != 0;
                HWND       fvHwnd   = ::GetParent(host);
                if (!fvHwnd || !::IsWindow(fvHwnd))
                    return S_OK;
                CWnd* pw = CWnd::GetCWndPtr(fvHwnd);
                auto* fv = dynamic_cast<CFileViewer*>(pw);
                if (!fv)
                    return S_OK;
                if (fv->TryConsumeImageNavKeys(static_cast<WPARAM>(vk), ctrlDown, altDown))
                    (void)args->put_Handled(TRUE);
                return S_OK;
            }).Get(),
        &m_impl->navAcceleratorToken);
    if (SUCCEEDED(hrAcc))
        m_impl->nav_accelerator_registered = true;
    else
        vweb_log_warn(std::string("[viewer-web] add_AcceleratorKeyPressed failed: HRESULT 0x")
                      + std::to_string(static_cast<uint32_t>(hrAcc)));
}

LRESULT CALLBACK CViewerWebPanel::ChromeNavMouseSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)hwnd;
    auto* panel = reinterpret_cast<CViewerWebPanel*>(dwRefData);
    if (panel) {
        LRESULT lr = 0;
        if (panel->TryConsumeCentrePreviewNavFromHostMouse(msg, wParam, lParam, lr))
            return lr;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK CViewerWebPanel::EnumChromeNavMouseSubclass(HWND w, LPARAM lp)
{
    auto* panel = reinterpret_cast<CViewerWebPanel*>(lp);
    if (!panel || !panel->m_impl)
        return TRUE;
    wchar_t cls[160]{};
    if (::GetClassNameW(w, cls, static_cast<int>(std::size(cls))) > 0) {
        const bool chrome_mouse_surface = (wcsstr(cls, L"Chrome_RenderWidgetHostHWND") != nullptr)
            || (wcsstr(cls, L"Chrome_WidgetWin") != nullptr);
        if (chrome_mouse_surface && ::IsWindow(w)) {
            bool already = false;
            for (HWND h : panel->m_impl->nav_mouse_subclassed) {
                if (h == w) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                if (::SetWindowSubclass(w, &CViewerWebPanel::ChromeNavMouseSubclassProc, kChromeNavMouseSubclassId,
                                        reinterpret_cast<DWORD_PTR>(panel)))
                    panel->m_impl->nav_mouse_subclassed.push_back(w);
            }
        }
    }
    (void)::EnumChildWindows(w, &CViewerWebPanel::EnumChromeNavMouseSubclass, lp);
    return TRUE;
}

void CViewerWebPanel::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    cs.style &= ~(WS_BORDER | WS_DLGFRAME);
}

int CViewerWebPanel::OnCreate(CREATESTRUCT&)
{
    m_impl->m_hostHwnd = GetHwnd();
    const std::wstring userData = viewer_webview2_user_data_folder();
    if (userData.empty()) {
        vweb_log_error("[viewer-web] OnCreate: WebView2 user-data folder unresolved");
        ::MessageBoxW(nullptr, L"Could not resolve WebView2 user-data folder for viewer.",
                      pm::brand::k_w_msgbox_viewerweb_title_w, MB_ICONERROR);
        return 0;
    }
    HWND               host   = m_impl->m_hostHwnd;
    Impl*              impl  = m_impl.get();
    CViewerWebPanel*   panel = this;

    auto envCb = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [host, impl, panel](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
                vweb_log_error(std::string("[viewer-web] CreateCoreWebView2Environment failed: HRESULT 0x")
                               + std::to_string(static_cast<uint32_t>(hr)));
                return S_OK;
            }
            return env->CreateCoreWebView2Controller(host,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [host, impl, panel](HRESULT hr2, ICoreWebView2Controller* ctrl) -> HRESULT {
                        if (FAILED(hr2) || !ctrl) {
                            vweb_log_error(std::string("[viewer-web] CreateCoreWebView2Controller failed: HRESULT 0x")
                                           + std::to_string(static_cast<uint32_t>(hr2)));
                            return S_OK;
                        }
                        impl->controller = ctrl;
                        ctrl->get_CoreWebView2(&impl->webview);
                        if (!impl->webview) {
                            vweb_log_error("[viewer-web] get_CoreWebView2 returned null");
                            return S_OK;
                        }
                        {
                            ComPtr<ICoreWebView2Controller4> c4;
                            if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&c4))) && c4)
                                (void)c4->put_AllowExternalDrop(TRUE);
                        }
                        apply_webview2_controller_bounds(ctrl, host);
                        apply_webview2_match_theme_background(ctrl);
                        ComPtr<ICoreWebView2Settings> s;
                        impl->webview->get_Settings(&s);
                        if (s) {
                            s->put_AreDevToolsEnabled(TRUE);
                            s->put_AreDefaultContextMenusEnabled(TRUE);
                            s->put_IsZoomControlEnabled(TRUE);
                            s->put_IsStatusBarEnabled(FALSE);
                            s->put_AreHostObjectsAllowed(FALSE);
                            ComPtr<ICoreWebView2Settings6> s6;
                            if (SUCCEEDED(s.As(&s6)) && s6)
                                (void)s6->put_IsWebMessageEnabled(TRUE);
                            else
                                vweb_log_warn(
                                    "[viewer-web] ICoreWebView2Settings6 not available; JS console bridge may be blocked");
                        }
                        apply_webview2_preferred_color_scheme(impl->webview.Get());

                        const std::wstring appFeaturesJs = pmui::webview_app_features_bootstrap_js();
                        impl->webview->AddScriptToExecuteOnDocumentCreated(appFeaturesJs.c_str(), nullptr);
                        const std::wstring systemContextJs = pmui::webview_system_context_bootstrap_js();
                        impl->webview->AddScriptToExecuteOnDocumentCreated(systemContextJs.c_str(), nullptr);

                        panel->RegisterCentrePreviewNavAccelerator(ctrl);

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
                                        if (t == "cweb_bus") {
                                            if (impl->busManager)
                                                impl->busManager->HandleExternalMessage("preview", u8);
                                        } else if (t == "vw_console") {
                                            const std::string lvl = j.value("level", std::string{"?"});
                                            const std::string msg = j.value("msg", std::string{});
                                            if (lvl == "error")
                                                vweb_log_error(std::string("[viewer-web] js ") + lvl + " "
                                                               + vweb_utf8_limited(msg, 1500));
                                            else
                                                vweb_log_warn(std::string("[viewer-web] js ") + lvl + " "
                                                              + vweb_utf8_limited(msg, 1500));
                                        } else if (t == "vw_hosted_read") {
                                            CViewerWebPanel::Impl::handle_vw_hosted_read(impl, j);
                                        } else if (t == "vw_editor_mode") {
                                            // React tells us when the Monaco editor is the active kind.
                                            // While active we suppress centre-preview arrow-key navigation.
                                            impl->editor_mode_active = j.value("active", false);
                                        } else if (t == "vw_save_file") {
                                            CViewerWebPanel::Impl::handle_vw_save_file(impl, j);
                                        } else if (t == "vw_openscad_preview") {
                                            CViewerWebPanel::Impl::handle_vw_openscad_preview(impl, j);
                                        }
                                    } catch (...) {
                                        vweb_log_info(std::string("[viewer-web] webMessage nonJsonUtf8=")
                                                      + vweb_utf8_limited(u8, 500));
                                    }
                                    return S_OK;
                                }).Get(),
                            &impl->webMessageToken);
                        impl->web_message_hook_registered = true;

                        impl->webview->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [host, impl, panel](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL navOk = 0;
                                    if (args)
                                        (void)args->get_IsSuccess(&navOk);
                                    if (!navOk) {
                                        COREWEBVIEW2_WEB_ERROR_STATUS wes = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                        ComPtr<ICoreWebView2NavigationCompletedEventArgs2> a2;
                                        if (args && SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&a2))) && a2)
                                            (void)a2->get_WebErrorStatus(&wes);
                                        LPWSTR srcFail = nullptr;
                                        std::string srcU8;
                                        if (impl->webview && SUCCEEDED(impl->webview->get_Source(&srcFail)) && srcFail) {
                                            srcU8 = vweb_path_w_for_log(srcFail);
                                            ::CoTaskMemFree(srcFail);
                                        }
                                        vweb_log_warn(std::string("[viewer-web] NavigationCompleted: IsSuccess=false webErrorStatus=")
                                                      + std::to_string(static_cast<int>(wes)) + " sourceUtf8=" + srcU8);
                                        return S_OK;
                                    }
                                    // Skip `about:blank` — only treat real viewer documents as ready for JS.
                                    bool realDoc = false;
                                    LPWSTR srcW = nullptr;
                                    if (impl->webview && SUCCEEDED(impl->webview->get_Source(&srcW)) && srcW) {
                                        if (wcsncmp(srcW, L"about:", 6) != 0)
                                            realDoc = true;
                                        ::CoTaskMemFree(srcW);
                                    }
                                    if (!realDoc) {
                                        if (impl->controller)
                                            panel->ResyncWebViewChrome(impl->controller.Get(), host);
                                        viewer_schedule_chrome_resync(host);
                                        return S_OK;
                                    }
                                    // Do not run `pendingScripts` / `FlushContextToWeb` in the controller callback:
                                    // `Navigate()` is async; `ExecuteScript` before the document loads no-ops and we
                                    // used to `clear()` the queue — startup `--src` markdown never reached React until
                                    // a second Explorer click re-sent context. Drain here after a real navigation.
                                    const size_t pendingBefore = impl->pendingScripts.size();
                                    for (auto& js : impl->pendingScripts)
                                        impl->RunJsNow(js);
                                    impl->pendingScripts.clear();
                                    impl->FlushContextToWeb();
                                    impl->PushFontExtraToWeb();
                                    if (!impl->console_bridge_installed) {
                                        impl->console_bridge_installed = true;
                                        const std::wstring hook
                                            = LR"((function(){if(window.__pmVwLog)return;window.__pmVwLog=1;function p(L,A){try{if(!window.chrome||!window.chrome.webview)return;var m=Array.prototype.join.call(A,' ');if(m.length>1800)m=m.slice(0,1800);window.chrome.webview.postMessage(JSON.stringify({t:'vw_console',level:L,msg:m}));}catch(e){}}var ce=console.error,cw=console.warn;console.error=function(){p('error',arguments);return ce.apply(console,arguments)};console.warn=function(){p('warn',arguments);return cw.apply(console,arguments)};window.addEventListener('error',function(ev){p('error',[String(ev&&ev.error||ev.message),String((ev&&ev.filename)||'')+':'+String((ev&&ev.lineno)||0)]);});window.addEventListener('unhandledrejection',function(ev){p('error',['unhandledrejection',String(ev&&ev.reason)]);});})();)";
                                        impl->RunJsNow(hook);
                                    }
                                    viewer_schedule_bridge_context_replay(host);
                                    if (impl->controller)
                                        panel->ResyncWebViewChrome(impl->controller.Get(), host);
                                    viewer_schedule_chrome_resync(host);
                                    return S_OK;
                                }).Get(), &impl->navCompletedToken);

                        impl->deferredHtml = pmui::load_viewer_web_html();
                        if (impl->deferredHtml.empty()) {
                            vweb_log_error("[viewer-web] viewer.html missing or empty (expected dist/shared/viewer.html)");
                            ::MessageBoxW(host, L"viewer.html is missing or empty (expected dist\\shared\\viewer.html).",
                                          pm::brand::k_w_msgbox_viewerweb_title_w,
                                          MB_ICONERROR);
                            return S_OK;
                        }
                        impl->useViewerWebOrigin = install_viewer_web_virtual_host(impl->webview.Get(), impl->deferredHtml);
                            impl->ApplyMarkdownAssetsFolderMapping("webview_create", L"");
                        impl->ready = true;
                        {
                            char buf[80];
                            snprintf(buf, sizeof(buf), "[viewer-web] WebView2 controller ready @%p hwnd=%p",
                                static_cast<void*>(impl), reinterpret_cast<void*>(host));
                            vweb_log_info(buf);
                        }
                        impl->EnsureNavigated();
                        // Pending `ExecuteScript` / `FlushContextToWeb` is applied from
                        // `NavigationCompleted` once the viewer document exists (see handler above).
                        const auto& pal = pmui::theme_palette();
                        if (impl->controller)
                            panel->ResyncWebViewChrome(impl->controller.Get(), host);
                        pmui::apply_window_theme_recursive(host, pal.dark);
                        viewer_schedule_chrome_resync(host);
                        return S_OK;
                    }).Get());
        });

    const HRESULT hrEnv = ::CreateCoreWebView2EnvironmentWithOptions(nullptr, userData.c_str(), nullptr, envCb.Get());
    if (FAILED(hrEnv)) {
        vweb_log_error(std::string("[viewer-web] CreateCoreWebView2EnvironmentWithOptions failed: HRESULT 0x")
                       + std::to_string(static_cast<uint32_t>(hrEnv)));
    }
    return 0;
}

LRESULT CViewerWebPanel::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_DESTROY:
        {
            char buf[80];
            snprintf(buf, sizeof(buf), "[viewer-web] WM_DESTROY @%p hwnd=%p — closing WebView2",
                static_cast<void*>(this), reinterpret_cast<void*>(GetHwnd()));
            vweb_log_info(buf);
        }
        // Tear down WebView2 while the HWND is still valid so the browser
        // process exits promptly instead of lingering after app shutdown.
        ::KillTimer(GetHwnd(), kTimerViewerWebChromeResync);
        ::KillTimer(GetHwnd(), kTimerViewerWebBridgeReplay);
        RemoveChromeNavMouseSubclass();
        if (m_impl) {
            if (m_impl->busManager)
                m_impl->busManager->UnregisterExternal("preview", this);
            if (m_impl->controller && m_impl->nav_accelerator_registered) {
                (void)m_impl->controller->remove_AcceleratorKeyPressed(m_impl->navAcceleratorToken);
                m_impl->nav_accelerator_registered = false;
            }
            if (m_impl->webview) {
                if (m_impl->web_message_hook_registered) {
                    m_impl->webview->remove_WebMessageReceived(m_impl->webMessageToken);
                    m_impl->web_message_hook_registered = false;
                }
                m_impl->webview->remove_NavigationCompleted(m_impl->navCompletedToken);
                m_impl->webview->Stop();
                m_impl->webview.Reset();
            }
            if (m_impl->controller) {
                m_impl->controller->Close();
                m_impl->controller.Reset();
            }
            m_impl->busManager = nullptr;
        }
        break;

    case WM_APPCOMMAND:
    case WM_XBUTTONDOWN: {
        LRESULT lr = 0;
        if (TryConsumeCentrePreviewNavFromHostMouse(msg, wparam, lparam, lr))
            return lr;
        break;
    }
    case WM_SIZE:
        if (m_impl->controller)
            apply_webview2_controller_bounds(m_impl->controller.Get(), GetHwnd());
        // `EnsureNavigated` may have deferred at controller creation (host rect was 0×0). WM_SHOWWINDOW
        // does not always fire when an existing child is shown again, so retry after layout resizes the host.
        m_impl->EnsureNavigated();
        break;
    case WM_SHOWWINDOW:
        if (wparam) m_impl->EnsureNavigated();
        break;
    case WM_TIMER:
        if (wparam == kTimerViewerWebChromeResync) {
            ::KillTimer(GetHwnd(), kTimerViewerWebChromeResync);
            if (m_impl->controller)
                ResyncWebViewChrome(m_impl->controller.Get(), GetHwnd());
            return 0;
        }
        if (wparam == kTimerViewerWebBridgeReplay) {
            ::KillTimer(GetHwnd(), kTimerViewerWebBridgeReplay);
            m_impl->FlushContextToWeb();
            m_impl->PushFontExtraToWeb();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    }
    return WndProcDefault(msg, wparam, lparam);
}

void CViewerWebPanel::SetContext(const std::vector<std::wstring>& sel, const std::wstring& fld,
                                 std::string_view viewer_kind, std::string_view display_language)
{
    const std::wstring prev_assets_map = m_impl->markdown_assets_folder;
    m_impl->markdown_embed_url_utf8.clear();
    m_impl->markdown_client_utf8.clear();
    m_impl->markdown_base_url_utf8.clear();
    m_impl->three_model_url_utf8.clear();
    m_impl->three_file_name_utf8.clear();
    m_impl->three_file_size_bytes = 0;
    m_impl->three_max_bytes       = 0;
    m_impl->three_error_utf8.clear();
    m_impl->openscad_source_text_utf8.clear();
    m_impl->hosted_file_url_utf8.clear();
    m_impl->hosted_file_text_utf8.clear();
    m_impl->hosted_file_embed_text = false;
    m_impl->hosted_file_name_utf8.clear();
    m_impl->hosted_file_size_bytes = 0;
    m_impl->hosted_file_max_bytes  = 0;
    m_impl->hosted_file_error_utf8.clear();
    m_impl->native_hosted_read_abs_path_w.clear();
    m_impl->selection = sel;
    m_impl->folder    = fld;
    if (!viewer_kind.empty())
        m_impl->viewer_kind_utf8 = std::string(viewer_kind);
    if (!display_language.empty())
        m_impl->ui_locale = std::string(display_language);
    // Only drop the virtual-host folder mapping when the selection is cleared. For non-empty
    // selection, `SetMarkdownClientPreview` / `SetHostedFileClientPreview` / `SetThreeClientPreview`
    // remap immediately after — clearing here caused `ERR_FILE_NOT_FOUND` on in-flight fetches
    // (e.g. `ClearMarkdown` + `LoadText` while switching Explorer picks, or markdown relative links).
    if (sel.empty()) {
        m_impl->markdown_assets_folder.clear();
        m_impl->ApplyMarkdownAssetsFolderMapping("SetContext(empty_sel)", prev_assets_map);
        m_impl->web_last_preview_folder_key.clear();
        m_impl->web_hosted_context_reset_pending = true;
        m_impl->FlushContextToWeb();
    }
    // Non-empty: defer `FlushContextToWeb` to `SetMarkdownClientPreview` / `SetHostedFileClientPreview` /
    // `SetThreeClientPreview` so `setStatus` includes URLs in one shot (and folder `Reload()` can skip it).
}

void CViewerWebPanel::SetDisplayLanguage(std::string_view display_language)
{
    if (!display_language.empty())
        m_impl->ui_locale = std::string(display_language);
    m_impl->FlushContextToWeb();
}

void CViewerWebPanel::SetViewerKind(std::string_view viewer_kind)
{
    if (!viewer_kind.empty())
        m_impl->viewer_kind_utf8 = std::string(viewer_kind);
    m_impl->FlushContextToWeb();
}

void CViewerWebPanel::SetMarkdownClientPreview(std::string_view markdown_utf8,
                                               std::string_view markdown_base_url_https_utf8,
                                               const std::wstring& markdown_assets_folder)
{
    m_impl->markdown_embed_url_utf8.clear();
    m_impl->markdown_client_utf8    = std::string(markdown_utf8);
    m_impl->markdown_base_url_utf8  = std::string(markdown_base_url_https_utf8);
    m_impl->markdown_assets_folder  = markdown_assets_folder;
    m_impl->three_model_url_utf8.clear();
    m_impl->three_file_name_utf8.clear();
    m_impl->three_file_size_bytes = 0;
    m_impl->three_max_bytes       = 0;
    m_impl->three_error_utf8.clear();
    m_impl->openscad_source_text_utf8.clear();
    m_impl->hosted_file_url_utf8.clear();
    m_impl->hosted_file_text_utf8.clear();
    m_impl->hosted_file_embed_text = false;
    m_impl->hosted_file_name_utf8.clear();
    m_impl->hosted_file_size_bytes = 0;
    m_impl->hosted_file_max_bytes  = 0;
    m_impl->hosted_file_error_utf8.clear();
    m_impl->native_hosted_read_abs_path_w.clear();
    m_impl->ApplyMarkdownAssetsFolderMapping("SetMarkdownClientPreview", L"");
    const std::wstring folder_key_src =
        m_impl->markdown_assets_folder.empty() ? m_impl->folder : m_impl->markdown_assets_folder;
    if (!m_impl->reload_if_mapped_preview_folder_changed(folder_key_src)) {
        m_impl->FlushContextToWeb();
    } else {
        vweb_log_info("[viewer-web] Reload() for folder/context reset; deferring FlushContextToWeb to NavigationCompleted");
    }
}

void CViewerWebPanel::RefreshChromeForTheme()
{
    m_impl->PushFontExtraToWeb();
    if (m_impl->controller)
        ResyncWebViewChrome(m_impl->controller.Get(), GetHwnd());
    viewer_schedule_chrome_resync(GetHwnd());
    if (m_impl->ready)
        m_impl->FlushContextToWeb();
}

void CViewerWebPanel::PostRawJson(std::string_view json_utf8)
{
    if (!m_impl || !m_impl->webview) return;
    const std::wstring w = pmui::utf8_to_wide(std::string(json_utf8));
    (void)m_impl->webview->PostWebMessageAsString(w.c_str());
}

void CViewerWebPanel::SetBusManager(CWebViewManager* manager)
{
    if (!m_impl)
        return;
    if (m_impl->busManager && m_impl->busManager != manager)
        m_impl->busManager->UnregisterExternal("preview", this);
    m_impl->busManager = manager;
    if (m_impl->busManager) {
        m_impl->busManager->RegisterExternal("preview", this,
            [this]() {
                return IsWindow() != FALSE;
            },
            [this](const std::string& json_utf8) {
                PostRawJson(json_utf8);
            });
    }
}

std::wstring CViewerWebPanel::SelectionPath() const
{
    if (!m_impl || m_impl->selection.empty()) return {};
    return m_impl->selection[0];
}

void CViewerWebPanel::SetThreeClientPreview(const std::wstring& model_file_path, std::uintmax_t file_size_bytes,
                                            std::string_view error_utf8)
{
    m_impl->markdown_embed_url_utf8.clear();
    m_impl->markdown_client_utf8.clear();
    m_impl->markdown_base_url_utf8.clear();
    m_impl->hosted_file_url_utf8.clear();
    m_impl->hosted_file_text_utf8.clear();
    m_impl->hosted_file_embed_text = false;
    m_impl->hosted_file_name_utf8.clear();
    m_impl->hosted_file_size_bytes = 0;
    m_impl->hosted_file_max_bytes  = 0;
    m_impl->hosted_file_error_utf8.clear();
    m_impl->native_hosted_read_abs_path_w.clear();

    const fs::path mp(model_file_path);
    const std::wstring fname_w = mp.filename().wstring();
    m_impl->three_file_name_utf8 = pmui::wide_to_utf8(fname_w);
    m_impl->three_file_size_bytes =
        file_size_bytes > static_cast<std::uintmax_t>(UINT64_MAX) ? UINT64_MAX : static_cast<std::uint64_t>(file_size_bytes);

    std::wstring extw = mp.extension().wstring();
    for (auto& ch : extw)
        ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
    const std::uintmax_t max_b = pmui::viewer_3d_max_bytes_for_ext(extw);
    m_impl->three_max_bytes = max_b > UINT64_MAX ? UINT64_MAX : static_cast<std::uint64_t>(max_b);

    m_impl->three_model_url_utf8.clear();
    m_impl->three_error_utf8.clear();

    if (!error_utf8.empty()) {
        m_impl->three_error_utf8.assign(error_utf8.data(), error_utf8.size());
        m_impl->native_hosted_read_abs_path_w.clear();
        const std::wstring prev_assets = m_impl->markdown_assets_folder;
        m_impl->markdown_assets_folder.clear();
        m_impl->ApplyMarkdownAssetsFolderMapping("SetThreeClientPreview_error", prev_assets);
        m_impl->FlushContextToWeb();
        return;
    }

    std::wstring folder_w = mp.parent_path().wstring();
    if (folder_w.empty())
        folder_w = L".";
    std::error_code ec;
    const fs::path abs_parent = fs::absolute(mp.parent_path(), ec);
    m_impl->markdown_assets_folder = (!ec && !abs_parent.empty()) ? abs_parent.wstring() : folder_w;

    m_impl->ApplyMarkdownAssetsFolderMapping("SetThreeClientPreview", L"");
    {
        std::string url = std::string("https://")
            + pmui::wide_to_utf8(std::wstring(pm::brand::k_markdown_assets_vhost_w)) + "/";
        append_pct_encoded_utf8_path_segment(url, fname_w);
        m_impl->three_model_url_utf8 = std::move(url);
    }
    m_impl->native_hosted_read_abs_path_w = mp.wstring();
    if (!m_impl->reload_if_mapped_preview_folder_changed(m_impl->markdown_assets_folder)) {
        m_impl->FlushContextToWeb();
    }
}

void CViewerWebPanel::SetOpenSCADSourceText(std::string_view scad_utf8)
{
    m_impl->openscad_source_text_utf8.assign(scad_utf8.data(), scad_utf8.size());
    m_impl->FlushContextToWeb();
}

void CViewerWebPanel::SetHostedFileClientPreview(const std::wstring& file_path, std::uintmax_t file_size_bytes,
                                                 std::string_view viewer_kind, std::string_view error_utf8,
                                                 const std::string* inline_text_utf8)
{
    m_impl->markdown_embed_url_utf8.clear();
    m_impl->markdown_client_utf8.clear();
    m_impl->markdown_base_url_utf8.clear();
    m_impl->three_model_url_utf8.clear();
    m_impl->three_file_name_utf8.clear();
    m_impl->three_file_size_bytes = 0;
    m_impl->three_max_bytes       = 0;
    m_impl->three_error_utf8.clear();
    m_impl->openscad_source_text_utf8.clear();
    m_impl->hosted_file_url_utf8.clear();
    m_impl->hosted_file_text_utf8.clear();
    m_impl->hosted_file_embed_text = false;
    m_impl->hosted_file_name_utf8.clear();
    m_impl->hosted_file_size_bytes = 0;
    m_impl->hosted_file_max_bytes  = 0;
    m_impl->hosted_file_error_utf8.clear();
    m_impl->native_hosted_read_abs_path_w.clear();

    std::error_code abs_ec;
    fs::path        mp(file_path);
    const fs::path  abs_mp = fs::absolute(mp, abs_ec);
    if (!abs_ec)
        mp = abs_mp;

    const std::wstring fname_w = mp.filename().wstring();
    m_impl->hosted_file_name_utf8 = pmui::wide_to_utf8(fname_w);
    m_impl->hosted_file_size_bytes =
        file_size_bytes > static_cast<std::uintmax_t>(UINT64_MAX) ? UINT64_MAX : static_cast<std::uint64_t>(file_size_bytes);

    const std::uintmax_t max_b = pmui::viewer_document_max_bytes_for_path_w(mp.wstring());
    m_impl->hosted_file_max_bytes = max_b > UINT64_MAX ? UINT64_MAX : static_cast<std::uint64_t>(max_b);

    if (!viewer_kind.empty())
        m_impl->viewer_kind_utf8 = std::string(viewer_kind);

    if (!error_utf8.empty()) {
        m_impl->hosted_file_error_utf8.assign(error_utf8.data(), error_utf8.size());
        m_impl->native_hosted_read_abs_path_w.clear();
        const std::wstring prev_assets = m_impl->markdown_assets_folder;
        m_impl->markdown_assets_folder.clear();
        m_impl->ApplyMarkdownAssetsFolderMapping("SetHostedFileClientPreview_error", prev_assets);
        m_impl->FlushContextToWeb();
        return;
    }

    const bool inline_text = inline_text_utf8 != nullptr && viewer_kind == pmui::viewer_web::k_viewer_kind_text;
    if (inline_text) {
        const std::wstring prev_assets = m_impl->markdown_assets_folder;
        m_impl->hosted_file_embed_text  = true;
        m_impl->hosted_file_text_utf8   = *inline_text_utf8;
        m_impl->native_hosted_read_abs_path_w.clear();
        m_impl->markdown_assets_folder.clear();
        m_impl->ApplyMarkdownAssetsFolderMapping("SetHostedFileClientPreview_inline", prev_assets);
        if (!m_impl->reload_if_mapped_preview_folder_changed(m_impl->folder)) {
            m_impl->FlushContextToWeb();
        } else {
            vweb_log_info("[viewer-web] Reload() for folder/context reset; deferring FlushContextToWeb to NavigationCompleted");
        }
        return;
    }

    std::wstring folder_w = mp.parent_path().wstring();
    if (folder_w.empty())
        folder_w = L".";
    std::error_code ec;
    const fs::path abs_parent = fs::absolute(mp.parent_path(), ec);
    m_impl->markdown_assets_folder = (!ec && !abs_parent.empty()) ? abs_parent.wstring() : folder_w;

    m_impl->ApplyMarkdownAssetsFolderMapping("SetHostedFileClientPreview", L"");
    {
        std::string url = std::string("https://")
            + pmui::wide_to_utf8(std::wstring(pm::brand::k_markdown_assets_vhost_w)) + "/";
        append_pct_encoded_utf8_path_segment(url, fname_w);
        m_impl->hosted_file_url_utf8 = std::move(url);
    }
    m_impl->native_hosted_read_abs_path_w = mp.wstring();
    if (!m_impl->reload_if_mapped_preview_folder_changed(m_impl->markdown_assets_folder)) {
        m_impl->FlushContextToWeb();
    } else {
        vweb_log_info("[viewer-web] Reload() for folder/context reset; deferring FlushContextToWeb to NavigationCompleted");
    }
}
