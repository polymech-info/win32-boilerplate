#ifndef PM_UI_FILEVIEWER_IMAGE_CROP_TOOL_H
#define PM_UI_FILEVIEWER_IMAGE_CROP_TOOL_H

//
// Windows Photos–style crop overlay for `CFileViewer` image mode. Coordinate model and
// behaviour follow `docs/img-cop.md`; transforms match `FileViewer.cpp` image paint
// (`zoom`, `m_viewCx` / `m_viewCy`, client-centred placement).
//
#include "stdafx.h"
#include <gdiplus.h>

struct PmVec2 {
    double x = 0;
    double y = 0;
};

struct PmRectD {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

enum class PmCropHit : int {
    None = 0,
    Move,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

enum class PmAspectPreset : int {
    Free = 0,
    Original,
    Square,
    Ratio_4_3,
    Ratio_3_2,
    Ratio_16_9,
    Ratio_9_16,
};

struct PmAspectRatio {
    bool   locked = false;
    double w      = 1;
    double h      = 1;
};

struct PmCropDragState {
    bool      active = false;
    PmCropHit hit    = PmCropHit::None;
    double    startMouseImgX = 0;
    double    startMouseImgY = 0;
    PmRectD   startCrop{};
};

/// Non-owning overlay: caller paints image first, then `Paint` on the same mem DC.
class CFileViewerImageCropTool {
public:
    void SetImageSize(int iw, int ih);
    void SetAspectPreset(PmAspectPreset p);
    PmAspectPreset AspectPreset() const noexcept { return m_preset; }

    /// Same parameters used when painting the image in `CFileViewer::OnDraw`.
    void SetViewTransform(int clientW, int clientH, double zoom, double viewCx, double viewCy);

    void SetActive(bool on) noexcept;
    bool Active() const noexcept { return m_active; }
    bool Dragging() const noexcept { return m_drag.active; }

    bool OnMouseDown(HWND hwnd, POINT clientPt);
    bool OnMouseMove(HWND hwnd, POINT clientPt);
    bool OnMouseUp(HWND hwnd, POINT clientPt);
    /// Clears an in-progress drag if capture leaves this window without a matching button-up.
    void OnCaptureLost(HWND hwnd);

    void ResetCropToFullImage();
    void Cancel(HWND hwnd);

    void Paint(HDC hdc, HWND dpiHwnd) const;

    [[nodiscard]] PmCropHit HitTestView(POINT clientPt) const;
    /// Win32 `IDC_*` for `LoadCursor`, or `nullptr` for default arrow.
    [[nodiscard]] LPCTSTR SuggestSetCursor(POINT clientPt) const;

    [[nodiscard]] PmRectD CropImageRectD() const { return m_cropImg; }

    /// Integer extract rect for a future Sharp / WIC pipeline (`docs/img-cop.md`).
    void GetCropExtractInts(int imageW, int imageH, int* outL, int* outT, int* outW, int* outH) const;

private:
    void   ImageToView(double ix, double iy, double* vx, double* vy) const;
    void   ViewToImage(double vx, double vy, double* ix, double* iy) const;
    PmRectD CropViewRectD() const;
    void   NormalizeCrop(PmRectD& r) const;
    void   ClampCropToImage(PmRectD& r) const;
    void   ApplySnap(PmRectD& r, PmCropHit activeHit);
    void   UpdateDrag(double mouseImgX, double mouseImgY);

    bool     m_active = false;
    int      m_iw = 0, m_ih = 0;
    int      m_cw = 0, m_ch = 0;
    double   m_zoom = 1;
    double   m_vcx = 0, m_vcy = 0;
    PmRectD  m_cropImg{};
    PmCropDragState m_drag{};
    PmAspectPreset  m_preset = PmAspectPreset::Free;
    PmAspectRatio   m_ratio{};
};

#endif // PM_UI_FILEVIEWER_IMAGE_CROP_TOOL_H
