#import <AppKit/AppKit.h>

/// Tabler `icons/filled/*.svg` — ThorVG CPU raster, `currentColor` → RGB tint, centered in @p slotPx
/// square with premultiplied pad color (matches Win32 `PadBitmapCentered` + `padFill` = bar).
NSImage* PmToolbarIconFromSvg(NSString* svgFileName, int iconPx, int slotPx, uint8_t r, uint8_t g, uint8_t b,
                              NSColor* padColor);

/// Try bundle `Resources/tabler-icons/icons/filled`, then `…/dist-osx/tabler-icons/...`, then env `PM_TABLER_FILLED`, then dev tree.
NSString* PmResolveTablerSvgPath(NSString* fileName);
