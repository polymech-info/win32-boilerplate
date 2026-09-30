#pragma once

#include "win/web/CWebViewManager.h"

#include <functional>

namespace pmui {

class CSettingsWebView {
public:
    static void Show(CWebViewManager& webViews, HWND hostHwnd,
                     std::function<void()> onAppearanceChanged = {});
};

} // namespace pmui

