// ThorVG + Tabler SVG → NSImage (parity with `src/win/ui_next/helpers/svg_raster.cpp` on Windows).
#import "pm_svg_raster_mac.h"

#include <mach-o/dyld.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <thorvg.h>

namespace {

void replace_current_color(std::string& s, uint8_t r, uint8_t g, uint8_t b)
{
  const std::string  from  = "currentColor";
  char                 repl[24]{};
  (void) std::snprintf(repl, sizeof repl, "#%02X%02X%02X", (unsigned) r, (unsigned) g, (unsigned) b);
  for (;;) {
    const size_t  p  = s.find(from);
    if (p == std::string::npos) {
      break;
    }
    s.replace(p, from.size(), repl);
  }
}

std::once_flag  s_tvgOnce;

void ensure_tvg()
{
  std::call_once(s_tvgOnce, [] { tvg::Initializer::init(0); });
}

bool read_entire(const std::string& path, std::string& out)
{
  std::ifstream  f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    return false;
  }
  const auto  sz  = f.tellg();
  f.seekg(0);
  if (sz <= 0) {
    return false;
  }
  out.assign(static_cast<size_t>(sz), '\0');
  f.read(out.data(), sz);
  return f.good() || f.eof();
}

NSImage* image_from_abgr_premul(const uint32_t* buf, int w, int h, uint32_t rowStrideU32)
{
  if (w < 1 || h < 1) {
    return nil;
  }
  NSBitmapImageRep*  rep
      = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                pixelsWide:w
                                                pixelsHigh:h
                                             bitsPerSample:8
                                           samplesPerPixel:4
                                                  hasAlpha:YES
                                                  isPlanar:NO
                                            colorSpaceName:NSCalibratedRGBColorSpace
                                                bytesPerRow:w * 4
                                               bitsPerPixel:32];
  if (rep == nil) {
    return nil;
  }
  uint8_t*  dst  = (uint8_t *) [rep bitmapData];
  if (dst == nullptr) {
    return nil;
  }
  for (int y  = 0; y < h; ++y) {
    for (int x  = 0; x < w; ++x) {
      const uint32_t  p
          = buf[static_cast<size_t>(y) * rowStrideU32 + static_cast<size_t>(x)];
      const uint8_t   a  = (uint8_t) ((p >> 24) & 0xFFu);
      const uint8_t   b  = (uint8_t) (p & 0xFFu);
      const uint8_t   g0 = (uint8_t) ((p >> 8) & 0xFFu);
      const uint8_t   r0 = (uint8_t) ((p >> 16) & 0xFFu);
      uint8_t*        o  = dst + 4 * (y * w + x);
      o[0]  = r0; // R
      o[1]  = g0; // G
      o[2]  = b;  // B
      o[3]  = a;  // A
    }
  }
  NSImage*  img  = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
  if (img != nil) {
    [img addRepresentation:rep];
  }
  return img;
}

} // namespace

NSString* PmResolveTablerSvgPath(NSString* fileName)
{
  if (fileName == nil) {
    return nil;
  }
  NSString*  b  = [fileName lastPathComponent];
  // 1) App bundle: Resources/tabler-icons/icons/filled
  {
    NSBundle*  B  = [NSBundle mainBundle];
    NSString*  p  = [B pathForResource:b ofType:nil
                         inDirectory:@"tabler-icons/icons/filled"];
    if (p != nil && [[NSFileManager defaultManager] fileExistsAtPath:p]) {
      return p;
    }
  }
  {
    // 2) `PM_TABLER_FILLED` = directory containing the .svg
    const char*  e  = getenv("PM_TABLER_FILLED");
    if (e != nullptr) {
      std::string  s(e);
      if (!s.empty() && s.back() == '/') {
        s.pop_back();
      }
      NSString*  d  = [NSString stringWithUTF8String:s.c_str()];
      NSString*  t  = [d stringByAppendingPathComponent:b];
      if ([[NSFileManager defaultManager] fileExistsAtPath:t]) {
        return t;
      }
    }
  }
  // 3) Beside executable: …/dist-osx/tabler-icons/icons/filled, …/MacOS/../../../../
  {
    char         buf0[4096];
    uint32_t     s  = sizeof buf0;
    if (_NSGetExecutablePath(buf0, &s) == 0) {
      NSString*  exe  = [NSString stringWithUTF8String:buf0];
      NSString*  d  = [exe stringByDeletingLastPathComponent];
      const NSArray*  cands
          = @[
            [d stringByAppendingPathComponent:@"../tabler-icons/icons/filled"],
            [d stringByAppendingPathComponent:@"../../tabler-icons/icons/filled"],
            [d stringByAppendingPathComponent:@"../../../tabler-icons/icons/filled"],
            [d stringByAppendingPathComponent:@"../../../../tabler-icons/icons/filled"],
            [d stringByAppendingPathComponent:@"../../dist/tabler-icons/icons/filled"],
            [d stringByAppendingPathComponent:@"../../../dist/tabler-icons/icons/filled"]
          ];
      for (NSString*  dir in cands) {
        NSString*  t
            = [dir stringByAppendingPathComponent:b];
        if ([[NSFileManager defaultManager] fileExistsAtPath:t]) {
          return t;
        }
      }
    }
  }
  // 4) CWD (dev source tree)
  {
    char  cwdb[4096];
    if (getcwd(cwdb, sizeof cwdb) != nullptr) {
      NSString*  cwd  = [NSString stringWithUTF8String:cwdb];
      NSString*  t
          = [[cwd
              stringByAppendingPathComponent:
                  @"../../packages/tabler-icons/icons/filled"] stringByAppendingPathComponent:b];
      if ([[NSFileManager defaultManager] fileExistsAtPath:t]) {
        return t;
      }
    }
  }
  return nil;
}

NSImage* PmToolbarIconFromSvg(NSString* svgFileName, int iconPx, int slotPx, uint8_t r, uint8_t g, uint8_t b,
                              NSColor* padColor)
{
  if (svgFileName == nil) {
    return nil;
  }
  NSString*  p  = PmResolveTablerSvgPath(svgFileName);
  if (p == nil) {
    return nil;
  }
  std::string  raw, err;
  if (!read_entire(p.UTF8String, raw) || raw.empty()) {
    return nil;
  }
  ensure_tvg();
  replace_current_color(raw, r, g, b);
  if (iconPx < 4) {
    iconPx  = 32;
  }
  if (slotPx < iconPx) {
    slotPx  = iconPx;
  }
  tvg::Picture*  pic  = tvg::Picture::gen();
  if (pic == nullptr) {
    return nil;
  }
  if (pic->load(raw.data(), (uint32_t) raw.size(), "svg", nullptr, true) != tvg::Result::Success) {
    tvg::Paint::rel(pic);
    return nil;
  }
  pic->size((float) iconPx, (float) iconPx);
  auto*  canvas  = tvg::SwCanvas::gen();
  if (canvas == nullptr) {
    tvg::Paint::rel(pic);
    return nil;
  }
  const uint32_t     strideU32  = (uint32_t) iconPx;
  std::vector<uint32_t>  buf(
      (size_t) iconPx * (size_t) iconPx, 0u);
  if (canvas->target((uint32_t* )buf.data(), strideU32, (uint32_t) iconPx, (uint32_t) iconPx, tvg::ColorSpace::ABGR8888S)
      != tvg::Result::Success) {
    tvg::Paint::rel(pic);
    delete canvas;
    return nil;
  }
  if (canvas->add(pic) != tvg::Result::Success) {
    tvg::Paint::rel(pic);
    delete canvas;
    return nil;
  }
  (void) canvas->update();
  (void) canvas->draw(true);
  (void) canvas->sync();
  delete canvas;
  tvg::Paint::rel(pic);

  NSImage*  icon
      = image_from_abgr_premul(buf.data(), iconPx, iconPx, strideU32);
  if (icon == nil) {
    return nil;
  }
  if (iconPx == slotPx) {
    return icon;
  }
  if (padColor == nil) {
    padColor  = [NSColor controlBackgroundColor];
  }
  NSImage*  slot
      = [[NSImage alloc] initWithSize:NSMakeSize((CGFloat) slotPx, (CGFloat) slotPx)];
  if (slot == nil) {
    return icon;
  }
  [slot lockFocus];
  NSRect  rct  = NSMakeRect(0, 0, (CGFloat) slotPx, (CGFloat) slotPx);
  [padColor setFill];
  NSRectFill(rct);
  const CGFloat  xo  = ((CGFloat) slotPx - (CGFloat) iconPx) / 2.0;
  const CGFloat  yo  = ((CGFloat) slotPx - (CGFloat) iconPx) / 2.0;
  [icon drawInRect:NSMakeRect(xo, yo, (CGFloat) iconPx, (CGFloat) iconPx)
         fromRect:NSMakeRect(0, 0, (CGFloat) iconPx, (CGFloat) iconPx)
        operation:NSCompositingOperationSourceOver
         fraction:1.0
   respectFlipped:YES
            hints:nil];
  [slot unlockFocus];
  return slot;
}
