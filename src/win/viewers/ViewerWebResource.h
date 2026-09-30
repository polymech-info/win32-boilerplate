#ifndef PM_UI_VIEWERWEBRESOURCE_H
#define PM_UI_VIEWERWEBRESOURCE_H

#include <string>

namespace pmui {

/// True when FEATURE_VIEWER_WEB=ON and the apps/viewer-next bundle was built.
bool viewer_web_available();

/// UTF-8 viewer.html from dist/shared, with legacy beside-exe fallback.
std::string load_viewer_web_html();

/// Folder containing viewer.html and any sibling lazy assets, when available.
std::wstring viewer_web_bundle_folder();

} // namespace pmui

#endif
