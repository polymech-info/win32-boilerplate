// Windows.Graphics.Capture (main window) + Media Foundation H.264 MP4.
// Interop: vendored robmikh.common (see src/session/recorder/vendor).

#include "pm_session_video_recorder.hpp"

#if defined(_WIN32) && defined(FEATURE_SESSION_VIDEO_RECORDER)

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <d3d11_4.h>
#  include <dxgi1_2.h>
#  include <unknwn.h>
// C++/WinRT (Graphics.Capture) — one consistent order before interop shims; robmikh
// `direct3d11.interop.h` includes the D3D11 WinRT header again (include-guarded).
#  include <winrt/base.h>
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Graphics.Capture.h>
#  include <winrt/Windows.Graphics.DirectX.h>
#  include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#  include <mfapi.h>
#  include <mfidl.h>      // before mfreadwrite (IMF* / IMFSinkWriter)
#  include <mfreadwrite.h>
#  include <mferror.h>

#  include <robmikh.common/capture.desktop.interop.h>
#  include <robmikh.common/direct3d11.interop.h> // CreateDirect3D11DeviceFromDXGIDevice, GetDXGIInterfaceFromObject

#  include <atomic>
#  include <cstdint>
#  include <utility>
#  include <iomanip>
#  include <memory>
#  include <mutex>
#  include <sstream>
#  include <string>

#  include "log_sink.h"

#  pragma comment(lib, "d3d11.lib")
#  pragma comment(lib, "dxgi.lib")
#  pragma comment(lib, "mfplat.lib")
#  pragma comment(lib, "mfreadwrite.lib")
#  pragma comment(lib, "mfuuid.lib")
#  pragma comment(lib, "strmiids.lib")
#  pragma comment(lib, "windowsapp.lib")
#  pragma comment(lib, "RuntimeObject.lib")

using WgcFramePool  = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using DxpPixelFormat  = winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using IWinRtD3d11Device     = winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

namespace media::win::recorder
{
namespace detail
{
constexpr UINT32 kFps             = 30U;
const LONGLONG   kHnsFrameDur     = 10'000'000LL / (LONGLONG)kFps; // 100ns
constexpr UINT32 kH264AvgBitrate  = 8'000'000U;
constexpr int    kH264MaxDim      = 4096;
constexpr int    kH264MaxPixels   = 3840 * 2160;

static std::wstring wide_from_utf8_lossy(const std::string& s)
{
    if (s.empty())
        return {};
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0)
        return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(need), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need) <= 0)
        return std::wstring(s.begin(), s.end());
    return out;
}

// UI-thread session video start: STA + C++/WinRT. Do **not** switch this thread to MTA for capture — the
// shell (Win32++/Ribbon) expects a single-threaded apartment here; use frame-pool `Create` when
// `CreateFreeThreaded` misbehaves instead of init_apartment(multi_threaded) on the UI thread.
void ensure_com_st_and_winrt()
{
    static std::atomic<bool> done{ false };
    if (done.load(std::memory_order_acquire)) return;
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (const winrt::hresult_error& e) {
        if (e.code() == 0x80010106) { /* RPC_E_CHANGED_MODE: COM + WinRT already inited for this thread */ }
        else
            throw;
    }
    done.store(true, std::memory_order_release);
}

/// In-app Log panel (see `log_sink.cpp` / `LogPanel.cpp` AppendLine + cwd file) — any thread.
static void ui_log_session_video(std::string msg)
{
    if (msg.rfind("[session video]", 0) != 0) msg = std::string("[session video] ") + std::move(msg);
    pmui::append_pm_image_log_file_line_wide(wide_from_utf8_lossy(msg));
    // Single path: same as spdlog UI sink (UWM -> LogMessage -> CLogView::AppendLine + cwd file in LogPanel).
    pmui::post_log_panel_line_utf8(msg);
}

static const char* hresult_hint_un(unsigned int c)
{
    switch (c) {
        case 0x80004002:
            return " (E_NOINTERFACE — WGC: D3D/WinRT device bridge, frame pool, or optional session API on this OS.)";
        case 0x80010106: return " (RPC_E_CHANGED_MODE — COM already initialized on this thread)";
        default:         return "";
    }
}

static bool validate_h264_frame_size(int w, int h, std::string& why)
{
    if (w < 2 || h < 2) {
        why = "encoded frame is too small after even-size crop";
        return false;
    }
    if (w > kH264MaxDim || h > kH264MaxDim ||
        static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) > kH264MaxPixels) {
        std::ostringstream o;
        o << "capture frame " << w << "x" << h
          << " is above the guarded H.264 limit (" << kH264MaxDim << " px per side, "
          << kH264MaxPixels << " pixels). Reduce the window size or record on a lower-scale monitor.";
        why = o.str();
        return false;
    }
    return true;
}

/// Optional `GraphicsCaptureSession` properties; missing contract → E_NOINTERFACE — log and continue.
template<typename F>
static void wgc_session_opt(F&& op, const char* ok_line, const char* skip_line)
{
    try {
        std::forward<F>(op)();
        ui_log_session_video(ok_line);
    } catch (const winrt::hresult_error& e) {
        if (e.code() != 0x80004002) throw;
        ui_log_session_video(skip_line);
    }
}

std::atomic<int> s_mf_users{ 0 };
void            mf_retain() noexcept
{
    if (s_mf_users.fetch_add(1) + 1 == 1) (void)MFStartup(MF_VERSION, MFSTARTUP_FULL);
}
void mf_release() noexcept
{
    const int n = s_mf_users.fetch_sub(1) - 1;
    if (n == 0) MFShutdown();
}

/// D3D for WGC: BGRA + video, feature levels 11.1/11.0, hardware then WARP (same idea as robmikh).
static winrt::com_ptr<ID3D11Device> create_d3d11_device_for_wgc(winrt::com_ptr<ID3D11DeviceContext>& out_ctx)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#  if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#  endif
    D3D_FEATURE_LEVEL       levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    constexpr UINT          nlevels  = sizeof(levels) / sizeof(levels[0]);
    winrt::com_ptr<ID3D11Device> dev;
    D3D_FEATURE_LEVEL            got{};
    HRESULT                      hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, nlevels, D3D11_SDK_VERSION, dev.put(), &got, out_ctx.put());
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, nlevels, D3D11_SDK_VERSION, dev.put(), &got, out_ctx.put());
    }
    winrt::check_hresult(hr);
    return dev;
}

class RecSession;

std::mutex        g_mux;
std::shared_ptr<RecSession> g_sess;

class RecSession : public std::enable_shared_from_this<RecSession>
{
public:
    static std::shared_ptr<RecSession> make() { return std::shared_ptr<RecSession>(new RecSession); }

    void start_capture(HWND hwnd, const std::filesystem::path& out, std::string& /*err*/)
    {
        m_outPath  = out;
        m_stopping = false;
        m_paused   = false;
        m_encW     = 0;
        m_encH     = 0;
        m_loggedFirstFrame   = false;
        m_loggedEvenCrop     = false;
        m_logFrameIndexMod30 = 0;
        { const auto _u8 = out.u8string(); ui_log_session_video(std::string("starting capture, output: ") + std::string(reinterpret_cast<const char*>(_u8.data()), _u8.size())); }
        ui_log_session_video("D3D11: D3D11CreateDevice (BGRA+VIDEO, FL 11.1/11.0, HW then WARP)…");
        m_d3d = create_d3d11_device_for_wgc(m_ctx);
        {
            // WGC / free-threaded pool may use the D3D device from worker threads; device hosts ID3D11Multithread.
            winrt::com_ptr<ID3D11Multithread> mthread;
            if (SUCCEEDED(m_d3d->QueryInterface(IID_PPV_ARGS(mthread.put())))) {
                mthread->SetMultithreadProtected(TRUE);
                ui_log_session_video("D3D11: SetMultithreadProtected(TRUE) on device");
            } else if (SUCCEEDED(m_ctx->QueryInterface(IID_PPV_ARGS(mthread.put())))) {
                mthread->SetMultithreadProtected(TRUE);
                ui_log_session_video("D3D11: SetMultithreadProtected(TRUE) on immediate context (fallback QI)");
            } else
                ui_log_session_video("D3D11: no ID3D11Multithread (unexpected) — WGC may fail on some stacks");
        }
        ui_log_session_video("D3D11: device + immediate context ready");
        {
            winrt::com_ptr<IDXGIDevice>      dxgi = m_d3d.as<IDXGIDevice>();
            winrt::com_ptr<::IInspectable>  insp;
            ui_log_session_video("WinRT: CreateDirect3D11DeviceFromDXGIDevice (explicit IInspectable*)…");
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));
            m_wrDevice = insp.as<IWinRtD3d11Device>();
        }
        ui_log_session_video("WGC: CreateCaptureItemForWindow…");
        m_item = robmikh::common::desktop::CreateCaptureItemForWindow(hwnd);
        // BGRA8 — WinRT exposes this as B8G8R8A8UIntNormalized (there is no separate B8G8R8A8UInt in the projection here).
        m_fmt = DxpPixelFormat::B8G8R8A8UIntNormalized;
        const auto sz = m_item.Size();
        int        pw = (sz.Width > 0) ? sz.Width : 1;
        int        ph = (sz.Height > 0) ? sz.Height : 1;
        m_poolW     = pw;
        m_poolH     = ph;
        {
            std::ostringstream o;
            o << "WGC: item " << sz.Width << "x" << sz.Height << ", pool (initial) " << pw << "x" << ph
              << ", B8G8R8A8UIntNormalized, buffers=2";
            ui_log_session_video(o.str());
        }
        const auto initialSize = m_item.Size();

        // Prefer Create (UI / dispatcher-friendly) first; CreateFreeThreaded if Create fails
        // (E_NOINTERFACE / apartment / driver quirks). See feedback: isolate free-threading issues.
        ui_log_session_video("WGC step: Direct3D11CaptureFramePool::Create (UI-thread delivery)…");
        try {
            m_pool = WgcFramePool::Create(m_wrDevice, m_fmt, 2, initialSize);
            ui_log_session_video("WGC: frame pool = Create (ok)");
        } catch (const winrt::hresult_error& e) {
            const auto c = static_cast<unsigned int>(e.code() & 0xFFFFFFFFU);
            std::ostringstream o;
            o << "WGC: Create failed, hresult=0x" << std::hex << std::setfill('0') << std::setw(8) << c << std::dec
              << " — try CreateFreeThreaded…";
            ui_log_session_video(o.str());
            m_pool = WgcFramePool::CreateFreeThreaded(m_wrDevice, m_fmt, 2, initialSize);
            ui_log_session_video("WGC: frame pool = CreateFreeThreaded (ok)");
        }

        ui_log_session_video("WGC step: CreateCaptureSession(item)…");
        m_session = m_pool.CreateCaptureSession(m_item);
        ui_log_session_video("WGC: CreateCaptureSession ok — optional border/cursor session properties…");
        wgc_session_opt(
            [this] { m_session.IsBorderRequired(false); },
            "WGC: IsBorderRequired(false) ok",
            "WGC: IsBorderRequired skipped (E_NOINTERFACE)");
        wgc_session_opt(
            [this] { m_session.IsCursorCaptureEnabled(true); },
            "WGC: IsCursorCaptureEnabled(true) ok",
            "WGC: IsCursorCaptureEnabled skipped (E_NOINTERFACE — pointer may be missing on older Windows)");

        ui_log_session_video("WGC step: FrameArrived handler…");
        const std::weak_ptr<RecSession> weak{ shared_from_this() };
        m_pool.FrameArrived(
            [weak](WgcFramePool const& pool, winrt::Windows::Foundation::IInspectable const&) {
                if (const auto s = weak.lock()) s->on_frame(pool);
            });

        ui_log_session_video("WGC step: StartCapture()…");
        m_session.StartCapture();
        ui_log_session_video("WGC: StartCapture() returned; capture active");
    }

    void on_frame(WgcFramePool const& pool)
    {
        try {
        if (m_stopping.load(std::memory_order_relaxed)) return;
        const auto fr = pool.TryGetNextFrame();
        if (!fr) return;
        if (m_stopping.load(std::memory_order_relaxed)) return;
        if (m_paused.load(std::memory_order_relaxed)) return;

        const auto cs  = fr.ContentSize();
        const int  cx  = cs.Width;
        const int  cy  = cs.Height;
        if (cx <= 0 || cy <= 0) return;
        const int encCx = cx & ~1;
        const int encCy = cy & ~1;
        if ((encCx != cx || encCy != cy) && !m_loggedEvenCrop) {
            m_loggedEvenCrop = true;
            std::ostringstream o;
            o << "WGC frame " << cx << "x" << cy << " cropped to even H.264 size "
              << encCx << "x" << encCy;
            ui_log_session_video(o.str());
        }
        std::string sizeErr;
        if (!validate_h264_frame_size(encCx, encCy, sizeErr)) {
            m_lastErr = sizeErr;
            err_stop(m_lastErr.c_str());
            return;
        }

        if (m_encW > 0 && (encCx != m_encW || encCy != m_encH)) {
            std::ostringstream o;
            o << "size changed to " << cx << "x" << cy << " (encoded " << encCx << "x" << encCy << ")"
              << " (encoder " << m_encW << "x" << m_encH << ") — dropping frames; stop and restart to re-encode";
            ui_log_session_video(o.str());
            return;
        }

        if (cx != m_poolW || cy != m_poolH) {
            try {
                pool.Recreate(m_wrDevice, m_fmt, 2, winrt::Windows::Graphics::SizeInt32{ cx, cy });
                m_poolW = cx;
                m_poolH = cy;
            } catch (const winrt::hresult_error&) { err_stop("WGC frame pool Recreate"); return; }
        }

        if (!m_sink) {
            {
                std::ostringstream o;
                o << "first frame " << cx << "x" << cy << " (encoded " << encCx << "x" << encCy
                  << ") — init MF sink (H.264) + D3D staging…";
                ui_log_session_video(o.str());
            }
            if (!init_encoder(encCx, encCy)) {
                err_stop(m_lastErr.c_str());
                return;
            }
        }
        if (!m_loggedFirstFrame) {
            m_loggedFirstFrame = true;
            { const auto _u8 = m_outPath.u8string(); ui_log_session_video(std::string("writing samples to: ") + std::string(reinterpret_cast<const char*>(_u8.data()), _u8.size())); }
        }

        winrt::com_ptr<ID3D11Texture2D> srcTex;
        try {
            srcTex = GetDXGIInterfaceFromObject<ID3D11Texture2D>(fr.Surface());
        } catch (const winrt::hresult_error&) {
            err_stop("GetDXGIInterfaceFromObject(texture)");
            return;
        }
        const D3D11_BOX srcBox{ 0, 0, 0, static_cast<UINT>(encCx), static_cast<UINT>(encCy), 1 };
        m_ctx->CopySubresourceRegion(m_staging.get(), 0, 0, 0, 0, srcTex.get(), 0, &srcBox);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(m_ctx->Map(m_staging.get(), 0, D3D11_MAP_READ, 0, &map))) {
            err_stop("Map staging");
            return;
        }
        const bool ok = push_rgb32_frame(map, encCx, encCy);
        m_ctx->Unmap(m_staging.get(), 0);
        if (!ok) err_stop("WriteSample");        
        } catch (const winrt::hresult_error& e) {
            const auto c = static_cast<unsigned int>(e.code() & 0xFFFFFFFFU);
            std::ostringstream o;
            o << "FrameArrived exception hresult=0x" << std::hex << std::setfill('0') << std::setw(8) << c
              << std::dec << hresult_hint_un(c);
            err_stop(o.str().c_str());
        } catch (const std::exception& ex) {
            err_stop(ex.what() ? ex.what() : "FrameArrived std::exception");
        } catch (...) {
            err_stop("FrameArrived unknown exception");
        }
    }

    void stop_and_finalize() noexcept
    {
        m_stopping = true;
        try {
            if (m_session) m_session.Close();
        } catch (...) { /* ignore */ }
        m_session = nullptr;
        try {
            if (m_pool) m_pool.Close();
        } catch (...) { /* ignore */ }
        m_pool = nullptr;
        if (m_sink) {
            (void)m_sink->Finalize();
            m_sink = nullptr;
        }
        m_staging   = nullptr;
        m_d3d       = nullptr;
        m_ctx       = nullptr;
        m_item      = nullptr;
        m_wrDevice  = nullptr;
    }

    bool active() const noexcept
    {
        // After err_stop, session is gone even if the shared_ptr is momentarily not cleared.
        return !m_stopping.load() && (m_session != nullptr);
    }

    void toggle_pause() noexcept
    {
        const bool prev = m_paused.load(std::memory_order_relaxed);
        m_paused.store(!prev, std::memory_order_relaxed);
        if (!prev)
            ui_log_session_video("paused (Ctrl+Alt+P to resume; paused time omitted from MP4 timeline)");
        else
            ui_log_session_video("resumed");
    }

    bool paused() const noexcept { return m_paused.load(std::memory_order_relaxed); }

    ~RecSession()
    {
        if (m_session || m_pool || m_sink) stop_and_finalize();
    }

private:
    RecSession() = default;

    void err_stop(const char* w) noexcept
    {
        ui_log_session_video(std::string("error, stopping: ") + (w ? w : "stop"));
        m_stopping = true;
        stop_and_finalize();
        {
            std::lock_guard<std::mutex> l(g_mux);
            if (g_sess.get() == this) g_sess.reset();
        }
        mf_release();
    }

    bool init_encoder(int w, int h)
    {
        m_encW  = w;
        m_encH  = h;
        m_frameIdx = 0;
        std::string sizeErr;
        if (!validate_h264_frame_size(w, h, sizeErr)) {
            m_lastErr = sizeErr;
            return false;
        }

        winrt::com_ptr<IMFSinkWriter> sw;
        if (FAILED(MFCreateSinkWriterFromURL(m_outPath.c_str(), nullptr, nullptr, sw.put()))) {
            m_lastErr = "MFCreateSinkWriterFromURL";
            return false;
        }
        winrt::com_ptr<IMFMediaType> outT;
        if (FAILED(MFCreateMediaType(outT.put())) || FAILED(outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(outT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) || FAILED(MFSetAttributeSize(outT.get(), MF_MT_FRAME_SIZE, (UINT)w, (UINT)h)) ||
            FAILED(MFSetAttributeRatio(outT.get(), MF_MT_FRAME_RATE, kFps, 1)) || FAILED(
                outT->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) || FAILED(
                outT->SetUINT32(MF_MT_AVG_BITRATE, kH264AvgBitrate))) {
            m_lastErr = "H.264 output type";
            return false;
        }
        if (FAILED(sw->AddStream(outT.get(), &m_outStreamIndex))) {
            m_lastErr = "AddStream";
            return false;
        }
        winrt::com_ptr<IMFMediaType> inT;
        if (FAILED(MFCreateMediaType(inT.put())) || FAILED(inT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) || FAILED(
                inT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) || FAILED(MFSetAttributeSize(inT.get(), MF_MT_FRAME_SIZE, (UINT)w, (UINT)h)) ||
            FAILED(MFSetAttributeRatio(inT.get(), MF_MT_FRAME_RATE, kFps, 1)) || FAILED(
                inT->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT)w * 4))) {
            m_lastErr = "RGB32 input type";
            return false;
        }
        if (FAILED(sw->SetInputMediaType(m_outStreamIndex, inT.get(), nullptr))) {
            m_lastErr = "SetInputMediaType (RGB32→H.264) failed";
            return false;
        }
        if (FAILED(sw->BeginWriting())) {
            m_lastErr = "BeginWriting";
            return false;
        }
        m_sink = std::move(sw);
        {
            std::ostringstream o;
            o << "MF sink ready: H.264, " << w << "x" << h << ", " << kFps << " fps, RGB32 in";
            ui_log_session_video(o.str());
        }
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width          = (UINT)w;
            td.Height         = (UINT)h;
            td.MipLevels      = 1;
            td.ArraySize      = 1;
            td.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc     = { 1, 0 };
            td.Usage          = D3D11_USAGE_STAGING;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(m_d3d->CreateTexture2D(&td, nullptr, m_staging.put()))) {
                m_lastErr = "CreateTexture2D";
                m_sink  = nullptr;
                return false;
            }
        }
        return true;
    }

    bool push_rgb32_frame(const D3D11_MAPPED_SUBRESOURCE& map, int w, int h) noexcept
    {
        const LONGLONG t0  = m_frameIdx * kHnsFrameDur;
        winrt::com_ptr<IMFMediaBuffer> buf;
        const DWORD   nbytes = (DWORD)w * (DWORD)h * 4U;
        if (FAILED(MFCreateMemoryBuffer(nbytes, buf.put()))) return false;
        winrt::com_ptr<IMFSample> s;
        if (FAILED(MFCreateSample(s.put())) || FAILED(s->AddBuffer(buf.get()))) return false;
        s->SetSampleTime(t0);
        s->SetSampleDuration(kHnsFrameDur);
        BYTE* dst0 = nullptr;
        DWORD mlen = 0, clen = 0;
        if (FAILED(buf->Lock(&dst0, &mlen, &clen))) return false;
        for (int y = 0; y < h; ++y) {
            const auto* srow   = (const std::uint8_t*)map.pData + (size_t)y * (size_t)map.RowPitch;
            auto*       drow   = (std::uint8_t*)dst0 + (size_t)y * (size_t)w * 4U;
            memcpy(drow, srow, (size_t)w * 4U);
        }
        (void)buf->Unlock();
        (void)buf->SetCurrentLength(nbytes);
        ++m_frameIdx;
        return SUCCEEDED(m_sink->WriteSample(m_outStreamIndex, s.get()));
    }

    std::atomic_bool          m_stopping{ false };
    std::atomic_bool          m_paused{ false };
    int                       m_encW{ 0 };
    int                       m_encH{ 0 };
    int                       m_poolW{ 0 };
    int                       m_poolH{ 0 };
    LONGLONG                  m_frameIdx{ 0 };
    bool                      m_loggedFirstFrame{ false };
    bool                      m_loggedEvenCrop{ false };
    int                       m_logFrameIndexMod30{ 0 };
    std::string               m_lastErr;
    std::filesystem::path     m_outPath;

    winrt::com_ptr<ID3D11Device>  m_d3d;
    winrt::com_ptr<ID3D11DeviceContext> m_ctx;
    winrt::com_ptr<ID3D11Texture2D>   m_staging;
    IWinRtD3d11Device  m_wrDevice{ nullptr };
    DxpPixelFormat  m_fmt{};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem     m_item{ nullptr };
    WgcFramePool  m_pool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession  m_session{ nullptr };
    winrt::com_ptr<IMFSinkWriter> m_sink;
    DWORD m_outStreamIndex{ 0 };
};

bool start_impl(HWND main_window, const std::filesystem::path& out_mp4, std::string& err_utf8)
{
    if (!main_window) {
        err_utf8 = "session video: no HWND";
        ui_log_session_video(err_utf8);
        return false;
    }
    err_utf8.clear();
    try {
        ensure_com_st_and_winrt();
    } catch (const winrt::hresult_error& e) {
        const auto c = static_cast<unsigned int>(e.code() & 0xFFFFFFFFU);
        std::ostringstream o;
        o << "session video: winrt::init_apartment failed, hresult=0x" << std::hex << std::setfill('0') << std::setw(8) << c
          << std::dec << hresult_hint_un(c);
        err_utf8 = o.str();
        ui_log_session_video(err_utf8);
        return false;
    }
    {
        std::lock_guard<std::mutex> l(g_mux);
        if (g_sess) {
            err_utf8 = "session video: already recording";
            ui_log_session_video("start rejected: already recording");
            return false;
        }
    }
    ui_log_session_video("Media Foundation: MFStartup (ref)");
    mf_retain();

    const std::shared_ptr<RecSession> sess = RecSession::make();
    {
        std::lock_guard<std::mutex> l(g_mux);
        g_sess = sess;
    }
    try {
        sess->start_capture(main_window, out_mp4, err_utf8);
    } catch (const winrt::hresult_error& e) {
        const auto    c  = static_cast<unsigned int>(e.code() & 0xFFFFFFFFU);
        std::ostringstream o;
        o << "session video: hresult=0x" << std::hex << std::setfill('0') << std::setw(8) << c << std::dec << hresult_hint_un(c);
        err_utf8 = o.str();
        ui_log_session_video(err_utf8);
        { std::lock_guard<std::mutex> l(g_mux); g_sess.reset(); }
        mf_release();
        return false;
    } catch (const std::exception& ex) {
        err_utf8   = ex.what() ? ex.what() : "session video: exception";
        ui_log_session_video(std::string("exception: ") + err_utf8);
        { std::lock_guard<std::mutex> l(g_mux); g_sess.reset(); }
        mf_release();
        return false;
    } catch (...) {
        err_utf8   = "session video: unknown exception";
        ui_log_session_video(err_utf8);
        { std::lock_guard<std::mutex> l(g_mux); g_sess.reset(); }
        mf_release();
        return false;
    }
    ui_log_session_video("session video start complete (WGC + MF; stop with app command or hotkey)");
    return true;
}

void stop_impl() noexcept
{
    ui_log_session_video("stop: closing WGC + finalizing MP4, MF release");
    std::shared_ptr<RecSession> s;
    { std::lock_guard<std::mutex> l(g_mux); s = std::move(g_sess); }
    if (s) s->stop_and_finalize();
    mf_release();
    ui_log_session_video("stop: done (MF ref released)");
}

bool is_recording_impl() noexcept
{
    std::lock_guard<std::mutex> l(g_mux);
    return (bool)g_sess && g_sess->active();
}

void toggle_pause_impl() noexcept
{
    std::shared_ptr<RecSession> s;
    { std::lock_guard<std::mutex> l(g_mux); s = g_sess; }
    if (!s || !s->active()) {
        ui_log_session_video("pause toggle ignored: not recording (Ctrl+Alt+R to start)");
        return;
    }
    s->toggle_pause();
}

bool is_paused_impl() noexcept
{
    std::lock_guard<std::mutex> l(g_mux);
    return (bool)g_sess && g_sess->active() && g_sess->paused();
}
} // namespace detail

bool start(HWND h, const std::filesystem::path& p, std::string& e) { return detail::start_impl(h, p, e); }
void   stop() noexcept { detail::stop_impl(); }
bool   is_recording() noexcept { return detail::is_recording_impl(); }
void   toggle_pause() noexcept { detail::toggle_pause_impl(); }
bool   is_paused() noexcept { return detail::is_paused_impl(); }
} // namespace media::win::recorder

#endif
