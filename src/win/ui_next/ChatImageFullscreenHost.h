#ifndef PM_UI_CHAT_IMAGE_FULLSCREEN_HOST_H
#define PM_UI_CHAT_IMAGE_FULLSCREEN_HOST_H

#include "stdafx.h"

#include <string>
#include <vector>

namespace pmui {

/// Popup hosting @c CFileViewer (see FileViewer): wheel + pinch zoom, drag pan,
/// Fit / Full overlays. Esc closes; Left / Right cycle @p paths.
/// @p owner owns Z-order (typically @c CChatWebView). If @p constrainToMainFrame,
/// the popup matches @c ::GetAncestor(owner, GA_ROOT) window rect; otherwise it
/// fills the nearest monitor (see @c MONITORINFO::rcMonitor).
void chat_image_fullscreen_show(HWND owner, std::vector<std::wstring> paths, size_t index,
                                  bool constrainToMainFrame = false);

} // namespace pmui

#endif
