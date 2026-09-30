// PM-Image — AppKit: Tabler+ThorVG icon toolbar (parity with Win own-ribbon), splits, log, resize engine.
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <dispatch/dispatch.h>

#include <string>

#ifdef PM_TOOLBAR_SVG
#import "pm_svg_raster_mac.h"
#endif

#include "pm_dev_settings.hpp"
#include "pm_image_gui_log.hpp"
#include "resize.hpp"

// Win32 `RibbonUI.h` / `OwnRibbonTab.cpp` command ids (Home tab).
enum {
  kCmdClear        = 301,
  kCmdAddFiles     = 302,
  kCmdAddFolder    = 303,
  kCmdResize       = 300,
  kCmdRun          = 312,
  kCmdCompress     = 313,
  kCmdMeta         = 314,
  kCmdTransform    = 315,
  kCmdFind         = 316,
  kCmdChat         = 317,
  kCmdPause        = 319,
  kCmdSaveAs       = 357,
  kCmdDuplicates   = 324,
  kCmdResetLayout  = 355,
  kCmdAppSettings  = 370,
  kCmdResume       = 320,
  kCmdCancel       = 321,
  kCmdSaveSession  = 322,
  kCmdLoadSession  = 323,
  kCmdDebug        = 356,
};

@protocol PmImageChromeTarget <NSObject>
- (void)pmToolbarAppearanceChanged;
@end

/** Toolbar strip — layer + `controlBackgroundColor`; notifies @c appearanceTarget when theme flips. */
@interface PmChromeBarView : NSView
@property(nonatomic, weak) id<PmImageChromeTarget> appearanceTarget;
@end
@implementation PmChromeBarView
- (void)viewDidChangeEffectiveAppearance
{
  [super viewDidChangeEffectiveAppearance];
  if (self.wantsLayer) {
    self.layer.backgroundColor  = [[NSColor controlBackgroundColor] CGColor];
  }
  id<PmImageChromeTarget> t  = self.appearanceTarget;
  if (t != nil) {
    [t pmToolbarAppearanceChanged];
  }
}
- (void)layout
{
  [super layout];
  if (self.wantsLayer) {
    self.layer.backgroundColor  = [[NSColor controlBackgroundColor] CGColor];
  }
}
@end

static std::string PmNsToStd(NSString *s) {
  if (s == nil) {
    return {};
  }
  return std::string(s.UTF8String);
}

/** One row, matching `OwnRibbonTab::HomeLayout` + `ViewFooterLayout` (separators as `isSep`). */
typedef struct {
  int         isSep;
  NSInteger   tag;
  const char* svg;
  uint8_t     r, g, b;
  const char* en;
} PmTDef;
static const PmTDef kPmToolbar[] = {
    {0, kCmdAddFiles,   "file-upload.svg", 59,  130, 246,  "Add files"},
    {0, kCmdAddFolder,  "folder.svg",      16,  185, 129,  "Add folder"},
    {0, kCmdClear,      "trash.svg",       239, 68,  68,   "Clear"},
    {1, 0,              NULL,              0,   0,   0,    0},
    {0, kCmdSaveAs,     "file-pencil.svg", 99,  102, 241,  "Save as"},
    {1, 0,              NULL,              0,   0,   0,    0},
    {0, kCmdResize,     "photo.svg",       14,  165, 233,  "Resize"},
    {0, kCmdCompress,   "archive.svg",     168, 85,  247,  "Compress"},
    {0, kCmdMeta,       "tags.svg",        245, 158, 11,   "Meta"},
    {0, kCmdTransform,  "sparkles.svg",    236, 72,  153,  "Transform"},
    {0, kCmdFind,       "search.svg",      20,  184, 166,  "Find"},
    {0, kCmdDuplicates, "stack-2.svg",     16,  185, 129,  "Duplicates"},
    {0, kCmdChat,       "message-circle.svg", 6, 182, 212,  "Chat"},
    {1, 0,              NULL,              0,   0,   0,    0},
    {0, kCmdRun,        "player-play.svg", 34,  197, 94,   "Run"},
    {0, kCmdPause,      "player-pause.svg", 156, 163, 175, "Pause"},
    {0, kCmdResume,     "player-track-next.svg", 74, 222, 128, "Resume"},
    {0, kCmdCancel,     "player-stop.svg", 248, 113, 113,  "Cancel"},
    {1, 0,              NULL,              0,   0,   0,    0},
    {0, kCmdSaveSession, "bookmark.svg",  96,  165, 250,  "Save session"},
    {0, kCmdLoadSession, "folder-open.svg", 52,  211, 153,  "Load session"},
    {1, 0,              NULL,              0,   0,   0,    0},
    {0, kCmdResetLayout,  "layout.svg",     148, 163, 184,  "Reset layout"},
    {0, kCmdDebug,     "bug.svg",         251, 191, 36,   "Debug"},
    {0, kCmdAppSettings, "settings.svg",  167, 139, 250,  "App settings"},
};

@interface PmImageAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource,
                                         NSTableViewDelegate, NSSplitViewDelegate, PmImageChromeTarget> {
  dispatch_queue_t _engineDq;
}
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSView *contentRoot;
@property(nonatomic, strong) PmChromeBarView *barView;
@property(nonatomic, strong) NSScrollView *iconToolbarScroll;
/** Horizontal row of icon buttons (document view of @c iconToolbarScroll). */
@property(nonatomic, strong) NSView *toolbarIconDoc;
@property(nonatomic, strong) NSView *maxFieldsTrailing;
@property(nonatomic, strong) NSTextField *maxWField;
@property(nonatomic, strong) NSTextField *maxHField;
/** Root body — [ work row (explorer | queue|preview) | log ]. */
@property(nonatomic, strong) NSSplitView *bodySplit;
@property(nonatomic, strong) NSSplitView *mainHSplit;
@property(nonatomic, strong) NSSplitView *innerHSplit;
@property(nonatomic, strong) NSScrollView *explorerScroll;
@property(nonatomic, strong) NSScrollView *tableScroll;
@property(nonatomic, strong) NSTableView *tableView;
@property(nonatomic, strong) NSView *imageContainer;
@property(nonatomic, strong) NSImageView *imageView;
@property(nonatomic, strong) NSScrollView *logScroll;
@property(nonatomic, strong) NSTextView *logText;
@property(nonatomic, strong) NSMutableArray<NSString *> *filePaths;
@end

@implementation PmImageAppDelegate

- (instancetype)init {
  self = [super init];
  if (self) {
    _filePaths  = [NSMutableArray array];
    _engineDq   = dispatch_queue_create("com.polymech.pm-image-gui.engine", DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (void)appendLog:(NSString *)line {
  if (line != nil) {
    pm_image_gui_log_info(line.UTF8String);
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    if (self.logText == nil) {
      return;
    }
    NSString *s = [line stringByAppendingString:@"\n"];
    [self.logText.textStorage
        appendAttributedString:[[NSAttributedString alloc] initWithString:s]];
    [self.logText scrollToEndOfDocument:self];
  });
}

static NSTextField *pmLabel(NSString *t) {
  NSTextField *f     = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 40, 20)];
  f.stringValue      = t;
  f.editable         = NO;
  f.bezeled          = NO;
  f.drawsBackground  = NO;
  f.selectable       = NO;
  f.font             = [NSFont systemFontOfSize:11.0];
  return f;
}

- (void)pmAddToolbarSeparatorInView:(NSView *)row x:(CGFloat *)px
{
  const CGFloat  h  = 40.0;
  const CGFloat  y  = 8.0;
  *px += 4.0;
  NSView*  g  = [[NSView alloc] initWithFrame:NSMakeRect(*px, y, 1.0, h)];
  [g setWantsLayer:YES];
  g.layer.backgroundColor  = [[NSColor separatorColor] CGColor];
  [row addSubview:g];
  *px += 1.0 + 8.0;
}

- (NSButton *)pmNewToolbarButtonForDef:(const PmTDef *)d x:(CGFloat)x inView:(NSView *)row
{
  const CGFloat  cellW  = 64.0;
  const CGFloat  cellH  = 60.0;
  NSButton*  b
      = [[NSButton alloc] initWithFrame:NSMakeRect(x, 2, cellW, cellH)];
  [b setButtonType:NSButtonTypeMomentaryLight];
  [b setBezelStyle:NSBezelStyleShadowlessSquare];
  [b setBordered:NO];
  [b setTitle:[NSString stringWithUTF8String:d->en]];
  if (@available(macOS 11.0, *)) {
    [b setImageHugsTitle:YES];
  }
  [b setImagePosition:NSImageAbove];
  [b setImageScaling:NSImageScaleProportionallyDown];
  [b setFont:[NSFont systemFontOfSize:9.0]];
  [b setTarget:self];
  [b setAction:@selector(onToolbarCommand:)];
  [b setTag:d->tag];
  [b setToolTip:[NSString stringWithUTF8String:d->en]];
  [b
      setIdentifier:
          [NSString
              stringWithFormat:@"%@:%d,%d,%d", [NSString stringWithUTF8String:d->svg], (int) d->r,
                                 (int) d->g, (int) d->b]];

#ifdef PM_TOOLBAR_SVG
  {
    NSColor*  pad  = [NSColor controlBackgroundColor];
    NSImage*  im
        = PmToolbarIconFromSvg([NSString stringWithUTF8String:d->svg], 30, 58, d->r, d->g, d->b, pad);
    if (im != nil) {
      [b setImage:im];
    } else {
      [b setBordered:YES];
    }
  }
#endif
  if (d->tag == kCmdRun) {
    [b setKeyEquivalent:@"r"];
    [b setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
  }
  [row addSubview:b];
  return b;
}

- (void)pmBuildToolbar
{
  [self.barView setWantsLayer:YES];
  if (self.barView.layer != nil) {
    self.barView.layer.backgroundColor  = [[NSColor controlBackgroundColor] CGColor];
  }
  self.barView.appearanceTarget  = self;

  self.iconToolbarScroll  = [[NSScrollView alloc] init];
  [self.iconToolbarScroll setDrawsBackground:NO];
  [self.iconToolbarScroll setHasVerticalScroller:NO];
  [self.iconToolbarScroll setHasHorizontalScroller:YES];
  [self.iconToolbarScroll setAutohidesScrollers:YES];
  [self.iconToolbarScroll setBorderType:NSNoBorder];
  [self.iconToolbarScroll setAutoresizingMask:0];
  [self.barView addSubview:self.iconToolbarScroll];

  self.toolbarIconDoc  = [[NSView alloc] init];
  [self.iconToolbarScroll setDocumentView:self.toolbarIconDoc];
  [self.iconToolbarScroll setAutoresizesSubviews:YES];

  self.maxFieldsTrailing  = [[NSView alloc] init];
  [self.maxFieldsTrailing setAutoresizingMask:0];
  [self.barView addSubview:self.maxFieldsTrailing];

  {
    NSTextField*  wLab  = pmLabel(@"W");
    NSTextField*  hLab  = pmLabel(@"H");
    [wLab setFrame:NSMakeRect(4, 32, 24, 16)];
    [hLab setFrame:NSMakeRect(100, 32, 20, 16)];
    [self.maxFieldsTrailing addSubview:wLab];
    [self.maxFieldsTrailing addSubview:hLab];
  }
  self.maxWField  = [[NSTextField alloc] initWithFrame:NSMakeRect(28, 30, 60, 22)];
  [self.maxWField setStringValue:@"800"];
  self.maxHField  = [[NSTextField alloc] initWithFrame:NSMakeRect(124, 30, 60, 22)];
  [self.maxHField setStringValue:@"800"];
  [self.maxFieldsTrailing addSubview:self.maxWField];
  [self.maxFieldsTrailing addSubview:self.maxHField];

  CGFloat  x  = 4.0;
  const size_t  n  = sizeof(kPmToolbar) / sizeof(kPmToolbar[0]);
  for (size_t i = 0; i < n; ++i) {
    const PmTDef*  d  = &kPmToolbar[i];
    if (d->isSep) {
      [self pmAddToolbarSeparatorInView:self.toolbarIconDoc x:&x];
      continue;
    }
    [self pmNewToolbarButtonForDef:d x:x inView:self.toolbarIconDoc];
    x += 64.0;
  }
  [self.toolbarIconDoc setFrame:NSMakeRect(0, 0, x + 4.0, 64.0)];
  [self.iconToolbarScroll setDocumentView:self.toolbarIconDoc];
}

- (void)pmLayoutContent {
  if (self.contentRoot == nil || self.window == nil) {
    return;
  }
  const NSRect  B  = self.window.contentView.bounds;
  const CGFloat bH  = 80.0;
  const CGFloat m  = 4.0;
  [self.contentRoot setFrame:B];
  [self.barView setFrame:NSMakeRect(0, NSMaxY(B) - bH, NSWidth(B), bH)];
  {
    const CGFloat  trW  = 196.0;
    const CGFloat  scW  = fmax(100.0, NSWidth(self.barView.bounds) - trW - 2.0 * m);
    [self.iconToolbarScroll setFrame:NSMakeRect(m, 4, scW, bH - 2.0 * m)];
    [self.maxFieldsTrailing
        setFrame:NSMakeRect(m + scW, 4, trW, bH - 2.0 * m)];
  }
  [self.bodySplit
      setFrame:NSMakeRect(0, 0, NSWidth(B), fmax(1.0, NSHeight(B) - bH))];
}

- (void)pmTryApplyDevSplitDefaults {
  if ([[NSUserDefaults standardUserDefaults] boolForKey:@"PMImageGuiSeededFromDevV1"]) {
    return;
  }
  if (self.bodySplit == nil || self.mainHSplit == nil || self.innerHSplit == nil) {
    return;
  }
  const NSRect b = self.bodySplit.bounds;
  if (b.size.width < 200.0 || b.size.height < 200.0) {
    return;
  }
  PmDevGuiDefaults     def  = {};
  std::string          err  = "";
  const bool           have  = pm_dev_load_gui_defaults(def, err);
  if (!have) {
    [[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"PMImageGuiSeededFromDevV1"];
    return;
  }

  // Body: top (work area) / bottom (log) — isVertical=NO, index 0 divider from top
  [self.bodySplit
      setPosition:fmax(100.0, b.size.height - (CGFloat) def.log_height_pt)
  ofDividerAtIndex:0];

  const NSRect mh  = self.mainHSplit.bounds;
  if (mh.size.width > 0) {
    [self.mainHSplit setPosition:fmin((CGFloat) def.explorer_width_pt, mh.size.width - 120.0)
        ofDividerAtIndex:0];
  }
  const NSRect     inr = self.innerHSplit.bounds;
  if (inr.size.width > 0) {
    [self.innerHSplit setPosition:fmin(280.0, inr.size.width - 200.0) ofDividerAtIndex:0];
  }
  [[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"PMImageGuiSeededFromDevV1"];
}

#pragma mark - NSSplitViewDelegate

- (CGFloat)  splitView:(NSSplitView *)sv
    constrainMinCoordinate:(CGFloat)proposed
             ofSubviewAt:(NSInteger)divider {
  (void)divider;
  if (sv == self.bodySplit) {
    return fmax(100.0, proposed);
  }
  if (sv == self.mainHSplit) {
    return fmax(80.0, proposed);
  }
  if (sv == self.innerHSplit) {
    return fmax(120.0, proposed);
  }
  return proposed;
}

- (CGFloat)  splitView:(NSSplitView *)sv
    constrainMaxCoordinate:(CGFloat)proposed
             ofSubviewAt:(NSInteger)divider {
  (void)divider;
  if (sv == self.bodySplit) {
    const CGFloat tot = NSHeight(sv.bounds);
    return fmin(fmax(64.0, tot - 60.0), proposed);
  }
  if (sv == self.mainHSplit) {
    const CGFloat tot = NSWidth(sv.bounds);
    return fmin(fmax(160.0, tot - 200.0), proposed);
  }
  if (sv == self.innerHSplit) {
    const CGFloat tot = NSWidth(sv.bounds);
    return fmin(fmax(160.0, tot - 200.0), proposed);
  }
  return proposed;
}

- (void)buildWindow {
  const NSRect frame = NSMakeRect(80, 80, 1000, 720);
  const NSWindowStyleMask style
      = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        | NSWindowStyleMaskResizable;
  self.window
      = [[NSWindow alloc] initWithContentRect:frame
                                    styleMask:style
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
  [self.window setTitle:@"PM-Image"];
  [self.window setDelegate:self];
  [self.window setMinSize:NSMakeSize(700, 400)];

  self.contentRoot = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
  [self.contentRoot setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  [self.window.contentView addSubview:self.contentRoot];
  [self.contentRoot setWantsLayer:YES];

  self.barView  = [[PmChromeBarView alloc] init];
  [self.contentRoot addSubview:self.barView];
  [self pmBuildToolbar];

  // ── Explorer stub (movable via split) ─────────────────────────────
  self.explorerScroll = [[NSScrollView alloc] init];
  [self.explorerScroll setDrawsBackground:YES];
  if (@available(macOS 10.14, *)) {
    [self.explorerScroll setBackgroundColor:[NSColor controlBackgroundColor]];
  }
  {
    const NSRect  er = NSMakeRect(0, 0, 200, 120);
    NSTextField  *st = [[NSTextField alloc] initWithFrame:er];
    [st
        setStringValue:
            @"Explorer (stub)\n"
             "Movable left panel. A real file tree + selection → preview will replace this, "
             "per dev.json / settings."];
    [st setEditable:NO];
    [st setBezeled:NO];
    [st setDrawsBackground:NO];
    [st setFont:[NSFont systemFontOfSize:12.0]];
    [st setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [self.explorerScroll setDocumentView:st];
    [self.explorerScroll setHasVerticalScroller:YES];
  }

  // ── Queue (table) + image preview (horizontal) ─────────────────────
  self.tableView = [[NSTableView alloc] init];
  NSTableColumn *col  = [[NSTableColumn alloc] initWithIdentifier:@"p"];
  col.title   = @"Queue";
  col.width   = 200;
  [col setResizingMask:NSTableColumnAutoresizingMask];
  {
    NSTextFieldCell *dc  = [[NSTextFieldCell alloc] init];
    [dc setLineBreakMode:NSLineBreakByTruncatingHead];
    [col setDataCell:dc];
  }
  [self.tableView addTableColumn:col];
  [self.tableView setHeaderView:nil];
  [self.tableView setDataSource:self];
  [self.tableView setIntercellSpacing:NSMakeSize(4, 2)];
  [self.tableView setRowHeight:22.0];
  if (@available(macOS 10.14, *)) {
    [self.tableView setBackgroundColor:[NSColor controlBackgroundColor]];
  }

  self.tableScroll  = [[NSScrollView alloc] init];
  [self.tableScroll setDocumentView:self.tableView];
  [self.tableScroll setHasVerticalScroller:YES];
  [self.tableScroll setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

  self.imageView       = [[NSImageView alloc] init];
  [self.imageView setImageScaling:NSImageScaleProportionallyUpOrDown];
  self.imageContainer  = [[NSView alloc] init];
  [self.imageContainer setWantsLayer:YES];
  [self.imageView setImageAlignment:NSImageAlignCenter];
  [self.imageView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  [self.imageContainer addSubview:self.imageView];
  [self.imageContainer
      setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

  self.innerHSplit  = [[NSSplitView alloc] init];
  [self.innerHSplit setVertical:YES];
  [self.innerHSplit setDividerStyle:NSSplitViewDividerStyleThin];
  [self.innerHSplit
      setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  [self.innerHSplit setDelegate:self];
  [self.innerHSplit
      setAutosaveName:
          @"com.polymech.pm-image-gui.split.queuePreview"];
  [self.innerHSplit addSubview:self.tableScroll];
  [self.innerHSplit addSubview:self.imageContainer];
  for (NSView *v in self.innerHSplit.subviews) {
    [v setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  }

  // ── Explorer | (queue+preview) ────────────────────────────────────
  [self.explorerScroll
      setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  self.mainHSplit = [[NSSplitView alloc] init];
  [self.mainHSplit setVertical:YES];
  [self.mainHSplit setDividerStyle:NSSplitViewDividerStyleThin];
  [self.mainHSplit setDelegate:self];
  [self.mainHSplit
      setAutosaveName:
          @"com.polymech.pm-image-gui.split.explorer"];
  [self.mainHSplit addSubview:self.explorerScroll];
  [self.mainHSplit addSubview:self.innerHSplit];
  for (NSView *v in self.mainHSplit.subviews) {
    [v setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  }

  // Log
  self.logText   = [[NSTextView alloc] init];
  [self.logText setFont:[NSFont userFixedPitchFontOfSize:11.0]];
  [self.logText setEditable:NO];
  [self.logText setMinSize:NSMakeSize(0, 0)];
  [self.logText setMaxSize:NSMakeSize(1.0e7, 1.0e7)];
  [self.logText setVerticallyResizable:YES];
  [self.logText setHorizontallyResizable:NO];
  self.logScroll = [[NSScrollView alloc] init];
  [self.logScroll setDocumentView:self.logText];
  [self.logScroll setHasVerticalScroller:YES];
  [self.logScroll setBorderType:NSBezelBorder];
  [self.logScroll setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

  // Work row + log
  self.bodySplit = [[NSSplitView alloc] init];
  [self.bodySplit setVertical:NO];
  [self.bodySplit setDividerStyle:NSSplitViewDividerStyleThin];
  [self.bodySplit setDelegate:self];
  [self.bodySplit
      setAutosaveName:
          @"com.polymech.pm-image-gui.split.bodyLog"];
  [self.bodySplit addSubview:self.mainHSplit];
  [self.bodySplit addSubview:self.logScroll];
  for (NSView *v in self.bodySplit.subviews) {
    [v setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  }
  [self.contentRoot addSubview:self.bodySplit];

  [self pmLayoutContent];
  [self
      appendLog:
          @"PM-Image workbench — Panels are resizable (movable dividers). Explorer is a stub. "
          @"`dev.json` (or PM_IMAGE_DEV_JSON) seeds first-run layout when present."];

  [self.window center];
  [self.window makeKeyAndOrderFront:self];
  [NSApp activateIgnoringOtherApps:YES];

  // After layout pass, one-shot sizes from `dist/dev.json` (win32_dock dock 7/1)
  __weak PmImageAppDelegate *wself  = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    PmImageAppDelegate *D = wself;
    if (D != nil) {
      [D pmTryApplyDevSplitDefaults];
    }
  });
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
  (void)n;
  [self buildWindow];
}

- (void)windowDidResize:(NSNotification *)n {
  (void)n;
  [self pmLayoutContent];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)s {
  (void)s;
  return YES;
}

- (void)onOpen:(id)sender {
  (void)sender;
  NSOpenPanel *p  = [NSOpenPanel openPanel];
  p.allowsMultipleSelection = YES;
  p.canChooseFiles          = YES;
  p.canChooseDirectories    = NO;
  if (@available(macOS 12.0, *)) {
    p.allowedContentTypes  = @[
      UTTypePNG, UTTypeJPEG, UTTypeGIF, UTTypeTIFF, UTTypeBMP, UTTypeWebP,
    ];
  } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    p.allowedFileTypes
        = @[ @"png", @"jpg", @"jpeg", @"gif", @"webp", @"tiff", @"tif", @"bmp" ];
#pragma clang diagnostic pop
  }
  if ([p runModal] != NSModalResponseOK) {
    return;
  }
  for (NSURL *u in p.URLs) {
    if (u.path == nil) {
      continue;
    }
    if (![self.filePaths containsObject:u.path]) {
      [self.filePaths addObject:u.path];
    }
  }
  [self.tableView reloadData];
  if (self.filePaths.count >= 1) {
    NSImage *im = [[NSImage alloc] initWithContentsOfFile:self.filePaths[0]];
    [self.imageView setImage:im];
  }
}

- (void)onAddFolder {
  NSOpenPanel*  p  = [NSOpenPanel openPanel];
  p.allowsMultipleSelection  = NO;
  p.canChooseFiles           = NO;
  p.canChooseDirectories     = YES;
  if ([p runModal] != NSModalResponseOK) {
    return;
  }
  NSURL*  u  = p.URLs.firstObject;
  if (u.path == nil) {
    return;
  }
  NSArray*  exts  = @[
    @"png", @"jpg", @"jpeg", @"gif", @"webp", @"tiff", @"tif", @"bmp"
  ];
  NSFileManager*  fm  = [NSFileManager defaultManager];
  NSMutableSet*  seen  = [NSMutableSet set];
  NSString*  const root  = u.path;
  NSDirectoryEnumerator*  en
      = [fm enumeratorAtURL:[NSURL fileURLWithPath:root isDirectory:YES]
         includingPropertiesForKeys:@[]
                            options:NSDirectoryEnumerationSkipsHiddenFiles
                       errorHandler:nil];
  for (NSURL* fu in en) {
    if (![fu isFileURL]) {
      continue;
    }
    NSString*  rel  = [fu path];
    if (rel == nil) {
      continue;
    }
    NSString*  pe  = rel.pathExtension.lowercaseString;
    if (pe.length < 1) {
      continue;
    }
    if (![exts containsObject:pe]) {
      continue;
    }
    if (![seen containsObject:rel]) {
      [seen addObject:rel];
      if (![self.filePaths containsObject:rel]) {
        [self.filePaths addObject:rel];
      }
    }
  }
  [self.tableView reloadData];
  if (self.filePaths.count >= 1) {
    NSImage*  im  = [[NSImage alloc] initWithContentsOfFile:self.filePaths[0]];
    [self.imageView setImage:im];
  }
  [self appendLog:[NSString stringWithFormat:@"Added from folder: %@ (%lu image(s) in queue).", root, (unsigned long) self.filePaths.count]];
}

- (void)onClearQueue
{
  [self.filePaths removeAllObjects];
  [self.tableView reloadData];
  [self.imageView setImage:nil];
  [self appendLog:@"Cleared queue."];
}

- (void)onToolbarCommand:(id)sender
{
  if (![sender isKindOfClass:[NSButton class]]) {
    return;
  }
  const NSInteger  t  = [(NSButton *) sender tag];
  switch (t) {
  case kCmdAddFiles: [self onOpen:sender]; break;
  case kCmdAddFolder: [self onAddFolder]; break;
  case kCmdClear: [self onClearQueue]; break;
  case kCmdRun: [self onRunResize:sender]; break;
  case kCmdSaveAs: [self appendLog:@"Save As — not yet on macOS."]; break;
  case kCmdResize: [self appendLog:@"Resize: set W / H to the right, then Run."]; break;
  case kCmdCompress: [self appendLog:@"Compress — coming soon."]; break;
  case kCmdMeta: [self appendLog:@"Meta — coming soon."]; break;
  case kCmdTransform: [self appendLog:@"Transform — coming soon."]; break;
  case kCmdFind: [self appendLog:@"Find — coming soon."]; break;
  case kCmdDuplicates: [self appendLog:@"Duplicates — coming soon."]; break;
  case kCmdChat: [self appendLog:@"Chat — coming soon."]; break;
  case kCmdPause: [self appendLog:@"Pause — coming soon."]; break;
  case kCmdResume: [self appendLog:@"Resume — coming soon."]; break;
  case kCmdCancel: [self appendLog:@"Cancel — coming soon."]; break;
  case kCmdSaveSession: [self appendLog:@"Save session — coming soon."]; break;
  case kCmdLoadSession: [self appendLog:@"Load session — coming soon."]; break;
  case kCmdResetLayout: [self appendLog:@"Reset layout — not yet."]; break;
  case kCmdDebug: [self appendLog:@"Debug — coming soon."]; break;
  case kCmdAppSettings: [self appendLog:@"App settings — coming soon."]; break;
  default: [self appendLog:[NSString stringWithFormat:@"Unknown command %ld.", (long) t]]; break;
  }
}

#ifdef PM_TOOLBAR_SVG
- (void)pmRefreshToolbarButtonImages
{
  NSColor*  pad  = [NSColor controlBackgroundColor];
  for (NSView* v in self.toolbarIconDoc.subviews) {
    if (![v isKindOfClass:[NSButton class]]) {
      continue;
    }
    NSButton*  b  = (NSButton *) v;
    if (b.tag < 1 || b.identifier == nil) {
      continue;
    }
    NSArray*  a  = [b.identifier componentsSeparatedByString:@":"];
    if (a.count < 2) {
      continue;
    }
    NSString*  file  = a[0];
    NSArray*  cs     = [a[1] componentsSeparatedByString:@","];
    if (cs.count < 3) {
      continue;
    }
    int  r  = (int) [cs[0] intValue];
    int  g  = (int) [cs[1] intValue];
    int  bb  = (int) [cs[2] intValue];
    NSImage*  im  = PmToolbarIconFromSvg(file, 30, 58, (uint8_t) r, (uint8_t) g, (uint8_t) bb, pad);
    if (im != nil) {
      [b setImage:im];
    }
  }
}
#endif

- (void)pmToolbarAppearanceChanged
{
#ifdef PM_TOOLBAR_SVG
  [self pmRefreshToolbarButtonImages];
#endif
  if (self.barView.wantsLayer) {
    self.barView.layer.backgroundColor  = [[NSColor controlBackgroundColor] CGColor];
  }
}

- (void)onRunResize:(id)sender {
  (void)sender;
  if (self.filePaths.count < 1) {
    [self appendLog:@"Add at least one file first (queue or Add files…)."];
    return;
  }
  int   w  = (int) [self.maxWField.stringValue intValue];
  int   h  = (int) [self.maxHField.stringValue intValue];
  if (w <= 0) {
    w = 800;
  }
  if (h <= 0) {
    h = 800;
  }
  [self
      appendLog:
          [NSString
              stringWithFormat:
                  @"Resizing (max %d×%d) %lu file(s) on background engine queue…", w, h,
                  (unsigned long) self.filePaths.count]];

  __weak PmImageAppDelegate *wself  = self;
  NSArray<NSString *>      *copy  = [self.filePaths copy];
  const int                 wMax   = w;
  const int                 hMax   = h;

  dispatch_async(
      _engineDq,
      ^{
        for (NSString *p in copy) {
          if (wself == nil) {
            return;
          }
          NSString     *base = p.lastPathComponent;
          NSString     *stem = base.stringByDeletingPathExtension;
          NSString     *tmpd
              = [NSTemporaryDirectory() stringByAppendingPathComponent:@"pm-image-gui-out"];
          NSError      *e  = nil;
          if (![NSFileManager.defaultManager createDirectoryAtPath:tmpd
                                        withIntermediateDirectories:YES
                                                     attributes:nil
                                                          error:&e]) {
            NSString *m = e.localizedDescription;
            if (m == nil) {
              m = @"(error)";
            }
            dispatch_sync(
                dispatch_get_main_queue(), ^{
                  PmImageAppDelegate *D = wself;
                  if (D != nil) {
                    [D appendLog:[@"mkdir: " stringByAppendingString:m]];
                  }
                });
            return;
          }
          NSString     *outP
              = [[tmpd stringByAppendingPathComponent:stem] stringByAppendingString:@"_pmgui.jpg"];

          const std::string in8  = PmNsToStd(p);
          const std::string out8  = PmNsToStd(outP);
          std::string        err;
          media::ResizeOptions        opt;
          opt.max_width     = wMax;
          opt.max_height     = hMax;
          opt.format     = "jpeg";
          opt.cache_enabled  = false;
          const bool        ok  = media::resize_file(in8, out8, opt, err);

          if (ok) {
            const std::string line  = std::string("OK: ") + out8;
            dispatch_sync(
                dispatch_get_main_queue(), ^{
                  PmImageAppDelegate *D  = wself;
                  if (D == nil) {
                    return;
                  }
                  [D appendLog:[NSString stringWithUTF8String:line.c_str()]];
                  [D.imageView setImage:[[NSImage alloc] initWithContentsOfFile:outP]];
                });
          } else {
            const std::string line  = err + " — " + in8;
            dispatch_sync(
                dispatch_get_main_queue(), ^{
                  PmImageAppDelegate *D  = wself;
                  if (D == nil) {
                    return;
                  }
                  [D appendLog:[NSString stringWithUTF8String:line.c_str()]];
                });
          }
        }
        dispatch_sync(
            dispatch_get_main_queue(), ^{
              PmImageAppDelegate *D  = wself;
              if (D != nil) {
                [D appendLog:@"Batch finished."];
              }
            });
      });
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv {
  (void)tv;
  return (NSInteger) self.filePaths.count;
}

- (id)tableView:(NSTableView *)tv
    objectValueForTableColumn:(NSTableColumn *)c
                            row:(NSInteger)row {
  (void)tv;
  (void)c;
  if (row < 0 || row >= (NSInteger) self.filePaths.count) {
    return @"";
  }
  return self.filePaths[(NSUInteger) row];
}

@end

int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    pm_image_gui_log_init();
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    PmImageAppDelegate *d  = [[PmImageAppDelegate alloc] init];
    [NSApp setDelegate:d];
    [NSApp run];
  }
  return 0;
}
