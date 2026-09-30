#pragma once
// Shared JSON / string contracts between the Win32 host (`CViewerWebPanel`) and
// apps/viewer-next (`window.pmViewer`). Add new viewer kinds here + matching TS
// union + `src/viewers/<kind>/` in the web app.
//
// `setStatus.features.markdownText` — raw UTF-8 markdown for client-side preview (react-markdown).
// `setStatus.features.markdownBaseUrl` — optional base URL for relative links/images
//   (`https://<k_markdown_assets_vhost_w>/` + folder mapping). Large/binary loads use `vw_hosted_read` from the host, not cross-origin fetch.
// Virtual host names are chosen for fast NXDOMAIN (see Branding.cmake), not pretty *.local DNS.
// `setStatus.features.markdownEmbedUrl` — optional legacy iframe URL (host-generated HTML).
//
// 3D (`viewerKind` = `k_viewer_kind_three`):
//   `threeModelUrl` — HTTPS URL on `k_markdown_assets_vhost_w` (basename); host reads bytes via `vw_hosted_read`.
//   `threeFileName` — UTF-8 basename for UI + format detection.
//   `threeFileSizeBytes` / `threeMaxBytes` — optional; used when `threeError` is set (too large).
//   `threeError` — non-empty => show message instead of loading `threeModelUrl`.
//
// PDF / spreadsheet / text / video / image (`viewerKind` = `k_viewer_kind_pdf` | `k_viewer_kind_spreadsheet` |
// `k_viewer_kind_text` | `k_viewer_kind_video` | `k_viewer_kind_image`):
//   `hostedFileUrl` — HTTPS URL on `k_markdown_assets_vhost_w` (basename); prefer `vw_hosted_read` over `fetch` (no CORS on vhost mapping).
//   `hostedFileText` — optional UTF-8 body for `k_viewer_kind_text` only; when present, the web UI must not
//   `fetch(hostedFileUrl)`.
//   `hostedFileName` — UTF-8 basename for UI.
//   `hostedFileSizeBytes` / `hostedFileMaxBytes` — optional; used with `hostedFileError` (too large).
//   `hostedFileError` — non-empty => show message instead of loading `hostedFileUrl`.
//
// Relative assets: `markdownBaseUrl` + virtual host mapping to the .md parent folder
// avoids `file://` inside the HTTPS viewer document (blocked by WebView2).
//
// Layout (extend here when adding e.g. glTF / Three.js):
//   src/win/viewers/          — host contracts + resources
//   src/win/viewers/text/     — WebView2 panel for text/markdown (this ship)
//   (future) src/win/viewers/three/ — optional native shim or second web bundle
//   apps/viewer-next/src/viewers/<kind>/ — web UI modules
//
#include <string_view>

namespace pmui::viewer_web {

/// `setStatus` JSON field `viewerKind` — markdown preview / notes (current).
inline constexpr std::string_view k_viewer_kind_markdown = "markdown";
/// WebGL / Three.js mesh preview (STL, OBJ, glTF, PLY, STEP/STP, DXF) in apps/viewer-next.
inline constexpr std::string_view k_viewer_kind_three = "three";
/// OpenSCAD source preview compiled host-side to STL, then rendered in the same Three.js pane.
inline constexpr std::string_view k_viewer_kind_openscad = "openscad";
/// react-pdf (`features.hostedFileUrl` on markdown-assets vhost).
inline constexpr std::string_view k_viewer_kind_pdf = "pdf";
/// xlsx sheet → HTML (`features.hostedFileUrl` + fetch + xlsx in viewer-next).
inline constexpr std::string_view k_viewer_kind_spreadsheet = "spreadsheet";
/// Plain text / source (`features.hostedFileUrl` + fetch + Prism or Markdown in viewer-next).
inline constexpr std::string_view k_viewer_kind_text = "text";
/// Local video file (`features.hostedFileUrl` on markdown-assets vhost, same mapping as PDF).
inline constexpr std::string_view k_viewer_kind_video = "video";
/// Browser-rendered SVG / raster image (`features.hostedFileUrl` on markdown-assets vhost).
inline constexpr std::string_view k_viewer_kind_image = "image";
/// Local HTML file rendered in an `<iframe>` on the markdown-assets vhost.
/// The folder mapping gives the iframe a base URL equal to the HTML file's parent directory,
/// so relative links (CSS, images, scripts in the same folder) resolve correctly.
inline constexpr std::string_view k_viewer_kind_html = "html";

/// Agent session flow visualization for .agent.json files (@xyflow/react).
/// `features.hostedFileText` contains the JSON content; `hostedFileName` is the basename.
inline constexpr std::string_view k_viewer_kind_agent_flow = "agent-flow";

/// Monaco-based code / text editor (user-initiated; same file context as `k_viewer_kind_text`).
/// Sends `vw_editor_mode {active}` messages while active to suppress centre-preview nav keys.
/// Supports `vw_save_file {content}` → `vw_save_file_result {ok}` for Ctrl+S / Save button.
inline constexpr std::string_view k_viewer_kind_editor = "editor";

} // namespace pmui::viewer_web
