# pm-win32-mini

Small Win32 sandbox in `packages/media/cpp` (no `pm-image` / vips). It experiments with **borderless chrome**, **DWM hints**, **per-pixel alpha via `UpdateLayeredWindow`**, a **custom caption** (min / max / close), and a **Direct2D + WIC filmstrip** (`apps/win32-mini/filmstrip/`) composited on top of the same frosted GDI+ shell.

## Build

From `packages/media/cpp`:

```bash
npm run build:win32-mini
```

Output: `packages/media/cpp/dist/pm-win32-mini.exe`.

Filmstrip defaults to `tests/assets/agent` under the current working directory; override with `--src` or `--source` (path resolved against cwd), for example `.\dist\pm-win32-mini.exe --src tests\assets\agent` from `packages/media/cpp`.

CMake preset and source dir live under `apps/win32-mini/` (see `CMakePresets.json` in this folder and `package.json` in the parent `media/cpp` tree).

## Architecture (current `main.cpp`)

- **Window**: `WS_POPUP` with `WS_SYSMENU`, min/max boxes; **no** `WS_CAPTION` / `WS_THICKFRAME` (avoids thick DWM frame lines; resize is synthetic).
- **Extended style**: `WS_EX_APPWINDOW | WS_EX_LAYERED`.
- **Presentation**: Off-screen **32 bpp top-down DIB** (`CreateDIBSection`) + **GDI+** into that DC, then [`UpdateLayeredWindow`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow) with **PARGB** / `ULW_ALPHA`. **`SetLayeredWindowAttributes(LWA_ALPHA)`** is intentionally not used for the main surface (fewer redraw / “trail” issues when the client is constantly re-uploaded).
- **Shape**: Rounded shell via `GraphicsPath` + `FillPath`; pixels **outside** the path are cleared to **α = 0** so the HWND does not show square opaque corners (tradeoff: tiny corner wedges can be **click-through** because hit-testing follows alpha on layered windows).
- **DWM**: `DwmExtendFrameIntoClientArea` (zero margins), `DWMWA_NCRENDERING_POLICY` = disabled, border color “none” where supported, rounded-corner preference hint. This is an experiment host, not a claim of perfect DWM parity on every OS build.
- **Caption**: Owner-drawn strip + `WM_NCHITTEST` returns `HTCAPTION` / `HTMINBUTTON` / `HTMAXBUTTON` / `HTCLOSE`. **`DefWindowProc` does not reliably perform system NC actions** on this borderless popup style, so **`WM_NCLBUTTONDOWN`** is handled explicitly (`PostMessage(WM_SYSCOMMAND, SC_*)` / close path).
- **Fonts on the bitmap**: **Do not use GDI `DrawText` on the 32 bpp DIB** for content that must appear above the desktop: GDI often leaves **alpha = 0**, which reads as **fully transparent “holes”** once the bitmap is sent through `UpdateLayeredWindow`. Use **GDI+** (`DrawString` + `SolidBrush` with explicit **A** in the color).

## Findings (important)

### 1. `UpdateLayeredWindow` and real `WS_CHILD` controls on the same HWND

A layered top-level window driven **only** by `UpdateLayeredWindow` is effectively **one composited bitmap**. **Standard child controls parented to that same HWND** (`BUTTON`, `EDIT`, `STATIC`, …) often **do not appear** or do not composite as expected: they are still there for the message pump in many cases, but **they are not a reliable visual layer on top of the ULW surface**. Per-child **`WS_EX_LAYERED` + `SetLayeredWindowAttributes(LWA_ALPHA)`** was tried and **still did not produce a stable visible UI** in this experiment.

**Practical pattern that works:** host “real” UI on a **separate non-layered `WS_POPUP`** whose **owner** is the glass window. Position that popup in **screen space** over the region of interest (`MapWindowPoints` from the owner’s client). Children of **that** popup use the normal Win32 painting model. An older `main.cpp` prototype used `SetWindowPos(demoHost, owner, x, y, cx, cy, SWP_NOACTIVATE)` so the popup stacked **above** the ULW owner for hit-testing in the overlapped band; the STATIC / BUTTON / EDIT demo has been removed in favor of the D2D filmstrip path.

### 2. Layered hit-testing vs geometry

With **α = 0** outside the rounded path, **resize edges** and **caption** logic use **window-rect math** in `WM_NCHITTEST`; do not assume the user can grab a **transparent** outer wedge—those pixels may fall through to windows below.

### 3. Distance from “stock” Win32 chrome

You are only a **policy / compositing** choice away from a normal titled window (`WS_OVERLAPPEDWINDOW`, default NC). The hard parts explored here are **custom NC + resize**, **DWM interaction**, and **layered presentation**, not a different platform.

### 4. Backups

Frozen snapshots for earlier looks live under `apps/win32-mini/backup/` (see comments at the top of `main.cpp` for filenames and what each captured).

## References

- [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow)
- [Layered windows (overview / remarks)](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)
- [Custom frame (DWM)](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)
- [`DWMWINDOWATTRIBUTE`](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)

## Next steps (later)

- Tune glass vs readability (alphas, typography, ClearType vs anti-alias on translucent backgrounds).
- If **click-through** on corner wedges matters: opaque inset band, smaller radius, or hybrid hit-testing (documented tradeoff only).
- If **shadow / NC** behavior matters under strong alpha: separate experiments (`DwmExtendFrameIntoClientArea` slivers, etc.).
- Productize **owned-popup** vs **embedded WebView / DirectComposition** depending on whether controls must sit *inside* the same visual hull as the ULW bitmap.
