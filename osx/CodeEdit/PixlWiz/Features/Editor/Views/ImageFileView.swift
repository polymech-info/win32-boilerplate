//
//  ImageFileView.swift
//  CodeEdit
//
//  Created by Paul Ebose on 2024/5/9.
//

import AppKit
import ImageIO
import SwiftUI

// MARK: - AppKit: centering clip + pinch + pan + scroll-wheel zoom

/// Keeps the `documentView` centered when it is **smaller** than the visible clip. When the document is
/// larger, leaves horizontal/vertical scroll behavior to `NSClipView` (panning).
private final class CenteringImageClipView: NSClipView {
    override func constrainBoundsRect(_ proposedBounds: NSRect) -> NSRect {
        var rect = super.constrainBoundsRect(proposedBounds)
        guard let doc = documentView, !doc.frame.isEmpty else { return rect }
        let dw = doc.frame.width
        let dh = doc.frame.height
        let rw = rect.width
        let rh = rect.height
        if dw < rw {
            rect.origin.x = (dw - rw) * 0.5
        }
        if dh < rh {
            rect.origin.y = (dh - rh) * 0.5
        }
        return rect
    }
}

// MARK: - AppKit: pinch + pan + scroll-wheel zoom (NSScrollView.magnification)

/// Scroll view for a single `NSImage` — **pinch** (trackpad) and **scroll wheel** (or **⌘+scroll** on a
/// trackpad) zoom; **pan** with trackpad 2‑finger / **click-drag** / scrollbars when content is larger
/// than the clip. **Coarse** mouse wheel (no precise scrolling deltas) zooms.
final class PolymechZoomableImageScrollView: NSScrollView {
    private let imageView = NSImageView()
    var didInitialFit: Bool = false
    /// After a bad early layout (tiny clip), allow one refit when the real viewport size arrives.
    private var allowViewportGrowthRefit: Bool = true
    private var clipDiagonalWhenLastFitted: CGFloat = 0
    private var isProgrammaticMagnification: Bool = false
    private var magnificationObserver: NSKeyValueObservation?
    /// Status bar: percent of viewport-fit scale (100 = fitted; see ``zoomPercentRelativeToFit()``).
    var onMagnificationForStatusBar: ((CGFloat) -> Void)?
    var image: NSImage? {
        didSet { applyImage() }
    }

    deinit {
        magnificationObserver?.invalidate()
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        configure()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configure()
    }

    private func configure() {
        hasVerticalScroller = true
        hasHorizontalScroller = true
        autohidesScrollers = true
        borderType = .noBorder
        drawsBackground = true
        backgroundColor = .textBackgroundColor
        scrollerStyle = .overlay
        autoresizingMask = [.width, .height]

        allowsMagnification = true
        minMagnification = 0.1
        maxMagnification = 10.0
        magnification = 1.0

        imageView.imageFrameStyle = .none
        imageView.imageScaling = .scaleNone

        let clip = CenteringImageClipView()
        clip.copiesOnScroll = false
        contentView = clip
        documentView = imageView

        // Clip: drags starting on the margin around a smaller-than-viewport image. Image view: drags on the bitmap.
        let makePan: () -> NSPanGestureRecognizer = {
            let g = NSPanGestureRecognizer(target: self, action: #selector(Self.handlePanGesture(_:)))
            // `NSPanGestureRecognizer.buttonMask` is a UInt bitfield; bit 0 = primary (left) button. Default is 0x1.
            g.buttonMask = 1
            g.delaysPrimaryMouseButtonEvents = false
            return g
        }
        clip.addGestureRecognizer(makePan())
        imageView.addGestureRecognizer(makePan())

        magnificationObserver = observe(\.magnification, options: [.old, .new]) { [weak self] _, _ in
            guard let self else { return }
            if !self.isProgrammaticMagnification {
                self.allowViewportGrowthRefit = false
            }
            self.publishMagnificationForStatusBar()
        }
    }

    private func publishMagnificationForStatusBar() {
        onMagnificationForStatusBar?(zoomPercentRelativeToFit())
    }

    /// 100 = current scale matches “fit full image in viewport”; >100 = zoomed in vs that fit.
    func zoomPercentRelativeToFit() -> CGFloat {
        guard let d = documentView, d.bounds.width >= 1, d.bounds.height >= 1 else {
            return magnification * 100
        }
        let (clipW, clipH) = viewportSizeForFit()
        let fitScale = min(min(clipW / d.bounds.width, clipH / d.bounds.height), 1.0)
        let fs = max(fitScale, 0.0001)
        let pct = (magnification / fs) * 100
        return min(max(pct, 0), 50_000)
    }

    /// Reset scroll and magnification to the same **fit-to-viewport** pass used on open (debug / status bar **Fit**).
    func applyViewportFit() {
        didInitialFit = false
        allowViewportGrowthRefit = true
        clipDiagonalWhenLastFitted = 0
        isProgrammaticMagnification = true
        magnification = 1.0
        isProgrammaticMagnification = false
        contentView.setBoundsOrigin(.zero)
        needsLayout = true
        layoutSubtreeIfNeeded()
        applyInitialFitIfNeeded()
        if !didInitialFit {
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.layoutSubtreeIfNeeded()
                self.applyInitialFitIfNeeded()
                self.publishMagnificationForStatusBar()
            }
        }
        publishMagnificationForStatusBar()
    }

    private func applyImage() {
        didInitialFit = false
        allowViewportGrowthRefit = true
        clipDiagonalWhenLastFitted = 0
        guard let img = image else {
            imageView.image = nil
            return
        }
        imageView.image = img
        let s = Self.documentSize(for: img)
        let w = max(1, s.width)
        let h = max(1, s.height)
        imageView.setFrameSize(NSSize(width: w, height: h))
        scheduleDeferredInitialFit()
    }

    /// `NSImage.size` can be zero or stale until representations load; use bitmap pixel dimensions when needed.
    private static func documentSize(for img: NSImage) -> NSSize {
        var s = img.size
        if s.width >= 1, s.height >= 1 { return s }
        for rep in img.representations {
            let pw = rep.pixelsWide
            let ph = rep.pixelsHigh
            if pw > 0, ph > 0 {
                return NSSize(width: CGFloat(pw), height: CGFloat(ph))
            }
        }
        return NSSize(width: max(1, s.width), height: max(1, s.height))
    }

    /// Image often arrives after the first `layout()` pass; SwiftUI may also resize the view one frame later.
    private func scheduleDeferredInitialFit() {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.layoutSubtreeIfNeeded()
            self.applyInitialFitIfNeeded()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak self] in
            guard let self, !self.didInitialFit else { return }
            self.layoutSubtreeIfNeeded()
            self.applyInitialFitIfNeeded()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            guard let self, !self.didInitialFit else { return }
            self.layoutSubtreeIfNeeded()
            self.applyInitialFitIfNeeded()
        }
    }

    /// Visible viewport in **points** for fitting. Prefer the clip view’s **frame** (layout size in the scroll view);
    /// `bounds` is often wrong (0 or huge) until/while SwiftUI hosts the `NSScrollView`.
    private func viewportSizeForFit() -> (CGFloat, CGFloat) {
        var w = contentView.frame.size.width
        var h = contentView.frame.size.height
        if w < 32 || h < 32 {
            let b = contentView.bounds.size
            w = b.width
            h = b.height
        }
        if w < 32 || h < 32 {
            let vis = documentVisibleRect.size
            if vis.width >= 32 && vis.height >= 32 {
                w = vis.width
                h = vis.height
            }
        }
        if w < 32 || h < 32 {
            w = bounds.size.width
            h = bounds.size.height
        }
        return (max(w, 1), max(h, 1))
    }

    /// Call when the **file** (path) changes so zoom/scroll from the previous file are cleared.
    func resetScrollAndMagnification() {
        allowViewportGrowthRefit = true
        clipDiagonalWhenLastFitted = 0
        isProgrammaticMagnification = true
        magnification = 1.0
        isProgrammaticMagnification = false
        contentView.setBoundsOrigin(.zero)
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard window != nil, image != nil else { return }
        if !didInitialFit {
            scheduleDeferredInitialFit()
        }
    }

    override func layout() {
        super.layout()
        applyInitialFitIfNeeded()
    }

    private func applyInitialFitIfNeeded() {
        guard window != nil, image != nil, let d = documentView else { return }
        let (clipW, clipH) = viewportSizeForFit()
        let minClip: CGFloat = 32
        guard clipW >= minClip, clipH >= minClip else { return }

        let diag = hypot(clipW, clipH)
        if didInitialFit, allowViewportGrowthRefit, clipDiagonalWhenLastFitted >= minClip,
           diag > clipDiagonalWhenLastFitted * 1.25 {
            didInitialFit = false
            isProgrammaticMagnification = true
            magnification = 1.0
            isProgrammaticMagnification = false
            contentView.setBoundsOrigin(.zero)
            allowViewportGrowthRefit = false
        }

        if didInitialFit { return }

        let dW = d.bounds.width
        let dH = d.bounds.height
        guard dW >= 1, dH >= 1 else { return }

        let fitW = clipW / dW
        let fitH = clipH / dH
        // Shrink-to-fit when the image is larger than the clip; never upscale past 100% on open (avoids bogus zoom
        // if dimensions are wrong, and matches “see full image at natural resolution or smaller”).
        // 75%: comfortable breathing room; user can pinch/scroll to zoom in further.
        var m = min(fitW, fitH, 1.0) * 0.75
        if m.isNaN || m.isInfinite { m = 0.75 }
        m = min(max(m, minMagnification), maxMagnification)
        isProgrammaticMagnification = true
        setMagnification(m, centeredAt: NSPoint(x: dW * 0.5, y: dH * 0.5))
        isProgrammaticMagnification = false
        didInitialFit = true
        clipDiagonalWhenLastFitted = diag
        publishMagnificationForStatusBar()
    }

    /// Precise (trackpad / Magic Mouse) scroll → pan. Coarse **wheel** (no `hasPreciseScrollingDeltas`) →
    /// zoom. **⌘+scroll** zooms on a trackpad so you can still zoom without a traditional wheel.
    override func scrollWheel(with event: NSEvent) {
        let precise = event.hasPreciseScrollingDeltas
        let wantsZoom: Bool
        if event.modifierFlags.contains(.command) {
            wantsZoom = true
        } else {
            wantsZoom = !precise
        }
        if !wantsZoom {
            super.scrollWheel(with: event)
            return
        }
        guard let d = documentView, window != nil else { return }
        let docPoint = d.convert(event.locationInWindow, from: nil)
        let dW = d.bounds.width
        let dH = d.bounds.height
        if dW < 1 || dH < 1 { return }

        let deltaY = event.scrollingDeltaY
        guard abs(deltaY) > 0.0001 else { return }
        let expFactor = exp(deltaY * 0.01)
        var m = magnification * expFactor
        m = min(max(m, minMagnification), maxMagnification)
        allowViewportGrowthRefit = false
        setMagnification(m, centeredAt: docPoint)
    }

    /// Click-drag to pan (mouse or trackpad click-and-drag). Wheel pan remains trackpad-only; coarse wheels zoom.
    @objc private func handlePanGesture(_ gesture: NSPanGestureRecognizer) {
        guard let clip = contentView as? NSClipView else { return }
        var t = gesture.translation(in: clip)
        gesture.setTranslation(.zero, in: clip)
        let m = max(magnification, 0.0001)
        t.x /= m
        t.y /= m
        var origin = clip.bounds.origin
        origin.x -= t.x
        origin.y -= t.y
        clip.setBoundsOrigin(origin)
        reflectScrolledClipView(clip)
    }
}

// MARK: - Preview loading (thumbnail / bounded decode)

/// Loads editor previews without decoding full-resolution RAWs or huge rasters on the main thread.
///
/// `NSImage(contentsOf:)` develops camera RAW to full size and can hang the UI for seconds. ImageIO
/// thumbnail generation prefers embedded previews and caps the pixel size for the rest.
enum PolymechImagePreviewLoader {
    /// Longest edge for previews; enough for fit-to-editor while keeping memory and decode time bounded.
    static var maxPreviewPixelDimension: Int = 4096

    static func loadPreview(at url: URL) -> NSImage? {
        let srcOpts: [CFString: Any] = [kCGImageSourceShouldCache: false]
        guard let src = CGImageSourceCreateWithURL(url as CFURL, srcOpts as CFDictionary) else {
            return NSImage(contentsOf: url)
        }

        let thumbOpts: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPreviewPixelDimension,
        ]
        if let cg = CGImageSourceCreateThumbnailAtIndex(src, 0, thumbOpts as CFDictionary) {
            let w = CGFloat(cg.width)
            let h = CGFloat(cg.height)
            return NSImage(cgImage: cg, size: NSSize(width: w, height: h))
        }

        return NSImage(contentsOf: url)
    }
}

// MARK: - SwiftUI

/// A view for previewing an image, while respecting its dimensions.
///
/// **Pinch** and **scroll wheel** (or **⌘+scroll** on a trackpad) zoom. **Trackpad 2‑finger** scroll
/// and **click-drag** pan when content is larger than the viewport. Large images are initially scaled to fit the editor.
///
/// If the preview image cannot be created, it shows a *"Cannot preview image"* message.
struct ImageFileView: View {
    private let imageURL: URL
    private let statusBarForImagePreview: StatusBarViewModel?
    @State private var image: NSImage?
    @State private var showError: Bool = false
    /// Bumps on each in-place file reload so an older decode cannot overwrite a newer one.
    @State private var fileReloadEpoch: Int = 0

    init(_ imageURL: URL, statusBarForImagePreview: StatusBarViewModel? = nil) {
        self.imageURL = imageURL
        self.statusBarForImagePreview = statusBarForImagePreview
    }

    var body: some View {
        Group {
            if showError {
                Text("Cannot preview image")
            } else if let image {
                PolymechZoomableImageViewRepresentable(
                    image: image,
                    imageURL: imageURL,
                    statusBar: statusBarForImagePreview
                )
            } else {
                ProgressView()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .task(id: imageURL) {
            image = nil
            showError = false
            let url = imageURL
            do {
                // Nested Task: if this `.task` is cancelled, awaiting `.value` throws `CancellationError`.
                let loaded = try await Task(priority: .userInitiated) {
                    PolymechImagePreviewLoader.loadPreview(at: url)
                }.value
                if Task.isCancelled { return }
                if let loaded {
                    image = loaded
                } else {
                    showError = true
                }
            } catch is CancellationError {
                // Tab closed / file switched while decoding; no state update.
            } catch {
                showError = true
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .polymechImageFileDidWriteInPlace)) { note in
            guard
                let u = note.userInfo?["url"] as? URL,
                u.standardizedFileURL.path == imageURL.standardizedFileURL.path
            else { return }
            Task { @MainActor in
                fileReloadEpoch += 1
                let epoch = fileReloadEpoch
                let url = imageURL
                Task(priority: .userInitiated) {
                    let loaded = PolymechImagePreviewLoader.loadPreview(at: url)
                    await MainActor.run {
                        guard epoch == fileReloadEpoch else { return }
                        if let loaded {
                            image = loaded
                            showError = false
                        } else {
                            showError = true
                        }
                    }
                }
            }
        }
        .onDisappear {
            statusBarForImagePreview?.clearImagePreviewZoomState()
        }
    }
}

struct PolymechZoomableImageViewRepresentable: NSViewRepresentable {
    let image: NSImage
    let imageURL: URL
    let statusBar: StatusBarViewModel?

    final class Provider {
        var path: String = ""
    }

    func makeCoordinator() -> Provider { Provider() }

    func makeNSView(context: Context) -> PolymechZoomableImageScrollView {
        let v = PolymechZoomableImageScrollView()
        v.didInitialFit = false
        context.coordinator.path = imageURL.path
        Self.registerStatusBar(nsView: v, statusBar: statusBar)
        v.image = image
        return v
    }

    func updateNSView(_ nsView: PolymechZoomableImageScrollView, context: Context) {
        let isNewFile = context.coordinator.path != imageURL.path
        if isNewFile {
            context.coordinator.path = imageURL.path
            nsView.resetScrollAndMagnification()
        }
        if isNewFile || nsView.image !== image {
            nsView.image = image
        }
        Self.registerStatusBar(nsView: nsView, statusBar: statusBar)
    }

    /// Host + callback must exist before `image` triggers deferred layout/fit, or the status bar never updates and **Fit** stays disconnected.
    private static func registerStatusBar(nsView: PolymechZoomableImageScrollView, statusBar: StatusBarViewModel?) {
        if let s = statusBar {
            s.imagePreviewZoomHost = nsView
            nsView.onMagnificationForStatusBar = { [weak s] pct in
                guard let s else { return }
                s.imagePreviewMagnification = pct
            }
            s.imagePreviewMagnification = nsView.zoomPercentRelativeToFit()
        } else {
            nsView.onMagnificationForStatusBar = nil
        }
    }
}
