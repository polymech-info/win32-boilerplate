# UserPage Viewer Port TODOs

This folder currently contains a view-only PixlWiz page renderer for `viewer-next`.
It can load page JSON from `/viewer/page?src=...`, render layouts, resolve widget IDs
through a local registry, and reuse the advanced markdown renderer.

The implementation is not yet a full faithful port of `pm-pics`. Several widgets are
compatibility implementations or placeholders so page JSON does not fail at runtime.

## Route And Loading

- Keep `/viewer/page?src=<encoded-url>` as the route-style entry point.
- Keep dev proxy support for known PolyMech hosts:
  - `https://pixlwiz.com/...`
  - `https://service.polymech.info/...`
- Add local file / hosted-file integration when the C++ side maps `*.json` to the Page viewer.
- Add clearer errors for unsupported remote hosts and invalid page payloads.

## Widget Parity

Replace the current compatibility widgets in `widgets.tsx` with faithful view-mode ports
from `pm-pics`, keeping editor-only branches deferred.

### `tabs-widget`

- Port the real `TabsWidget` view behavior.
- Load nested tab layouts by `layoutId` when `layoutData` is not embedded.
- Preserve tab bar position, orientation, custom class names, and nested context variables.
- Make nested layouts participate in heading extraction / TOC where appropriate.

### `layout-container-widget`

- Port real nested layout loading.
- Support `nestedPageId`, `nestedPageName`, and view-mode nested canvas behavior.
- Decide how nested layouts are fetched in `viewer-next` without the full `LayoutContext` API.

### Feed Widgets

- Port `home` and `category-feed` closer to `pm-pics`.
- Replace the simplified feed cards with the real card/list/large gallery views or equivalent shared components.
- Add category tree/sidebar support.
- Add sort, layout toggles, content-type filters, search mode, and pagination/load-more.
- Mirror `showTitle`, `showDescription`, `showAuthor`, `showSocial`, `columns`, `center`, and `fillHeight`.

### Media Widgets

- Port `photo-card` view from `PhotoCardWidgetView`.
- Port `photo-grid` and `photo-grid-widget` behavior.
- Port `gallery-widget` view including thumbnail placement/orientation, version handling, video handling, and zoom where required.
- Port `video-banner` view with real image/video data resolution.
- Replace fallback image URL construction with real picture/post client resolution.

### Content And Navigation Widgets

- Port `html-widget` variable substitution behavior from `pm-pics`.
- Port `menu-widget` view with page/category/custom link resolution and all style variants.
- Confirm `markdown-text` matches `pm-pics` output while still reusing `viewer-next`'s improved markdown renderer.

### Heavy / Specialized Widgets

- `file-browser`: replace placeholder with real VFS browsing or a viewer-host-aware equivalent.
- `support-chat-widget`: decide whether chat/AI dependencies belong in `viewer-next`; port inline/floating view if yes.
- `competitors-map`: port MapLibre-based view and its data clients if map pages must render in the viewer.
- Dynamic widgets from `useWidgetLoader`: decide whether page viewer should load external widget libraries.

## Client And Data Layer

- Replace lightweight helpers in `client.ts` with faithful equivalents for:
  - pages
  - posts
  - pictures
  - feed
  - categories
  - places / map data
  - VFS
- Decide whether to install/use React Query or keep a small local cache layer.
- Add auth/token handling only if private/unlisted pages must work in viewer mode.
- Normalize API base URL handling for production WebView and Vite dev.

## Providers And Shared Runtime

- Port or replace only the view-mode provider surface needed by widgets:
  - layout context
  - feed cache / media refresh
  - profiles
  - app config / i18n
  - router/navigation shims
- Avoid pulling editor providers until the editor phase starts.

## Dependencies To Revisit

Likely needed for faithful view parity:

- `react-router-dom`
- `@tanstack/react-query`
- `react-helmet-async`
- `sonner`
- `maplibre-gl`
- `@vidstack/react`
- selected Radix/shadcn primitives used by view widgets
- `clsx` / `tailwind-merge` if porting `cn`
- `@polymech/shared` or local viewer-safe type copies

Install only when the corresponding real widget code is ported.

## Styling

- Audit Tailwind classes used by the real `pm-pics` widgets against Tailwind v4 in `viewer-next`.
- Add missing design tokens used by shadcn-style components.
- Preserve WebView-friendly scroll behavior and dark mode.
- Avoid importing the full `pm-pics` stylesheet unless it is scoped and safe.

## Editor Phase (Deferred)

Do not port these until the viewer is working:

- `UserPageEdit`
- page ribbon / command picker
- layout drag-and-drop editing
- widget property panels
- image/page/category picker edit UI
- AI layout wizard
- template manager
- email/send tooling

## Verification Checklist

- `/viewer/page` loads the default NodeHub page.
- `/viewer/page?src=<pixlwiz page json>` loads through dev proxy.
- `/viewer/page?src=<service.polymech.info page json>` loads through dev proxy.
- Pages with each registered widget render without fallback placeholders.
- Nested tabs/layouts render.
- `npm run build` passes.
- WebView embed build still passes after dependency additions.
