// FileViewer_Images.cpp — RAW-by-extension pipeline, libvips decode helpers, GDI+ image load / zoom /
// filename bar, and RAW loading spinner paint (`LoadPicture`, `DrawSpinner`, …).
#include "stdafx.h"
#include "FileViewer.h"
#include "FileViewerImageToolInterface.h"
#include "Resource.h"
#include "file_extensions.hpp"
#include "helpers/text_conv.hpp"
#include "llm/llm_fs_guard.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <objidl.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <wincodec.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Windowscodecs.lib")

#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
#include <vips/vips.h>
#endif

#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)

namespace {

constexpr int kRawPreviewTargetPx = 1280;

template <class T>
void SafeRelease(T*& p) noexcept
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct ScopedComInit {
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ScopedComInit()
    {
        if (SUCCEEDED(hr))
            ::CoUninitialize();
    }
    bool ok() const noexcept { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

bool EncodeWicBitmapSourceToJpeg(IWICImagingFactory* factory,
                                 IWICBitmapSource*   source,
                                 int                 targetPx,
                                 std::vector<unsigned char>& out_jpeg)
{
    out_jpeg.clear();
    if (!factory || !source)
        return false;

    UINT iw = 0, ih = 0;
    if (FAILED(source->GetSize(&iw, &ih)) || iw == 0 || ih == 0)
        return false;

    UINT ow = iw;
    UINT oh = ih;
    if (targetPx > 0) {
        const UINT maxEdge = (std::max)(iw, ih);
        if (maxEdge > static_cast<UINT>(targetPx)) {
            const double scale = static_cast<double>(targetPx) / static_cast<double>(maxEdge);
            ow = (std::max)(1u, static_cast<UINT>(std::lround(static_cast<double>(iw) * scale)));
            oh = (std::max)(1u, static_cast<UINT>(std::lround(static_cast<double>(ih) * scale)));
        }
    }

    IWICBitmapSource* bitmapForEncode = source;
    IWICBitmapScaler* scaler = nullptr;
    if (ow != iw || oh != ih) {
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || !scaler)
            return false;
        if (FAILED(scaler->Initialize(source, ow, oh, WICBitmapInterpolationModeFant))) {
            SafeRelease(scaler);
            return false;
        }
        bitmapForEncode = scaler;
    }

    IWICFormatConverter* converter = nullptr;
    HRESULT hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr) && converter) {
        hr = converter->Initialize(bitmapForEncode,
                                   GUID_WICPixelFormat24bppBGR,
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0,
                                   WICBitmapPaletteTypeCustom);
    }
    if (FAILED(hr) || !converter) {
        SafeRelease(converter);
        SafeRelease(scaler);
        return false;
    }

    IStream* stream = nullptr;
    hr = ::CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr) || !stream) {
        SafeRelease(converter);
        SafeRelease(scaler);
        return false;
    }

    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (SUCCEEDED(hr) && encoder)
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && props) {
        PROPBAG2 opt{};
        opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT var{};
        ::VariantInit(&var);
        var.vt = VT_R4;
        var.fltVal = 0.88f;
        (void)props->Write(1, &opt, &var);
        ::VariantClear(&var);
    }
    if (SUCCEEDED(hr) && frame)
        hr = frame->Initialize(props);
    if (SUCCEEDED(hr))
        hr = frame->SetSize(ow, oh);
    if (SUCCEEDED(hr)) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
        hr = frame->SetPixelFormat(&fmt);
    }
    if (SUCCEEDED(hr))
        hr = frame->WriteSource(converter, nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();

    if (SUCCEEDED(hr)) {
        HGLOBAL h = nullptr;
        if (SUCCEEDED(::GetHGlobalFromStream(stream, &h)) && h) {
            const SIZE_T len = ::GlobalSize(h);
            const void* p = ::GlobalLock(h);
            if (p && len > 0)
                out_jpeg.assign(static_cast<const unsigned char*>(p),
                                static_cast<const unsigned char*>(p) + len);
            if (p)
                ::GlobalUnlock(h);
        }
    }

    SafeRelease(props);
    SafeRelease(frame);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(converter);
    SafeRelease(scaler);
    return !out_jpeg.empty();
}

bool DecodeWicEmbeddedRawPreviewToJpeg(LPCWSTR path,
                                       int     targetPx,
                                       std::vector<unsigned char>& out_jpeg)
{
    out_jpeg.clear();
    if (!path || !path[0])
        return false;

    ScopedComInit com;
    if (!com.ok())
        return false;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory,
                                    nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_IWICImagingFactory,
                                    reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory)
        return false;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(path,
                                            nullptr,
                                            GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand,
                                            &decoder);
    if (FAILED(hr) || !decoder) {
        SafeRelease(factory);
        return false;
    }

    IWICBitmapSource* source = nullptr;
    hr = decoder->GetPreview(&source);
    if (FAILED(hr) || !source)
        hr = decoder->GetThumbnail(&source);
    if (FAILED(hr) || !source) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
            hr = frame->GetThumbnail(&source);
            SafeRelease(frame);
        }
    }

    const bool ok = source && EncodeWicBitmapSourceToJpeg(factory, source, targetPx, out_jpeg);
    SafeRelease(source);
    SafeRelease(decoder);
    SafeRelease(factory);
    return ok;
}

} // namespace

bool CFileViewer::TryLoadPictureRawPipeline(LPCWSTR path)
{
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    for (auto& c : ext) c = towlower(c);
    if (!pmui::is_raw_ext(ext))
        return false;

    const std::wstring pathW = path ? std::wstring(path) : std::wstring();
    const std::string utf8 = pmui::wide_to_utf8(path);

    m_spinnerActive = true;
    m_spinnerAngle  = 0;
    ::SetTimer(GetHwnd(), TIMER_SPINNER, 40, nullptr);

#ifdef FEATURE_RAW_PREVIEW
    {
        int  gen  = ++m_rawFastGen;
        HWND hwnd = GetHwnd();
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        m_rawFastCancel = cancel;
        m_rawFastThread = std::thread([pathW, utf8, gen, hwnd, cancel]() {
            if (cancel->load()) { vips_thread_shutdown(); return; }
            auto* bytes = new std::vector<unsigned char>;
            if (!DecodeWicEmbeddedRawPreviewToJpeg(pathW.c_str(), kRawPreviewTargetPx, *bytes))
                DecodeVipsThumbnailToJpeg(utf8, kRawPreviewTargetPx, *bytes);
            if (cancel->load()) { delete bytes; vips_thread_shutdown(); return; }
            if (!::PostMessage(hwnd, UWM_RAW_PREVIEW_READY,
                               reinterpret_cast<WPARAM>(bytes),
                               static_cast<LPARAM>(gen)))
                delete bytes;
            vips_thread_shutdown();
        });
    }
#endif

#ifdef FEATURE_RAW_VIEW
    {
        int  gen  = ++m_rawGeneration;
        HWND hwnd = GetHwnd();
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        m_rawCancel = cancel;
        m_rawThread = std::thread([utf8, gen, hwnd, cancel]() {
            if (cancel->load()) { vips_thread_shutdown(); return; }
            auto* bytes = new std::vector<unsigned char>;
            DecodeVipsFullToJpeg(utf8, *bytes);
            if (cancel->load()) { delete bytes; vips_thread_shutdown(); return; }
            if (!::PostMessage(hwnd, UWM_RAW_DECODED,
                               reinterpret_cast<WPARAM>(bytes),
                               static_cast<LPARAM>(gen)))
                delete bytes;
            vips_thread_shutdown();
        });
    }
#endif
    SetScrollSizes(CSize(0, 0));
    SetFileInfoFromPath(path);
    Invalidate(FALSE);
    return true;
}

bool CFileViewer::DecodeVipsThumbnailToJpeg(const std::string& utf8Path,
                                              int                targetPx,
                                              std::vector<unsigned char>& out_jpeg)
{
    out_jpeg.clear();
    VipsImage* thumb = nullptr;
    if (vips_thumbnail(utf8Path.c_str(), &thumb, targetPx,
            "size", VIPS_SIZE_DOWN, nullptr) != 0 || !thumb)
        return false;
    void* buf = nullptr; std::size_t len = 0;
    const bool ok = (vips_jpegsave_buffer(thumb, &buf, &len, "Q", 92, nullptr) == 0);
    g_object_unref(thumb);
    if (!ok || !buf || len == 0) {
        if (buf) g_free(buf);
        return false;
    }
    out_jpeg.assign(reinterpret_cast<const unsigned char*>(buf),
                    reinterpret_cast<const unsigned char*>(buf) + len);
    g_free(buf);
    return true;
}

bool CFileViewer::DecodeVipsFullToJpeg(const std::string&         utf8Path,
                                         std::vector<unsigned char>& out_jpeg)
{
    out_jpeg.clear();
    VipsImage* image = vips_image_new_from_file(utf8Path.c_str(), nullptr);
    if (!image) return false;
    void* buf = nullptr; std::size_t len = 0;
    const bool ok = (vips_jpegsave_buffer(image, &buf, &len, "Q", 95, nullptr) == 0);
    g_object_unref(image);
    if (!ok || !buf || len == 0) {
        if (buf) g_free(buf);
        return false;
    }
    out_jpeg.assign(reinterpret_cast<const unsigned char*>(buf),
                    reinterpret_cast<const unsigned char*>(buf) + len);
    g_free(buf);
    return true;
}

void CFileViewer::CancelRawThreads(bool wait_for_join)
{
    if (m_rawFastCancel) m_rawFastCancel->store(true);
#ifdef FEATURE_RAW_VIEW
    if (m_rawCancel)     m_rawCancel->store(true);
#endif

    ++m_rawFastGen;
#ifdef FEATURE_RAW_VIEW
    ++m_rawGeneration;
#endif

    if (m_spinnerActive) {
        m_spinnerActive = false;
        if (IsWindow()) ::KillTimer(GetHwnd(), TIMER_SPINNER);
    }

    if (m_rawFastThread.joinable()) {
        if (wait_for_join) m_rawFastThread.join();
        else               m_rawFastThread.detach();
    }
#ifdef FEATURE_RAW_VIEW
    if (m_rawThread.joinable()) {
        if (wait_for_join) m_rawThread.join();
        else               m_rawThread.detach();
    }
#endif

    m_rawFastCancel.reset();
#ifdef FEATURE_RAW_VIEW
    m_rawCancel.reset();
#endif
}

#endif // FEATURE_RAW_PREVIEW || FEATURE_RAW_VIEW

namespace {

std::wstring FormatHumanFileSize(uintmax_t bytes)
{
    wchar_t b[40]{};
    if (bytes >= 1024ull * 1024ull * 1024ull)
        swprintf_s(b, L"%.2f GB", static_cast<double>(bytes) / static_cast<double>(1024ull * 1024ull * 1024ull));
    else if (bytes >= 1024ull * 1024ull)
        swprintf_s(b, L"%.1f MB", static_cast<double>(bytes) / static_cast<double>(1024ull * 1024ull));
    else if (bytes >= 1024ull)
        swprintf_s(b, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        swprintf_s(b, L"%llu B", static_cast<unsigned long long>(bytes));
    return b;
}

} // namespace

void CFileViewer::DropImageAndStream()
{
    if (m_imageActiveTool)
        m_imageActiveTool->OnHostImageCleared(*this);
    delete m_pImage;
    m_pImage = nullptr;
    if (m_imgStream) {
        m_imgStream->Release();
        m_imgStream = nullptr;
    }
}

void CFileViewer::SyncImageToolFromHostPicture()
{
    if (!m_imageActiveTool)
        return;
    if (!m_pImage) {
        m_imageActiveTool->OnHostImageSized(*this, 0, 0);
        return;
    }
    const UINT iw = m_pImage->GetWidth(), ih = m_pImage->GetHeight();
    if (iw > 0 && ih > 0)
        m_imageActiveTool->OnHostImageSized(*this, static_cast<int>(iw), static_cast<int>(ih));
    else
        m_imageActiveTool->OnHostImageSized(*this, 0, 0);
}

double CFileViewer::GetFitZoom() const
{
    if (!m_pImage) return 1.0;
    const UINT iw = m_pImage->GetWidth(), ih = m_pImage->GetHeight();
    if (!iw || !ih) return 1.0;
    const CRect rc = GetImageAreaRect();
    if (rc.Width() <= 0 || rc.Height() <= 0) return 1.0;
    return (std::min)((double)rc.Width() / iw, (double)rc.Height() / ih);
}

void CFileViewer::EnsureExplicitZoom()
{
    if (m_zoom > 0 || !m_pImage) return;
    m_zoom   = GetFitZoom();
    m_viewCx = m_pImage->GetWidth() / 2.0;
    m_viewCy = m_pImage->GetHeight() / 2.0;
    m_fitZoomAtLayout = m_zoom;
}

void CFileViewer::BeginImageInteractionPaint()
{
    m_fastImageInteractionPaint = true;
    if (IsWindow())
        ::SetTimer(GetHwnd(), TIMER_IMAGE_INTERACTION_SETTLE, 80, nullptr);
}

void CFileViewer::EndImageInteractionPaint(bool invalidate)
{
    if (IsWindow())
        ::KillTimer(GetHwnd(), TIMER_IMAGE_INTERACTION_SETTLE);
    if (!m_fastImageInteractionPaint)
        return;
    m_fastImageInteractionPaint = false;
    if (invalidate && IsWindow())
        Invalidate(FALSE);
}

void CFileViewer::ResetView()
{
    m_zoom   = 0.0;
    m_fitZoomAtLayout = 0;
    if (m_pImage) {
        m_viewCx = m_pImage->GetWidth() / 2.0;
        m_viewCy = m_pImage->GetHeight() / 2.0;
    }
    m_dragging = false;
    EndImageInteractionPaint(false);
    if (m_imageActiveTool)
        m_imageActiveTool->SetActive(*this, false);
    Invalidate(FALSE);
}

void CFileViewer::ApplyZoom(double factor, int sx, int sy)
{
    if (!m_pImage) return;
    EnsureExplicitZoom();
    const UINT iw = m_pImage->GetWidth(), ih = m_pImage->GetHeight();
    if (!iw || !ih) return;
    const CRect rc = GetImageAreaRect();
    const double cW = rc.Width(), cH = rc.Height();
    const double imgX = m_viewCx + (sx - cW * 0.5) / m_zoom;
    const double imgY = m_viewCy + (sy - cH * 0.5) / m_zoom;
    const double fitZ = GetFitZoom();
    const double defaultZ = fitZ * 0.75;
    const double newZ = (std::max)(defaultZ, (std::min)(64.0, m_zoom * factor));
    m_viewCx = imgX - (sx - cW * 0.5) / newZ;
    m_viewCy = imgY - (sy - cH * 0.5) / newZ;
    if (factor < 1.0) {
        // When zooming out, gently return toward the image centre. This avoids
        // ending up with a tiny image stranded at an old pan position.
        const double cx = iw * 0.5;
        const double cy = ih * 0.5;
        const double nearFit = (fitZ > 1e-12)
            ? (std::clamp)((fitZ * 1.35 - newZ) / (fitZ * 1.20), 0.0, 1.0)
            : 0.0;
        const double pull = 0.16 + 0.56 * nearFit;
        m_viewCx += (cx - m_viewCx) * pull;
        m_viewCy += (cy - m_viewCy) * pull;
    }
    m_zoom   = newZ;
    m_fitZoomAtLayout = fitZ;
    BeginImageInteractionPaint();
    Invalidate(FALSE);
}

void CFileViewer::ApplyDefaultImageZoom()
{
    if (!m_pImage)
        return;
    const UINT iw = m_pImage->GetWidth(), ih = m_pImage->GetHeight();
    if (!iw || !ih)
        return;
    const double fit = GetFitZoom();
    m_zoom   = fit * 0.75;
    m_viewCx = iw * 0.5;
    m_viewCy = ih * 0.5;
    m_fitZoomAtLayout = fit;
}

void CFileViewer::ApplyNativeZoomCentered()
{
    if (!m_pImage) return;
    m_zoom   = 1.0;
    m_viewCx = m_pImage->GetWidth() / 2.0;
    m_viewCy = m_pImage->GetHeight() / 2.0;
    m_fitZoomAtLayout = GetFitZoom();
    Invalidate(FALSE);
}

bool CFileViewer::LoadPicture(LPCWSTR path)
{
    ClearText();
    ClearMarkdown();
#if defined(FEATURE_VIEWER_WEB)
    ReleaseViewerWebFolderMapping();
#endif
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label.Empty();
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    m_rawStatusLabel.Empty();
#endif
    ResetView();
    m_previewPath = path ? std::wstring(path) : std::wstring();

    if (path) {
        if (const std::string deny = media::llm::llm_fs_guard_deny_reason(std::filesystem::path(path));
            !deny.empty()) {
            m_label = pmui::utf8_to_wide(deny).c_str();
            ClearFileInfo();
            SetScrollSizes(CSize(0, 0));
            Invalidate(FALSE);
            return false;
        }
    }

#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    if (TryLoadPictureRawPipeline(path))
        return true;
#endif

    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) {
            m_label = L"Could not open image";
            ClearFileInfo();
            SetScrollSizes(CSize(0, 0));
            Invalidate(FALSE);
            return false;
        }
        const std::streamsize size = f.tellg();
        f.seekg(0, std::ios::beg);
        if (size <= 0) {
            m_label = L"Empty / unreadable image";
            ClearFileInfo();
            SetScrollSizes(CSize(0, 0));
            Invalidate(FALSE);
            return false;
        }

        std::vector<unsigned char> buf(static_cast<size_t>(size));
        if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
            m_label = L"Could not read image bytes";
            ClearFileInfo();
            SetScrollSizes(CSize(0, 0));
            Invalidate(FALSE);
            return false;
        }
        f.close();

        IStream* stream =
            ::SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        if (!stream) {
            m_label = L"SHCreateMemStream failed";
            ClearFileInfo();
            SetScrollSizes(CSize(0, 0));
            Invalidate(FALSE);
            return false;
        }

        auto* img = Gdiplus::Image::FromStream(stream);
        if (img && img->GetLastStatus() == static_cast<Gdiplus::Status>(0)) {
            m_pImage    = img;
            m_imgStream = stream;
            SetFileInfoFromPath(path);
            SetScrollSizes(CSize(0, 0));
            ApplyDefaultImageZoom();
            SyncImageToolFromHostPicture();
            m_imagePill.SetVisible(ImageNavHasNeighbors());
            Invalidate(FALSE);
            return true;
        }
        delete img;
        stream->Release();
    }

    {
        std::wstring ext = std::filesystem::path(path).extension().wstring();
        for (auto& c : ext)
            c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
        if (pmui::is_browser_image_ext(ext) && LoadBrowserImage(path))
            return true;
    }

    m_label = L"Could not load image";
    ClearFileInfo();
    SetScrollSizes(CSize(0, 0));
    Invalidate(FALSE);
    return false;
}

void CFileViewer::ClearPicture()
{
    ClearImageNav();
    m_imagePill.SetVisible(false);
    m_imagePill.ReleaseResources();
    m_fitZoomAtLayout = 0;
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    CancelRawThreads();
#endif
    DropImageAndStream();
    m_label = L"Drop images here or use Add Files";
#if defined(FEATURE_RAW_PREVIEW) || defined(FEATURE_RAW_VIEW)
    m_rawStatusLabel.Empty();
#endif
    m_previewPath.clear();
    ClearFileInfo();
    ResetView();
    m_imageActiveTool.reset();
}

void CFileViewer::SetFileInfoFromPath(LPCWSTR path)
{
    if (!path || !path[0]) {
        ClearFileInfo();
        return;
    }
    const std::filesystem::path fp(path);
    const std::wstring        fn = fp.filename().wstring();
    std::wstring              line = fn;
    try {
        if (std::filesystem::is_regular_file(fp)) {
            const uintmax_t sz = std::filesystem::file_size(fp);
            line += L"  ·  ";
            line += FormatHumanFileSize(sz);
        }
    } catch (...) {
    }
    m_fileInfoText = line.c_str();
    Invalidate(FALSE);
}

void CFileViewer::SetFileInfoText(LPCWSTR text)
{
    m_fileInfoText = (text && text[0]) ? text : L"";
    Invalidate(FALSE);
}

void CFileViewer::ClearFileInfo()
{
    m_fileInfoText.Empty();
    Invalidate(FALSE);
}

namespace {
constexpr double kSpinnerPi = 3.14159265358979323846;
} // namespace

void CFileViewer::DrawSpinner(HDC hdc, int cx, int cy, int radius) const
{
    Gdiplus::Graphics gfx(hdc);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    constexpr int N = 8;
    const float   r1 = radius * 0.5f;
    const float   r2 = (float)radius;
    const double  base = m_spinnerAngle * kSpinnerPi / 180.0;

    for (int i = 0; i < N; ++i) {
        BYTE   alpha = (BYTE)(28 + 220 * i / (N - 1));
        double angle = base + i * (2.0 * kSpinnerPi / N);
        float  x1    = cx + r1 * (float)std::cos(angle);
        float  y1    = cy + r1 * (float)std::sin(angle);
        float  x2    = cx + r2 * (float)std::cos(angle);
        float  y2    = cy + r2 * (float)std::sin(angle);
        Gdiplus::Pen pen(Gdiplus::Color(alpha, 180, 180, 180), 2.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        gfx.DrawLine(&pen, x1, y1, x2, y2);
    }
}
