#ifndef PM_UI_FILEVIEWER_IMAGE_TOOL_INTERFACE_H
#define PM_UI_FILEVIEWER_IMAGE_TOOL_INTERFACE_H

//
// Pluggable in-image tools for `CFileViewer` (crop, future filters, …). The viewer
// owns `std::unique_ptr<IFileViewerImageTool> m_imageActiveTool` and forwards
// input / paint / save hooks while a tool session is armed.
//
#include "stdafx.h"
#include <memory>

namespace Gdiplus {
class Bitmap;
}

class CFileViewer;

enum class PmImageToolKind : int {
    None = 0,
    Crop = 1,
};

/// Per-tool UI + behaviour; `CFileViewer` holds at most one active implementation.
class IFileViewerImageTool {
public:
    virtual ~IFileViewerImageTool() = default;

    virtual PmImageToolKind Kind() const noexcept        = 0;
    virtual bool            IsActive() const noexcept    = 0;
    virtual void            SetActive(CFileViewer& host, bool on) = 0;

    virtual void OnHostImageSized(CFileViewer& host, int iw, int ih)  = 0;
    virtual void OnHostImageCleared(CFileViewer& host)                = 0;

    virtual void SetViewTransform(int clientW, int clientH, double zoom, double viewCx, double viewCy) = 0;

    virtual bool OnMouseDown(CFileViewer& host, HWND hwnd, POINT clientPt) = 0;
    virtual bool OnMouseMove(CFileViewer& host, HWND hwnd, POINT clientPt) = 0;
    virtual bool OnMouseUp(CFileViewer& host, HWND hwnd, POINT clientPt)   = 0;
    virtual void OnCaptureLost(CFileViewer& host, HWND hwnd)              = 0;

    virtual void PaintOverlay(const CFileViewer& host, HDC hdcMem, HWND dpiHwnd) = 0;
    /// @p hoveredBtnEnum matches `CFileViewer::kImageToolChromeBtnConfirm` for the in-image OK control.
    virtual void PaintChrome(const CFileViewer& host, HDC hdcMem, HWND dpiHwnd, int clientW, int clientH,
        int barH, int hoveredBtnEnum) = 0;

    virtual bool     HitChrome(const CFileViewer& host, POINT clientPt) const     = 0;
    virtual LPCTSTR  SuggestSetCursor(const CFileViewer& host, POINT clientPt) const = 0;
    virtual bool     DraggingMouse() const noexcept = 0;

    virtual bool WantsVkReturnWhileActive() const noexcept = 0;

    /// Returns a new bitmap to adopt as the preview, or `nullptr` (overlay only / no pixel change).
    virtual Gdiplus::Bitmap* TryCommitInMemoryEdit(CFileViewer& host) = 0;

    /// After the host adopts a new `m_pImage` from `TryCommitInMemoryEdit`, resync tool state.
    virtual void OnHostReplacedPreviewImage(CFileViewer& host, int newW, int newH) = 0;

    virtual bool HostAllowsOverwriteSave(const CFileViewer& host) const = 0;
    virtual bool SaveOverwrite(CFileViewer& host, CString& errOut)    = 0;
    virtual bool SaveAs(CFileViewer& host, HWND owner, CString& errOut) = 0;

    virtual void OnDpiOrThemeChromeChange(CFileViewer& host) = 0;

    /// Before `SetActive(host, true)` from UI: reset aspect/crop rect for a new session.
    virtual void ArmNewInteractiveSession(CFileViewer& host) = 0;
};

[[nodiscard]] std::unique_ptr<IFileViewerImageTool> PmCreateFileViewerImageTool(PmImageToolKind kind);

#endif // PM_UI_FILEVIEWER_IMAGE_TOOL_INTERFACE_H
