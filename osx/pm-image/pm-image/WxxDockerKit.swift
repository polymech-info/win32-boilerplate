//
//  WxxDockerKit.swift
//  pm-image
//
//  **Deployment target: macOS 15.2** (set in Xcode project)
//
//  Win32++-aligned AppKit docking: `CDockFrame` / `CDocker` concepts mapped to `WxxMainDockWindow` + `WxxDockHost`
//  (see `docs/osx-dockers.md`). The previous `DockingAppKit.swift` monolith was a prototype; this file is the
//  active implementation until the tree is split into `WxxDock/*.swift`.
//
//  **docked** panes follow splitter **frame**; **inner** content (e.g. log) scrolls inside the docker view.
//  Layout preset + UserDefaults persistence (`WxxDock.*` keys). v1: one panel per container, no tabs.
//
//  Stack: AppKit, NSSplitViewController, `NSPanel` floats, NSViewController — no SwiftUI UI.
//

import AppKit
import Foundation

// MARK: - VC reparenting (call at most once per move)

/// Removes the view from its superview and the controller from `parent` if needed — one consistent path
/// (avoids double-`removeFromParent` / mixed ordering when moving between `DockContainer` and `FloatPanelHost`).
fileprivate func detachViewControllerForReparent(_ vc: NSViewController) {
    vc.loadViewIfNeeded()
    vc.view.removeFromSuperview()
    if vc.parent != nil { vc.removeFromParent() }
}

// MARK: - Split size bounds (docked chrome only; see Win32++ `wxx_docking.h` — child content scrolls inside the docker view)

/// Reasonable `NSSplitViewItem` min/max *thickness* so a docked panel never demands an unbounded min size from the main frame.
/// Sums of vertical mins (root top + bottom) must stay **below** a typical `minSize.height` (e.g. 400) or the split becomes over-constrained and drags/resize feel “stuck.”
enum DockingSplitSizeBounds {
    static let leftColumn: ClosedRange<CGFloat> = 100...600
    static let rightPane: ClosedRange<CGFloat> = 100...900
    static let bottomStrip: ClosedRange<CGFloat> = 48...800
    static let leftRow: ClosedRange<CGFloat> = 64...400
    /// Root top row: [ left | center | right ]. **Keep this min modest** so `top + bottom` vertical minimums do not exceed the window.
    static let mainContentStripMinHeight: CGFloat = 100
    static let mainContentStripMaxHeight: CGFloat = 20_000
}

// MARK: - In-app log (Log panel + optional console echo)

/// Append-only log buffer for the **Log** demo panel. Main-thread use only.
enum InAppLog {
    static let didChange = Notification.Name("InAppLog.didChange")
    private static var lines: [String] = []
    private static let maxLines = 1_000

    static var allText: String { lines.joined(separator: "\n") }

    static func line(_ message: String) {
        let ts = Self.timeFormatter.string(from: Date())
        lines.append("[\(ts)] \(message)")
        if lines.count > maxLines { lines.removeFirst(lines.count - maxLines) }
        // Post async so clients (e.g. Log panel `NSTextView`) never run in the same call stack
        // as `WxxDockHost` / `embed` view reparenting — that re-entrancy can crash AppKit.
        scheduleNotify()
    }

    static func clear() {
        lines.removeAll()
        scheduleNotify()
    }

    private static func scheduleNotify() {
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: didChange, object: nil)
        }
    }

    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.locale = .current
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
}

/// After `contentViewController` is set, sizes the window and moves it into `NSScreen.main`’s visibleFrame (not under menu bar / off-screen).
enum PMWindowPlace {
    static func sizeAndCenterInVisibleFrame(_ window: NSWindow, contentSize: NSSize) {
        window.setContentSize(contentSize)
        window.layoutIfNeeded()
        var frame = window.frame
        guard let screen = NSScreen.main else {
            window.center()
            return
        }
        let vf = screen.visibleFrame
        if frame.width > vf.width - 40 { frame.size.width = vf.width - 40 }
        if frame.height > vf.height - 40 { frame.size.height = vf.height - 40 }
        var x = vf.minX + (vf.width - frame.width) * 0.5
        var y = vf.minY + (vf.height - frame.height) * 0.5
        x = min(max(x, vf.minX), vf.maxX - frame.width)
        y = min(max(y, vf.minY), vf.maxY - frame.height)
        frame.origin = NSPoint(x: x, y: y)
        window.setFrame(frame, display: true)
    }
}

// MARK: - Model

/// Left workbench = three stacked rows (top = files, mid = inspector, lower = metadata in the `browser` guideline).
enum DockPosition: String, Codable, CaseIterable {
    case leftTop
    case leftMid
    case leftBottom
    case right
    case bottom
    case floating
    case hidden
}

enum PanelID: String, Codable, CaseIterable {
    case files
    case inspector
    case chat
    case metadata
    case log
}

struct WorkspaceLayout: Codable, Equatable {
    var name: String
    var panels: [PanelLayout]
}

struct PanelLayout: Codable, Equatable {
    var panel: PanelID
    var position: DockPosition
    var width: CGFloat?
    var height: CGFloat?
    var floatingFrame: CGRect?

    private enum Keys: String, CodingKey { case panel, position, width, height, fx, fy, fw, fh }

    init(panel: PanelID, position: DockPosition, width: CGFloat?, height: CGFloat?, floatingFrame: CGRect?) {
        self.panel = panel
        self.position = position
        self.width = width
        self.height = height
        self.floatingFrame = floatingFrame
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: Keys.self)
        panel = try c.decode(PanelID.self, forKey: .panel)
        let posStr = (try? c.decode(String.self, forKey: .position)) ?? "hidden"
        position = Self.decodePosition(posStr)
        width = try c.decodeIfPresent(CGFloat.self, forKey: .width)
        height = try c.decodeIfPresent(CGFloat.self, forKey: .height)
        if
            let fx = try c.decodeIfPresent(CGFloat.self, forKey: .fx),
            let fy = try c.decodeIfPresent(CGFloat.self, forKey: .fy),
            let fw = try c.decodeIfPresent(CGFloat.self, forKey: .fw),
            let fh = try c.decodeIfPresent(CGFloat.self, forKey: .fh)
        {
            floatingFrame = CGRect(x: fx, y: fy, width: fw, height: fh)
        } else {
            floatingFrame = nil
        }
    }

    private static func decodePosition(_ s: String) -> DockPosition {
        switch s {
        case "left", "leftTop": return .leftTop
        case "leftMid": return .leftMid
        case "leftBottom": return .leftBottom
        case "right": return .right
        case "bottom": return .bottom
        case "floating": return .floating
        case "hidden": return .hidden
        default: return .hidden
        }
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: Keys.self)
        try c.encode(panel, forKey: .panel)
        try c.encode(position.rawValue, forKey: .position)
        try c.encodeIfPresent(width, forKey: .width)
        try c.encodeIfPresent(height, forKey: .height)
        if let r = floatingFrame {
            try c.encode(r.minX, forKey: .fx)
            try c.encode(r.minY, forKey: .fy)
            try c.encode(r.width, forKey: .fw)
            try c.encode(r.height, forKey: .fh)
        }
    }
}

// MARK: - Presets (spec)

extension WorkspaceLayout {
    /// Guideline: Files above Inspector above Metadata in the left column; other panels off by default.
    static let browser = WorkspaceLayout(
        name: "browser",
        panels: [
            .init(panel: .files, position: .leftTop, width: 240, height: 200, floatingFrame: nil),
            .init(panel: .inspector, position: .leftMid, width: nil, height: 200, floatingFrame: nil),
            .init(panel: .metadata, position: .leftBottom, width: nil, height: 200, floatingFrame: nil),
            .init(panel: .log, position: .right, width: 300, height: nil, floatingFrame: nil),
            .init(panel: .chat, position: .hidden, width: nil, height: nil, floatingFrame: nil),
        ]
    )
}

// MARK: - DockPanel

final class DockPanel: NSObject {
    let id: PanelID
    let title: String
    let viewController: NSViewController
    var position: DockPosition = .hidden
    /// Tool float is an `NSPanel` whose `contentViewController` is a `FloatPanelHostViewController`
    /// that `addChild`s the panel (the panel is never the window’s direct `contentViewController`).
    var floatingPanel: NSPanel?

    init(id: PanelID, title: String, viewController: NSViewController) {
        self.id = id
        self.title = title
        self.viewController = viewController
        super.init()
    }
}

// MARK: - Containers & center

final class DockContainerViewController: NSViewController {
    private(set) var hosted: NSViewController?
    private var childConstraints: [NSLayoutConstraint] = []

    override func loadView() {
        let v = NSView()
        v.wantsLayer = true
        v.layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
        v.setContentHuggingPriority(.init(1), for: .vertical)
        v.setContentHuggingPriority(.init(1), for: .horizontal)
        v.setContentCompressionResistancePriority(.init(1), for: .vertical)
        v.setContentCompressionResistancePriority(.init(1), for: .horizontal)
        view = v
    }

    func embed(_ child: NSViewController?) {
        if !childConstraints.isEmpty {
            NSLayoutConstraint.deactivate(childConstraints)
            childConstraints.removeAll()
        }
        if let h = hosted { detachViewControllerForReparent(h) }
        hosted = child
        guard let c = child else {
            view.needsLayout = true
            return
        }
        detachViewControllerForReparent(c)
        addChild(c)
        view.addSubview(c.view)
        c.view.translatesAutoresizingMaskIntoConstraints = false
        let cstr = [
            c.view.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            c.view.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            c.view.topAnchor.constraint(equalTo: view.topAnchor),
            c.view.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ]
        childConstraints = cstr
        NSLayoutConstraint.activate(cstr)
        view.needsLayout = true
        view.layoutSubtreeIfNeeded()
    }
}

/// The float **`NSWindow`’s** `contentViewController`. It `addChild`s the tool panel so the
/// **window** never hosts that `NSViewController` directly. Dock/split uses another parent
/// (`DockContainerViewController`). Tearing the panel out of the host, then re-embedding, follows
/// normal `removeFromParent` / `addChild` rules instead of fighting window–VC ownership.
private final class FloatPanelHostViewController: NSViewController {
    private(set) var childPanel: NSViewController?

    override func loadView() {
        let v = NSView()
        v.wantsLayer = true
        v.layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
        view = v
    }

    func setChildPanel(_ vc: NSViewController) {
        if let c = childPanel { detachViewControllerForReparent(c) }
        childPanel = vc
        detachViewControllerForReparent(vc)
        addChild(vc)
        let u = vc.view
        u.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(u)
        NSLayoutConstraint.activate([
            u.topAnchor.constraint(equalTo: view.topAnchor),
            u.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            u.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            u.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        view.needsLayout = true
        view.layoutSubtreeIfNeeded()
    }

    /// Detach the tool panel for reparenting into a dock slot; window will clear `contentViewController` after.
    func evictChildPanel() {
        guard let c = childPanel else { return }
        detachViewControllerForReparent(c)
        childPanel = nil
    }
}

final class CenterContentViewController: NSViewController {
    override func loadView() {
        let v = NSView()
        v.wantsLayer = true
        v.layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor
        v.setContentHuggingPriority(.init(1), for: .vertical)
        v.setContentHuggingPriority(.init(1), for: .horizontal)
        v.setContentCompressionResistancePriority(.init(1), for: .vertical)
        v.setContentCompressionResistancePriority(.init(1), for: .horizontal)
        v.setAccessibilityIdentifier("pm-dock-center")
        let t = NSTextField(labelWithString: "Center content (document / canvas area)")
        t.alignment = .center
        t.textColor = .labelColor
        t.translatesAutoresizingMaskIntoConstraints = false
        v.addSubview(t)
        NSLayoutConstraint.activate([
            t.centerXAnchor.constraint(equalTo: v.centerXAnchor),
            t.centerYAnchor.constraint(equalTo: v.centerYAnchor),
        ])
        view = v
    }
}

// MARK: - VS Code–style redock (single + cross; no overlapping edge bands)

/// Which direction the cursor picked on the cross (in main window’s content area, screen coords).
enum DockRedockHintTarget: Equatable {
    case left
    case right
    case bottom
    case center
    case outside
}

private final class PassThroughRootView: NSView {
    override func layout() {
        super.layout()
        for s in subviews { s.frame = bounds }
    }
    override func hitTest(_: NSPoint) -> NSView? { nil }
}

private enum RedockWedge: Equatable { case north, east, south, west, center }

/// One familiar 4-arm + center reticle (VS Code–style). Flipped coordinates.
/// Must not sit under a layer-backed superview, or `draw` may not render in a transparent helper window.
private final class RedockCrossView: NSView {
    var activeWedge: RedockWedge?
    private let centerR: CGFloat = 22

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { false }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = false
    }

    @available(*, unavailable) required init?(coder: NSCoder) { nil }

    override func draw(_ dirtyRect: NSRect) {
        let w = bounds.width, h = bounds.height
        guard w > 4, h > 4 else { return }
        let cx = w * 0.5, cy = h * 0.5
        let armL: CGFloat = (min(w, h) * 0.5) - centerR - 6
        let armW: CGFloat = 36
        func fillArm(_ r: NSRect, _ wedge: RedockWedge) {
            let on = activeWedge == wedge
            (on ? NSColor.controlAccentColor.withAlphaComponent(0.6) : NSColor(white: 0.22, alpha: 0.5)).setFill()
            let p = NSBezierPath(roundedRect: r, xRadius: 6, yRadius: 6)
            p.fill()
            if on {
                NSColor.white.withAlphaComponent(0.95).setStroke()
                p.lineWidth = 2.2
            } else {
                NSColor.separatorColor.withAlphaComponent(0.55).setStroke()
                p.lineWidth = 1.1
            }
            p.stroke()
        }
        // N
        fillArm(
            NSRect(x: cx - armW * 0.5, y: cy - centerR - armL, width: armW, height: armL),
            .north
        )
        // S
        fillArm(
            NSRect(x: cx - armW * 0.5, y: cy + centerR, width: armW, height: armL),
            .south
        )
        // W
        fillArm(
            NSRect(x: cx - centerR - armL, y: cy - armW * 0.5, width: armL, height: armW),
            .west
        )
        // E
        fillArm(
            NSRect(x: cx + centerR, y: cy - armW * 0.5, width: armL, height: armW),
            .east
        )
        // Center
        let cr = NSRect(x: cx - centerR, y: cy - centerR, width: centerR * 2, height: centerR * 2)
        (activeWedge == .center ? NSColor.controlAccentColor.withAlphaComponent(0.5) : NSColor(white: 0.28, alpha: 0.5)).setFill()
        let cpO = NSBezierPath(ovalIn: cr)
        cpO.fill()
        if activeWedge == .center {
            NSColor.white.withAlphaComponent(0.95).setStroke()
        } else {
            NSColor.separatorColor.withAlphaComponent(0.65).setStroke()
        }
        cpO.lineWidth = activeWedge == .center ? 2.2 : 1.1
        cpO.stroke()
        let s = min(20, max(12, armL * 0.28))
        let chev: [(RedockWedge, String, NSRect)] = [
            (.north, "chevron.compact.up", NSRect(x: cx - s * 0.5, y: cy - centerR - armL * 0.5 - s * 0.5, width: s, height: s)),
            (.south, "chevron.compact.down", NSRect(x: cx - s * 0.5, y: cy + centerR + armL * 0.5 - s * 0.5, width: s, height: s)),
            (.west, "chevron.compact.left", NSRect(x: cx - centerR - armL * 0.5 - s * 0.5, y: cy - s * 0.5, width: s, height: s)),
            (.east, "chevron.compact.right", NSRect(x: cx + centerR + armL * 0.5 - s * 0.5, y: cy - s * 0.5, width: s, height: s)),
        ]
        for (wedge, name, fr) in chev {
            if let i = NSImage(systemSymbolName: name, accessibilityDescription: nil) {
                i.isTemplate = true
                (activeWedge == wedge ? NSColor.white : NSColor(white: 0.9, alpha: 0.75)).set()
                i.draw(in: fr, from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
            }
        }
        if let i = NSImage(systemSymbolName: "xmark", accessibilityDescription: nil) {
            i.isTemplate = true
            let sz: CGFloat = 16
            let r = NSRect(x: cx - sz * 0.5, y: cy - sz * 0.5, width: sz, height: sz)
            (activeWedge == .center ? NSColor.white : NSColor(white: 0.88, alpha: 0.7)).set()
            i.draw(in: r, from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
        }
    }

    /// Local point, same flipped space as `bounds`.
    func wedge(at local: NSPoint) -> RedockWedge? {
        let w = bounds.width, h = bounds.height
        guard w > 4, h > 4 else { return nil }
        let cx = w * 0.5, cy = h * 0.5
        let dx = local.x - cx, dy = local.y - cy
        if hypot(dx, dy) < centerR { return .center }
        if abs(dy) >= abs(dx) {
            return dy < 0 ? .north : .south
        } else {
            return dx < 0 ? .west : .east
        }
    }
}

/// Dim overlay + one **cross** (anchored in the **left column** when the pointer is over the sidebar, else workbench center).
private final class RedockHintGridView: NSView {
    weak var wxxDockHost: WxxDockHost?
    private let cross = RedockCrossView(frame: .zero)
    var lastPointerScreen: NSPoint = .zero
    /// `true` when the cursor (during redock) is over the 3-row sidebar; drives bottom-half **preview** and S→`leftMid` **drop**).
    private(set) var isPointerInLeftColumn = false
    var crossFrameInLocal: NSRect = .zero

    override var isFlipped: Bool { true }
    /// Opacity is drawn in `draw(_:)`; keep non–layer-backed so the cross subview can draw in a clear `NSWindow`.
    override var isOpaque: Bool { false }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = false
        addSubview(cross)
    }

    @available(*, unavailable) required init?(coder: NSCoder) { nil }

    override func draw(_ dirtyRect: NSRect) {
        NSColor(white: 0, alpha: 0.2).setFill()
        NSBezierPath(rect: bounds).fill()
        if let pr = dropPreviewRectInLocal(), pr.width > 1.5, pr.height > 1.5 {
            NSColor.controlAccentColor.withAlphaComponent(0.3).setFill()
            let p = NSBezierPath(roundedRect: pr, xRadius: 2, yRadius: 2)
            p.fill()
            NSColor.white.withAlphaComponent(0.2).setStroke()
            p.lineWidth = 1.5
            p.stroke()
        }
    }

    /// Visual Studio–style: semi-transparent “ghost” of where the panel will land (e.g. **bottom half** of a column when the ↓ target is hot).
    private func dropPreviewRectInLocal() -> NSRect? {
        guard let wv = cross.activeWedge, let d = wxxDockHost, window != nil else { return nil }
        switch wv {
        case .north, .center:
            return nil
        case .east:
            if let r = d.rightDockScreenRect(), let local = localRectFromScreen(r) { return local }
            return halvedFromBounds(horizontal: .trailing)
        case .west:
            if let l = d.leftColumnScreenRect() {
                let inLeft = lastPointerScreen != .zero && NSPointInRect(lastPointerScreen, l)
                if let local = localRectFromScreen(l) {
                    if inLeft {
                        return NSRect(x: local.minX, y: local.minY, width: local.width * 0.5, height: local.height)
                    }
                    return local
                }
            }
            return NSRect(x: 0, y: 0, width: min(bounds.width * 0.28, 200), height: bounds.height)
        case .south:
            if let l = d.leftColumnScreenRect() {
                let inLeft = lastPointerScreen != .zero && NSPointInRect(lastPointerScreen, l)
                if inLeft, let col = localRectFromScreen(l) {
                    let h2 = col.height * 0.5
                    return NSRect(x: col.minX, y: col.minY + h2, width: col.width, height: h2)
                }
            }
            if let b = d.bottomDockScreenRect(), let local = localRectFromScreen(b) { return local }
            return bottomHalfWorkbenchPreview()
        }
    }

    private func halvedFromBounds(horizontal: HSide) -> NSRect {
        let w = bounds.width, h = bounds.height
        let w2 = w * 0.5
        switch horizontal {
        case .leading: return NSRect(x: 0, y: 0, width: w2, height: h)
        case .trailing: return NSRect(x: w2, y: 0, width: w2, height: h)
        }
    }

    private func bottomHalfWorkbenchPreview() -> NSRect {
        let w = bounds.width, h = bounds.height
        let h2 = h * 0.5
        return NSRect(x: 0, y: h2, width: w, height: h2)
    }

    private enum HSide { case leading, trailing }

    override func layout() {
        super.layout()
        let w = bounds.width, h = bounds.height
        let size = min(200, min(w, h) * 0.45)
        let r: NSRect
        if
            let d = wxxDockHost,
            let l = d.leftColumnScreenRect(),
            lastPointerScreen != .zero,
            NSPointInRect(lastPointerScreen, l),
            let localL = localRectFromScreen(l)
        {
            r = NSRect(
                x: localL.midX - size * 0.5,
                y: localL.midY - size * 0.5,
                width: size,
                height: size
            )
        } else {
            r = NSRect(
                x: (w - size) * 0.5,
                y: (h - size) * 0.5,
                width: size,
                height: size
            )
        }
        cross.frame = r
        crossFrameInLocal = r
        needsDisplay = true
        cross.needsDisplay = true
    }

    private func localRectFromScreen(_ rScreen: NSRect) -> NSRect? {
        guard let wn = window else { return nil }
        return convert(wn.convertFromScreen(rScreen), from: nil)
    }

    private static func mapWedge(_ w: RedockWedge) -> DockRedockHintTarget {
        switch w {
        case .north, .center: return .center
        case .east: return .right
        case .south: return .bottom
        case .west: return .left
        }
    }

    func target(atScreen m: NSPoint) -> DockRedockHintTarget {
        layout()
        guard let wn = window else { return .outside }
        let lp = convert(wn.convertPoint(fromScreen: m), from: nil)
        if !NSPointInRect(lp, crossFrameInLocal) {
            if NSPointInRect(m, wn.frame) { return .center }
            return .outside
        }
        let cl = NSPoint(
            x: lp.x - crossFrameInLocal.minX,
            y: lp.y - crossFrameInLocal.minY
        )
        guard let wv = cross.wedge(at: cl) else { return .center }
        return Self.mapWedge(wv)
    }

    func setActive(_ t: DockRedockHintTarget) {
        switch t {
        case .left: cross.activeWedge = .west
        case .right: cross.activeWedge = .east
        case .bottom: cross.activeWedge = .south
        case .center: cross.activeWedge = .center
        case .outside: cross.activeWedge = nil
        }
        cross.needsDisplay = true
    }

    func updateHighlight(atScreen p: NSPoint) {
        lastPointerScreen = p
        if let d = wxxDockHost, let l = d.leftColumnScreenRect() {
            isPointerInLeftColumn = NSPointInRect(p, l)
        } else {
            isPointerInLeftColumn = false
        }
        layout()
        guard let wn = window else { return }
        let lp = convert(wn.convertPoint(fromScreen: p), from: nil)
        if !NSPointInRect(lp, crossFrameInLocal) {
            cross.activeWedge = nil
        } else {
            let cl = NSPoint(x: lp.x - crossFrameInLocal.minX, y: lp.y - crossFrameInLocal.minY)
            cross.activeWedge = cross.wedge(at: cl)
        }
        cross.needsDisplay = true
        needsDisplay = true
    }

    func resolveDrop(atScreen p: NSPoint) -> DockRedockHintTarget {
        updateHighlight(atScreen: p)
        return target(atScreen: p)
    }
}

// MARK: - Titlebar drag (Win32/MDI-style undock + float move)

/// Drag past a few points on the title strip to **tear** the panel out to an `NSPanel`, or — when already floating — move the panel.
private let kTitlebarUndockSlop: CGFloat = 4

/// `title` is the panel name (also shown in the system titlebar when floating).
final class PanelTitlebarDragView: NSView {
    let panelID: PanelID
    weak var wxxDockHost: WxxDockHost?
    private let label: NSTextField
    private var mouseDownScreen = NSPoint.zero
    private var startedDocked = false
    private var grabFromTitlebarMin = NSPoint.zero
    private var hasTornFromDock = false

    init(panelID: PanelID, title: String) {
        self.panelID = panelID
        self.label = NSTextField(labelWithString: title)
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.separatorColor.withAlphaComponent(0.45).cgColor
        label.font = NSFont.systemFont(ofSize: 13, weight: .semibold)
        label.translatesAutoresizingMaskIntoConstraints = false
        // Expose the drag surface as a single a11y node (identifier on `self`); else `NSTextField` wins and XCUITest never sees `pm-titlebar-*`.
        label.setAccessibilityElement(false)
        addSubview(label)
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            label.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -4),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
        setAccessibilityElement(true)
        setAccessibilityLabel("\(title) (drag panel)")
        setAccessibilityIdentifier("pm-titlebar-\(panelID.rawValue)")
    }

    deinit { stopTitlebarSession() }

    @available(*, unavailable) required init?(coder: NSCoder) { nil }

    override var intrinsicContentSize: NSSize { NSSize(width: 120, height: 28) }

    private func titlebarScreenFrame() -> NSRect? {
        guard let w = window, let content = w.contentView else { return nil }
        let tIn = convert(bounds, to: content)
        return w.convertToScreen(tIn)
    }

    private func recomputeGrab() {
        guard let s = titlebarScreenFrame() else { return }
        let m = NSEvent.mouseLocation
        grabFromTitlebarMin = NSPoint(x: m.x - s.minX, y: m.y - s.minY)
    }

    override func resetCursorRects() {
        addCursorRect(bounds, cursor: .openHand)
    }

    private func stopTitlebarSession() {
        hasTornFromDock = false
        wxxDockHost?.setRedockHintVisible(false)
    }

    /// Drag / drop uses an **event tracking loop** (`NSApp.nextEvent`) instead of
    /// `NSEvent.addLocalMonitorForEvents`. The local monitor re-enters AppKit for every
    /// mouse event and has been implicated in `objc_release` crashes during
    /// `_CFAutoreleasePoolPop` on the main run loop in this flow.
    override func mouseDown(with event: NSEvent) {
        mouseDownScreen = NSEvent.mouseLocation
        recomputeGrab()
        startedDocked = wxxDockHost?.isInDockedSplit(panelID) == true
        hasTornFromDock = false
        stopTitlebarSession()
        if !startedDocked { wxxDockHost?.setRedockHintVisible(true) }

        let mask: NSEvent.EventTypeMask = [.leftMouseDragged, .leftMouseUp]
        // Use the app’s queue so we still receive drags after `detach` moves this view
        // to a new key window.
        while true {
            let next = NSApp.nextEvent(matching: mask, until: .distantFuture, inMode: .eventTracking, dequeue: true)
            guard let e = next else { break }
            guard let d = wxxDockHost else { break }

            if e.type == .leftMouseUp {
                hasTornFromDock = false
                let p = NSEvent.mouseLocation
                d.tryRedockAfterTitlebarDrop(panelID: panelID, atScreen: p)
                break
            }
            if e.type != .leftMouseDragged { continue }

            let p = NSEvent.mouseLocation
            if startedDocked, d.isInDockedSplit(panelID) {
                let dist = hypot(p.x - mouseDownScreen.x, p.y - mouseDownScreen.y)
                if dist > kTitlebarUndockSlop, !hasTornFromDock {
                    hasTornFromDock = true
                    // Do not reparent inside `nextEvent` tracking — defer so AppKit finishes the current event/callout first.
                    let pid = panelID
                    DispatchQueue.main.async { [weak d] in
                        d?.detach(pid)
                    }
                    recomputeGrab()
                    d.setRedockHintVisible(true)
                }
            }
            if let w = d.floatingNSWindow(panelID) {
                d.positionFloatingWindow(w, titleView: self, mouse: p, grabFromTitlebarMin: grabFromTitlebarMin)
            }
            d.updateRedockHintPointer(atScreen: p)
        }
    }
}

// MARK: - Demo panel

final class DemoPanelViewController: NSViewController {
    let panelID: PanelID
    weak var wxxDockHost: WxxDockHost?
    private let positionLabel: NSTextField
    private let titlebar: PanelTitlebarDragView
    private var logTextView: NSTextView?
    private var inAppLogObserver: NSObjectProtocol?

    init(panelID: PanelID, title: String) {
        self.panelID = panelID
        self.titlebar = PanelTitlebarDragView(panelID: panelID, title: title)
        self.positionLabel = NSTextField(wrappingLabelWithString: "—")
        super.init(nibName: nil, bundle: nil)
        self.title = title
    }

    deinit {
        if let o = inAppLogObserver { NotificationCenter.default.removeObserver(o) }
    }

    @available(*, unavailable) required init?(coder: NSCoder) { nil }

    func updatePositionText(_ t: String) { positionLabel.stringValue = t }

    override func loadView() {
        let root = NSView()
        root.wantsLayer = true
        root.layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
        root.setContentHuggingPriority(.init(1), for: .vertical)
        root.setContentHuggingPriority(.init(1), for: .horizontal)
        root.setContentCompressionResistancePriority(.init(1), for: .vertical)
        root.setContentCompressionResistancePriority(.init(1), for: .horizontal)
        titlebar.wxxDockHost = wxxDockHost
        titlebar.translatesAutoresizingMaskIntoConstraints = false
        if panelID == .log {
            inAppLogObserver = NotificationCenter.default.addObserver(
                forName: InAppLog.didChange,
                object: nil,
                queue: .main
            ) { [weak self] _ in
                self?.applyInAppLogToTextViewDeferred()
            }
            // Scroll the log: intrinsic height of wrapping `NSTextField` or huge `intrinsic` from a mis-sized text
            // view would otherwise fight vertical window resize (analog: Win32 `CDockContainer` frame vs. inner `CEdit` scroll).
            let scroll = NSScrollView()
            scroll.hasVerticalScroller = true
            scroll.hasHorizontalScroller = false
            scroll.autohidesScrollers = true
            scroll.borderType = .bezelBorder
            scroll.translatesAutoresizingMaskIntoConstraints = false
            scroll.drawsBackground = true
            scroll.backgroundColor = .textBackgroundColor
            // Let the bottom spring absorb resize; the log area fills but does not dictate window height.
            scroll.setContentHuggingPriority(.init(1), for: .vertical)
            scroll.setContentCompressionResistancePriority(.init(1), for: .vertical)
            let tv = NSTextView()
            tv.isRichText = false
            tv.isEditable = false
            tv.isSelectable = true
            tv.drawsBackground = true
            tv.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
            tv.backgroundColor = .textBackgroundColor
            tv.textColor = .labelColor
            tv.isVerticallyResizable = true
            tv.isHorizontallyResizable = false
            tv.textContainer?.widthTracksTextView = true
            tv.textContainer?.heightTracksTextView = false
            tv.textContainer?.containerSize = NSSize(width: 0, height: CGFloat(1e7))
            tv.minSize = .zero
            tv.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
            tv.autoresizingMask = [.width]
            tv.setContentHuggingPriority(.init(1), for: .vertical)
            scroll.documentView = tv
            self.logTextView = tv
            let logRegion = scroll
            let stack = NSStackView( views: [ titlebar, positionLabel, logRegion, makeButtonRow(), NSView()])
            stack.orientation = .vertical
            stack.alignment = .leading
            stack.spacing = 8
            stack.setCustomSpacing(8, after: titlebar)
            stack.setCustomSpacing(8, after: positionLabel)
            stack.setCustomSpacing(8, after: logRegion)
            stack.translatesAutoresizingMaskIntoConstraints = false
            root.addSubview( stack)
            NSLayoutConstraint.activate([
                stack.topAnchor.constraint( equalTo: root.topAnchor),
                stack.leadingAnchor.constraint( equalTo: root.leadingAnchor),
                stack.trailingAnchor.constraint( equalTo: root.trailingAnchor),
                stack.bottomAnchor.constraint( equalTo: root.bottomAnchor),
            ])
            NSLayoutConstraint.activate([
                titlebar.leadingAnchor.constraint( equalTo: stack.leadingAnchor),
                titlebar.trailingAnchor.constraint( equalTo: stack.trailingAnchor),
            ])
            NSLayoutConstraint.activate([
                logRegion.heightAnchor.constraint(greaterThanOrEqualToConstant: 64),
            ])
            applyInAppLogToTextViewDeferred()
        } else {
            let stack = NSStackView(views: [
                titlebar, positionLabel, makeButtonRow(), NSView() // spring
            ])
            stack.orientation = .vertical
            stack.alignment = .leading
            stack.spacing = 8
            stack.setCustomSpacing(8, after: titlebar)
            stack.setCustomSpacing(12, after: positionLabel)
            stack.translatesAutoresizingMaskIntoConstraints = false
            root.addSubview(stack)
            let bottomPin = stack.bottomAnchor.constraint(equalTo: root.bottomAnchor)
            bottomPin.priority = .init(200)
            NSLayoutConstraint.activate([
                stack.topAnchor.constraint(equalTo: root.topAnchor, constant: 0),
                stack.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 0),
                stack.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: 0),
                bottomPin,
            ])
            NSLayoutConstraint.activate([
                titlebar.leadingAnchor.constraint(equalTo: stack.leadingAnchor),
                titlebar.trailingAnchor.constraint(equalTo: stack.trailingAnchor),
            ])
        }
        view = root
    }

    private func applyInAppLogToTextViewDeferred() {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            if self.wxxDockHost?.isLayoutTransactionActive == true { return }
            if self.wxxDockHost?.isReparentingActive == true { return }
            if let tv = self.logTextView, tv.window != nil {
                tv.string = InAppLog.allText
                let len = (tv.string as NSString).length
                if len > 0 { tv.setSelectedRange(NSRange(location: len, length: 0)) }
                tv.scrollToEndOfDocument(nil)
            }
        }
    }

    @objc private func clearInAppLog() {
        InAppLog.clear()
    }

    private func makeButtonRow() -> NSView {
        let row1 = NSStackView(views: [btn("Dock Left", #selector(dockLeft)), btn("Dock Right", #selector(dockRight)), btn("Dock Bottom", #selector(dockBottom))])
        row1.orientation = .horizontal
        row1.spacing = 4
        let floatHide: [NSView] = {
            if panelID == .log { return [ btn("Float", #selector(floatPanel)), btn("Hide", #selector(hidePanel)), btn("Clear log", #selector( clearInAppLog))] }
            return [ btn("Float", #selector(floatPanel)), btn("Hide", #selector(hidePanel))]
        }()
        let row2 = NSStackView( views: floatHide)
        row2.orientation = .horizontal
        row2.spacing = 4
        return NSStackView(views: [row1, row2])
    }

    private func btn(_ s: String, _ sel: Selector) -> NSButton {
        let b = NSButton(title: s, target: self, action: sel)
        b.bezelStyle = .rounded
        b.setButtonType(.momentaryPushIn)
        return b
    }

    @objc private func dockLeft() {
        guard let d = wxxDockHost else { return }
        d.attach(panelID, to: Self.preferredLeftSlot(panelID))
    }

    private static func preferredLeftSlot(_ id: PanelID) -> DockPosition {
        switch id {
        case .files: return .leftTop
        case .inspector: return .leftMid
        case .metadata: return .leftBottom
        default: return .leftTop
        }
    }
    @objc private func dockRight() { wxxDockHost?.attach(panelID, to: .right) }
    @objc private func dockBottom() { wxxDockHost?.attach(panelID, to: .bottom) }
    @objc private func floatPanel() { wxxDockHost?.detach(panelID) }
    @objc private func hidePanel() {
        InAppLog.line("Hide button: \(panelID.rawValue)")
        wxxDockHost?.hide(panelID)
    }
}

// MARK: - WxxDockHost (Win32++ `CDockFrame` / `CDocker` tree — AppKit `NSSplitViewController`)

/// Owns the split tree: root split (top | bottom) where top = [ left **column** (3 high) | center | right ].
final class WxxDockHost: NSObject, NSWindowDelegate {
    private enum UD {
        static let last = "WxxDock.lastLayoutJSON"
        static let name = "WxxDock.lastLayoutName"
    }

    let splitController: NSSplitViewController
    private let leftTopC = DockContainerViewController()
    private let leftMidC = DockContainerViewController()
    private let leftBotC = DockContainerViewController()
    private let leftColumnVC: NSSplitViewController
    private let leftTopItem: NSSplitViewItem
    private let leftMidItem: NSSplitViewItem
    private let leftBotItem: NSSplitViewItem
    private let leftColItem: NSSplitViewItem
    private let rightC = DockContainerViewController()
    private let bottomC = DockContainerViewController()
    private let centerVC = CenterContentViewController()

    private let rightItem: NSSplitViewItem
    private let bottomItem: NSSplitViewItem
    /// Root’s top item: the wide strip containing [ left | center | right ] — this row’s *thickness* is **height**; a tight max here breaks vertical window resize.
    private let topStripItem: NSSplitViewItem
    /// Middle strip SVC: [ left column | center | right ] (horizontal dividers between columns).
    private let middleRowSplit: NSSplitViewController

    private var panelById: [PanelID: DockPanel] = [:]
    weak var window: NSWindow?

    private var layoutTransactionDepth = 0
    var isLayoutTransactionActive: Bool { layoutTransactionDepth > 0 }
    private var reparentingDepth = 0
    /// True while `attach` / `detach` / `unmountPanelState` are mutating the VC tree (skip log UI, ignore redundant callbacks).
    var isReparentingActive: Bool { reparentingDepth > 0 }
    private let kFloatFrameAutosave = false

    private func logDock(_ s: String) { InAppLog.line(s) }

    override init() {
        let leftCol = NSSplitViewController()
        // Three rows: files (top) | inspector (mid) | metadata (lower); horizontal dividers between rows.
        leftCol.splitView.isVertical = false
        leftCol.splitView.dividerStyle = .paneSplitter
        // min < max and generous max so drags **resize** panes. Collapsing is still `canCollapse: true` + dimple double-click.
        self.leftTopItem = Self.makeContainerItem(
            self.leftTopC, canCollapse: true, min: DockingSplitSizeBounds.leftRow.lowerBound, max: DockingSplitSizeBounds.leftRow.upperBound, preferred: 200
        )
        self.leftMidItem = Self.makeContainerItem(
            self.leftMidC, canCollapse: true, min: DockingSplitSizeBounds.leftRow.lowerBound, max: DockingSplitSizeBounds.leftRow.upperBound, preferred: 200
        )
        self.leftBotItem = Self.makeContainerItem(
            self.leftBotC, canCollapse: true, min: DockingSplitSizeBounds.leftRow.lowerBound, max: DockingSplitSizeBounds.leftRow.upperBound, preferred: 200
        )
        // One row must be the “flex” column so the **horizontal** dividers between rows are draggable; all `.defaultHigh` can feel locked.
        leftTopItem.holdingPriority = .defaultHigh
        leftMidItem.holdingPriority = .defaultLow
        leftBotItem.holdingPriority = .defaultHigh
        leftCol.addSplitViewItem(leftTopItem)
        leftCol.addSplitViewItem(leftMidItem)
        leftCol.addSplitViewItem(leftBotItem)
        self.leftColumnVC = leftCol

        let h = NSSplitViewController()
        self.middleRowSplit = h
        h.splitView.isVertical = true
        h.splitView.dividerStyle = .paneSplitter

        let cItem = NSSplitViewItem(contentListWithViewController: self.centerVC)
        cItem.minimumThickness = 160
        cItem.maximumThickness = 4_000
        cItem.canCollapse = false
        cItem.holdingPriority = .defaultLow

        self.leftColItem = NSSplitViewItem(contentListWithViewController: leftCol)
        leftColItem.minimumThickness = DockingSplitSizeBounds.leftColumn.lowerBound
        leftColItem.maximumThickness = DockingSplitSizeBounds.leftColumn.upperBound
        leftColItem.holdingPriority = .defaultHigh

        rightItem = Self.makeContainerItem(
            self.rightC, canCollapse: true, min: DockingSplitSizeBounds.rightPane.lowerBound, max: DockingSplitSizeBounds.rightPane.upperBound, preferred: 300
        )
        rightItem.holdingPriority = .defaultHigh
        h.addSplitViewItem(leftColItem)
        h.addSplitViewItem(cItem)
        h.addSplitViewItem(rightItem)

        let root = NSSplitViewController()
        // Root: [ top = 3-col strip `h` | bottom = bottom dock ] — two rows, **horizontal** dividers between them.
        root.splitView.isVertical = false
        root.splitView.dividerStyle = .paneSplitter
        self.bottomItem = Self.makeContainerItem(
            self.bottomC, canCollapse: true, min: DockingSplitSizeBounds.bottomStrip.lowerBound, max: DockingSplitSizeBounds.bottomStrip.upperBound, preferred: 180
        )
        let topStrip = NSSplitViewItem(contentListWithViewController: h)
        topStrip.minimumThickness = DockingSplitSizeBounds.mainContentStripMinHeight
        topStrip.maximumThickness = DockingSplitSizeBounds.mainContentStripMaxHeight
        // Match bottom so the **horizontal** root divider is not one-sided; equal priority helps free dragging.
        topStrip.holdingPriority = .init(250) // .default
        self.topStripItem = topStrip
        bottomItem.holdingPriority = .init(250)
        root.addSplitViewItem(topStrip)
        root.addSplitViewItem(bottomItem)

        self.splitController = root
        super.init()
        root.view.wantsLayer = true
        root.view.layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor
        root.view.setAccessibilityIdentifier("pm-dock-root")
    }

    private static func makeContainerItem(_ vc: NSViewController, canCollapse: Bool, min: CGFloat, max: CGFloat, preferred _: CGFloat) -> NSSplitViewItem {
        let it = NSSplitViewItem(contentListWithViewController: vc)
        it.canCollapse = canCollapse
        it.minimumThickness = min
        it.maximumThickness = max
        return it
    }

    /// Empty dock slots must use **min thickness 0** so collapsed panes do not reserve splitter track space or fight window resize.
    /// `applySizesOnMain` resets nominal mins from `DockingSplitSizeBounds`; call this after that and after restore geometry.
    private func reconcileEmptyDockSplitMins() {
        let rowLo = DockingSplitSizeBounds.leftRow.lowerBound
        let rowHi = DockingSplitSizeBounds.leftRow.upperBound
        let applyRow: (NSSplitViewItem, DockContainerViewController) -> Void = { item, box in
            if box.hosted == nil {
                item.minimumThickness = 0
                item.maximumThickness = rowHi
            } else {
                item.minimumThickness = rowLo
                item.maximumThickness = rowHi
            }
        }
        applyRow(leftTopItem, leftTopC)
        applyRow(leftMidItem, leftMidC)
        applyRow(leftBotItem, leftBotC)

        let colB = DockingSplitSizeBounds.leftColumn
        if leftTopC.hosted == nil, leftMidC.hosted == nil, leftBotC.hosted == nil {
            leftColItem.minimumThickness = 0
            leftColItem.maximumThickness = colB.upperBound
        } else {
            leftColItem.minimumThickness = colB.lowerBound
            leftColItem.maximumThickness = colB.upperBound
        }

        let rp = DockingSplitSizeBounds.rightPane
        if rightC.hosted == nil {
            rightItem.minimumThickness = 0
            rightItem.maximumThickness = rp.upperBound
        } else {
            rightItem.minimumThickness = rp.lowerBound
            rightItem.maximumThickness = rp.upperBound
        }

        let bs = DockingSplitSizeBounds.bottomStrip
        if bottomC.hosted == nil {
            bottomItem.minimumThickness = 0
            bottomItem.maximumThickness = bs.upperBound
        } else {
            bottomItem.minimumThickness = bs.lowerBound
            bottomItem.maximumThickness = bs.upperBound
        }
    }

    func register(_ p: DockPanel) {
        panelById[p.id] = p
        if let d = p.viewController as? DemoPanelViewController {
            d.wxxDockHost = self
        }
    }

    // MARK: Slots: which panel occupies which dock (v1: at most one per container)

    private var leftTopID: PanelID?
    private var leftMidID: PanelID?
    private var leftBotID: PanelID?
    private var rightID: PanelID?
    private var bottomID: PanelID?

    private func clearID(_ id: PanelID) {
        if leftTopID == id { leftTopID = nil }
        if leftMidID == id { leftMidID = nil }
        if leftBotID == id { leftBotID = nil }
        if rightID == id { rightID = nil }
        if bottomID == id { bottomID = nil }
    }

    private func syncLeftColumnCollapse() {
        leftTopItem.isCollapsed = leftTopC.hosted == nil
        leftMidItem.isCollapsed = leftMidC.hosted == nil
        leftBotItem.isCollapsed = leftBotC.hosted == nil
        let empty = leftTopC.hosted == nil && leftMidC.hosted == nil && leftBotC.hosted == nil
        leftColItem.isCollapsed = empty
    }

    private func beginLayoutTransaction() {
        layoutTransactionDepth += 1
        logDock("layout tx begin depth=\(layoutTransactionDepth)")
    }

    private func endLayoutTransaction() {
        layoutTransactionDepth = max(0, layoutTransactionDepth - 1)
        logDock("layout tx end depth=\(layoutTransactionDepth)")
        if layoutTransactionDepth == 0 { refreshAllPanels() }
    }

    /// Stops a panel in dock and/or float (internal; not the public `hide` menu path).
    private func unmountPanelState(_ id: PanelID) {
        reparentingDepth += 1
        defer { reparentingDepth = max(0, reparentingDepth - 1) }
        guard let panel = panelById[id] else { return }
        logDock("unmount start: \(id.rawValue)")
        removeFromDocking(panel)
        if panel.floatingPanel != nil {
            logDock("unmount: clear float shell \(id.rawValue)")
            clearFloatAndScheduleClose(panel)
        }
        panel.position = .hidden
        syncLeftColumnCollapse()
        rightItem.isCollapsed = rightC.hosted == nil
        bottomItem.isCollapsed = bottomC.hosted == nil
        reconcileEmptyDockSplitMins()
        clearID(id)
        if !isLayoutTransactionActive {
            logDock("unmount end: \(id.rawValue)")
            refreshAllPanels()
            saveLastSnapshot()
        }
    }

    /// Clears host + panel from the float shell and hides it. Does **not** call `close()` — pool-drain
    /// crashes have been tied to `close` racing reparent; `orderOut` + dropping our reference is enough for the prototype.
    private func clearFloatAndScheduleClose(_ panel: DockPanel) {
        guard let w = panel.floatingPanel else { return }
        w.delegate = nil
        logDock("clear float content: \(panel.id.rawValue)")
        clearFloatingWindowContent(w)
        w.orderOut(nil)
        panel.floatingPanel = nil
        w.isReleasedWhenClosed = true
    }

    func container(for pos: DockPosition) -> DockContainerViewController? {
        switch pos {
        case .leftTop: return leftTopC
        case .leftMid: return leftMidC
        case .leftBottom: return leftBotC
        case .right: return rightC
        case .bottom: return bottomC
        case .hidden, .floating: return nil
        }
    }

    /// `true` when the panel is embedded in the main split (not floating, not fully hidden for slot purposes).
    func isInDockedSplit(_ id: PanelID) -> Bool {
        guard let p = panelById[id] else { return false }
        switch p.position {
        case .leftTop, .leftMid, .leftBottom, .right, .bottom: return true
        case .hidden, .floating: return false
        }
    }

    func floatingNSWindow(_ id: PanelID) -> NSWindow? { panelById[id]?.floatingPanel }

    /// Screen-space rect of the whole 3-row left column (for anchoring the redock cross on the “files” / sidebar).
    fileprivate func leftColumnScreenRect() -> NSRect? {
        guard !leftColItem.isCollapsed else { return nil }
        return Self.screenRect(of: leftColumnVC.view, in: window)
    }
    fileprivate func bottomDockScreenRect() -> NSRect? {
        guard !bottomItem.isCollapsed else { return nil }
        return Self.screenRect(of: bottomC.view, in: window)
    }
    fileprivate func rightDockScreenRect() -> NSRect? {
        guard !rightItem.isCollapsed else { return nil }
        return Self.screenRect(of: rightC.view, in: window)
    }

    private static func screenRect(of v: NSView, in w: NSWindow?) -> NSRect? {
        guard let mw = w, let cv = mw.contentView else { return nil }
        guard v.window === mw, v.frame.width > 0.5, v.frame.height > 0.5 else { return nil }
        return mw.convertToScreen(v.convert(v.bounds, to: cv))
    }

    // MARK: Redock (VS Code–style overlay)

    private var redockHintWindow: NSWindow?
    private var redockHintGrid: RedockHintGridView?

    /// `contentView.frame` is in its superview; `convertToScreen` must use the rect in **window** base coordinates.
    private func mainContentLayoutRectInScreen() -> NSRect? {
        guard let w = window, let c = w.contentView else { return nil }
        c.layoutSubtreeIfNeeded()
        // `convert(_:to: nil)` = rect in host window’s base (same space as `convertToScreen` expects)
        return w.convertToScreen( c.convert( c.bounds, to: nil) )
    }

    /// A separate top-level `NSWindow` is required so the overlay draws **above** the floating `NSPanel`; then we still must order it above the panel.
    private func installRedockHintWindowIfNeeded() {
        guard redockHintWindow == nil, let w = window, let c = w.contentView, mainContentLayoutRectInScreen() != nil else { return }
        c.layoutSubtreeIfNeeded()
        let fr = c.bounds
        let g = RedockHintGridView( frame: fr)
        g.wxxDockHost = self
        g.autoresizingMask = [ .width, .height]
        let root = PassThroughRootView( frame: fr)
        root.autoresizingMask = [ .width, .height]
        root.addSubview( g)
        let panel = NSWindow(
            contentRect: mainContentLayoutRectInScreen()!,
            styleMask: [ .borderless],
            backing: .buffered,
            defer: false
        )
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.isReleasedWhenClosed = false
        panel.hasShadow = false
        panel.ignoresMouseEvents = true
        // Must sit above a titled `NSPanel` at `level == .floating`; +1 is not always enough in practice.
        panel.level = .popUpMenu
        panel.collectionBehavior = [ .canJoinAllSpaces, .fullScreenAuxiliary]
        panel.contentView = root
        redockHintGrid = g
        redockHintWindow = panel
    }

    private func positionRedockHintOverMainContent() {
        guard let h = redockHintWindow, let r = mainContentLayoutRectInScreen(), r.size.width > 0, r.size.height > 0 else { return }
        h.setFrame( r, display: true)
    }

    /// Keep the redock hint above floating tool windows.
    ///
    /// **Do not** use `order(_:relativeTo: windowNumber)` against float windows. While a float is
    /// closing or its `contentViewController` is being cleared, that window can be a zombie from
    /// AppKit’s perspective — `relativeTo:` has caused `objc_release` / pool-drain crashes (seg 11)
    /// in the main run loop. The hint window’s **level** (`.popUpMenu`) is already well above
    /// `.floating`, so `orderFrontRegardless` is enough.
    private func bringRedockHintAboveAllFloatingWindows() {
        guard let h = redockHintWindow else { return }
        h.level = .popUpMenu
        h.orderFrontRegardless()
    }

    /// Shows the + reticle overlay on top of the workbench **and** the floating tool windows.
    func setRedockHintVisible(_ on: Bool) {
        logDock( "redock UI \(on ? "show" : "hide")")
        if on {
            installRedockHintWindowIfNeeded()
            positionRedockHintOverMainContent()
            redockHintWindow?.contentView?.layoutSubtreeIfNeeded()
            redockHintGrid?.layoutSubtreeIfNeeded()
            redockHintGrid?.display()
            // Pop-up / hint window: must come after a frame, then explicitly stack above the float(s).
            redockHintWindow?.orderFrontRegardless()
            bringRedockHintAboveAllFloatingWindows()
        } else {
            redockHintWindow?.orderOut(nil)
        }
    }

    func updateRedockHintPointer( atScreen p: NSPoint) {
        positionRedockHintOverMainContent()
        bringRedockHintAboveAllFloatingWindows()
        redockHintGrid?.updateHighlight( atScreen: p)
        redockHintGrid?.display()
    }

    private func mapRedockToDock(_ t: DockRedockHintTarget, isPointerInLeftColumn: Bool) -> DockPosition? {
        switch t {
        case .left: return .leftTop
        case .right: return .right
        case .bottom: return isPointerInLeftColumn ? .leftMid : .bottom
        case .center, .outside: return nil
        }
    }

    /// On mouse up: if the pointer is over an edge, dock the panel; otherwise keep float.
    func tryRedockAfterTitlebarDrop(panelID: PanelID, atScreen p: NSPoint) {
        let t: DockRedockHintTarget
        let inLeft: Bool
        if let g = redockHintGrid {
            t = g.resolveDrop(atScreen: p)
            inLeft = g.isPointerInLeftColumn
        } else {
            t = .outside
            inLeft = false
        }
        setRedockHintVisible(false)
        redockHintGrid?.setActive( .outside)
        if let pos = mapRedockToDock(t, isPointerInLeftColumn: inLeft) {
            let pid = panelID
            // Defer until after this `mouseDown` returns; reparenting during tracking has crashed AppKit.
            DispatchQueue.main.async { [weak self] in
                self?.attach(pid, to: pos)
            }
        } else {
            logDock( "redock drop: \(panelID.rawValue) zone=\(t) (still floating)")
        }
    }

    /// Positions a floating `NSPanel` so the in-content `titleView`’s min corner tracks `mouse` minus the grab offset from a prior `mouseDown` (keeps the cursor “on” the bar while tearing off).
    func positionFloatingWindow(_ w: NSWindow, titleView: NSView, mouse: NSPoint, grabFromTitlebarMin: NSPoint) {
        w.layoutIfNeeded()
        w.contentView?.layoutSubtreeIfNeeded()
        guard let content = w.contentView else { return }
        let tIn = titleView.convert(titleView.bounds, to: content)
        let screen = w.convertToScreen(tIn)
        let wantMinX = mouse.x - grabFromTitlebarMin.x
        let wantMinY = mouse.y - grabFromTitlebarMin.y
        let d = NSPoint(x: wantMinX - screen.minX, y: wantMinY - screen.minY)
        w.setFrameOrigin(NSPoint(x: w.frame.minX + d.x, y: w.frame.minY + d.y))
    }

    /// - Parameter batch: `true` when called from `apply` after `beginLayoutTransaction` (no per-step save/refresh).
    func attach(_ id: PanelID, to pos: DockPosition, batch: Bool = false) {
        reparentingDepth += 1
        defer { reparentingDepth = max(0, reparentingDepth - 1) }
        logDock("attach start: \(id.rawValue) → \(pos.rawValue) batch=\(batch)")
        guard let panel = panelById[id] else { return }
        if pos == .hidden {
            unmountPanelState(id)
            logDock("attach end (unmount hidden)")
            return
        }
        if pos == .floating {
            detach(id)
            logDock("attach end (detach to float)")
            return
        }
        if panel.floatingPanel != nil {
            logDock("attach: reparent from float, orderOut shell (no close)")
            clearFloatAndScheduleClose(panel)
        }
        clearID(id)
        switch pos {
        case .leftTop:
            if let o = leftTopID, o != id { unmountPanelState(o) }
        case .leftMid:
            if let o = leftMidID, o != id { unmountPanelState(o) }
        case .leftBottom:
            if let o = leftBotID, o != id { unmountPanelState(o) }
        case .right:
            if let o = rightID, o != id { unmountPanelState(o) }
        case .bottom:
            if let o = bottomID, o != id { unmountPanelState(o) }
        case .hidden, .floating: break
        }
        logDock("attach: embed to split \(id.rawValue)")
        removeFromDocking(panel)
        switch pos {
        case .leftTop:
            leftTopC.embed(panel.viewController)
            leftTopID = id
        case .leftMid:
            leftMidC.embed(panel.viewController)
            leftMidID = id
        case .leftBottom:
            leftBotC.embed(panel.viewController)
            leftBotID = id
        case .right:
            rightC.embed(panel.viewController)
            rightItem.isCollapsed = false
            rightID = id
        case .bottom:
            bottomC.embed(panel.viewController)
            bottomItem.isCollapsed = false
            bottomID = id
        case .hidden, .floating: break
        }
        syncLeftColumnCollapse()
        reconcileEmptyDockSplitMins()
        panel.position = pos
        splitController.view.layoutSubtreeIfNeeded()
        logDock("attach end: \(id.rawValue) → \(pos.rawValue)")
        if !batch {
            refreshAllPanels()
            saveLastSnapshot()
        }
    }

    func detach(_ id: PanelID) {
        reparentingDepth += 1
        defer { reparentingDepth = max(0, reparentingDepth - 1) }
        logDock("detach start: \(id.rawValue)")
        guard let panel = panelById[id] else { return }
        removeFromDocking(panel)
        clearID(id)
        syncLeftColumnCollapse()
        rightItem.isCollapsed = rightC.hosted == nil
        bottomItem.isCollapsed = bottomC.hosted == nil
        reconcileEmptyDockSplitMins()
        closeFloatingIfNeeded(panel, forceClose: true)

        let w: CGFloat = 300
        let h: CGFloat = 320
        let rect = defaultFloatRect(width: w, height: h)
        let p = NSPanel(
            contentRect: rect,
            styleMask: [.titled, .closable, .resizable, .utilityWindow],
            backing: .buffered,
            defer: false
        )
        p.hidesOnDeactivate = false
        p.level = .floating
        p.isFloatingPanel = true
        p.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        p.title = panel.title
        if kFloatFrameAutosave { p.setFrameAutosaveName("float-\(panel.id.rawValue)-frame") }
        p.isReleasedWhenClosed = false
        p.delegate = self
        p.contentMinSize = NSSize(width: 200, height: 160)
        logDock("detach: install float host for \(id.rawValue)")
        installFloatingWindowContent(p, viewController: panel.viewController)
        panel.floatingPanel = p
        panel.position = .floating
        p.makeKeyAndOrderFront(nil)
        logDock("detach end: \(id.rawValue) → floating")
        if !isLayoutTransactionActive {
            refreshAllPanels()
            saveLastSnapshot()
        }
    }

    func hide(_ id: PanelID) {
        unmountPanelState(id)
    }

    private func removeFromDocking(_ p: DockPanel) {
        for c in [leftTopC, leftMidC, leftBotC, rightC, bottomC] where c.hosted === p.viewController { c.embed(nil) }
    }

    private func closeFloatingIfNeeded(_ p: DockPanel, forceClose: Bool = true) {
        if !forceClose { p.floatingPanel = nil; return }
        guard p.floatingPanel != nil else { return }
        clearFloatAndScheduleClose(p)
    }

    private func defaultFloatRect(width: CGFloat, height: CGFloat) -> NSRect {
        if let f = window?.frame {
            return NSRect(x: f.midX - width / 2, y: f.midY - height / 2, width: width, height: height)
        }
        return NSRect(x: 200, y: 200, width: width, height: height)
    }

    /// The float’s `contentViewController` is `FloatPanelHostViewController` only; the tool panel is its **child** VC.
    private func installFloatingWindowContent(_ w: NSWindow, viewController vc: NSViewController) {
        let host = FloatPanelHostViewController()
        w.contentViewController = host
        host.setChildPanel(vc)
    }

    private func clearFloatingWindowContent(_ w: NSWindow) {
        if let host = w.contentViewController as? FloatPanelHostViewController {
            host.evictChildPanel()
        }
        w.contentViewController = nil
    }

    // MARK: Layout I/O

    func apply(_ layout: WorkspaceLayout) {
        logDock("apply layout “\(layout.name)” (tx)")
        beginLayoutTransaction()
        for pid in PanelID.allCases { unmountPanelState(pid) }
        for pl in layout.panels {
            if pl.position == .hidden { continue }
            switch pl.position {
            case .floating:
                if let f = pl.floatingFrame { floatFromLayout(pid: pl.panel, frame: f) } else { detach(pl.panel) }
            case .leftTop, .leftMid, .leftBottom, .right, .bottom: attach(pl.panel, to: pl.position, batch: true)
            case .hidden: break
            }
        }
        endLayoutTransaction()
        applyApproximateSizes(layout)
        UserDefaults.standard.set(layout.name, forKey: UD.name)
        let snap = currentLayout(name: layout.name)
        logDock("apply: persist + refresh")
        persist(snap)
    }

    private func floatFromLayout(pid: PanelID, frame: NSRect) {
        guard let panel = panelById[pid] else { return }
        logDock("floatFromLayout: \(pid.rawValue)")
        closeFloatingIfNeeded(panel)
        removeFromDocking(panel)
        clearID(pid)
        let p = NSPanel(
            contentRect: frame,
            styleMask: [.titled, .closable, .resizable, .utilityWindow],
            backing: .buffered,
            defer: false
        )
        p.hidesOnDeactivate = false
        p.level = .floating
        p.isFloatingPanel = true
        p.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        p.title = panel.title
        p.setFrame(frame, display: true)
        if kFloatFrameAutosave { p.setFrameAutosaveName("float-\(panel.id.rawValue)-frame") }
        p.isReleasedWhenClosed = false
        p.delegate = self
        installFloatingWindowContent(p, viewController: panel.viewController)
        panel.floatingPanel = p
        panel.position = .floating
        p.makeKeyAndOrderFront(nil)
    }

    private func applyApproximateSizes(_ layout: WorkspaceLayout) {
        let layoutCopy = layout
        DispatchQueue.main.async { [weak self] in
            self?.applySizesOnMain(layoutCopy)
        }
    }

    private func applySizesOnMain(_ layout: WorkspaceLayout) {
        window?.layoutIfNeeded()
        // Never set `minimumThickness == maximumThickness` from a saved width/height — that **locks** dividers
        // (AppKit only toggles collapse) and can pin the main window to a **short** height.
        leftColItem.minimumThickness = DockingSplitSizeBounds.leftColumn.lowerBound
        leftColItem.maximumThickness = DockingSplitSizeBounds.leftColumn.upperBound
        rightItem.minimumThickness = DockingSplitSizeBounds.rightPane.lowerBound
        rightItem.maximumThickness = DockingSplitSizeBounds.rightPane.upperBound
        bottomItem.minimumThickness = DockingSplitSizeBounds.bottomStrip.lowerBound
        bottomItem.maximumThickness = DockingSplitSizeBounds.bottomStrip.upperBound
        topStripItem.minimumThickness = DockingSplitSizeBounds.mainContentStripMinHeight
        topStripItem.maximumThickness = DockingSplitSizeBounds.mainContentStripMaxHeight
        leftTopItem.minimumThickness = DockingSplitSizeBounds.leftRow.lowerBound
        leftTopItem.maximumThickness = DockingSplitSizeBounds.leftRow.upperBound
        leftMidItem.minimumThickness = DockingSplitSizeBounds.leftRow.lowerBound
        leftMidItem.maximumThickness = DockingSplitSizeBounds.leftRow.upperBound
        leftBotItem.minimumThickness = DockingSplitSizeBounds.leftRow.lowerBound
        leftBotItem.maximumThickness = DockingSplitSizeBounds.leftRow.upperBound
        reconcileEmptyDockSplitMins()
        // `WorkspaceLayout` carries optional w/h; we never applied them to `NSSplitView` before — that produced random defaults and “stuck” feel.
        let copy = layout
        DispatchQueue.main.async { [weak self] in
            self?.window?.layoutIfNeeded()
            self?.splitController.view.layoutSubtreeIfNeeded()
            self?.applyRestoredSplitGeometry(from: copy)
            self?.reconcileEmptyDockSplitMins()
        }
    }

    /// Best-effort: move split dividers to match saved `PanelLayout` sizes (clamped to `minPosition` / `maxPosition` per `NSSplitView`).
    private func applyRestoredSplitGeometry(from layout: WorkspaceLayout) {
        window?.layoutIfNeeded()
        splitController.view.layoutSubtreeIfNeeded()
        middleRowSplit.view.layoutSubtreeIfNeeded()
        leftColumnVC.view.layoutSubtreeIfNeeded()

        if !bottomItem.isCollapsed, let hb = layout.panels.first(where: { $0.position == .bottom })?.height, hb > 1 {
            setRootDividerForBottomHeight(hb)
        }
        middleRowSplit.view.layoutSubtreeIfNeeded()

        if !leftColItem.isCollapsed, let wL = firstLeftColumnWidth(in: layout), wL > 1 {
            setMiddleRowLeftColumnWidth(wL)
        }
        middleRowSplit.view.layoutSubtreeIfNeeded()

        if !rightItem.isCollapsed, let wR = layout.panels.first(where: { $0.position == .right })?.width, wR > 1 {
            setMiddleRowRightPaneWidth(wR)
        }
        leftColumnVC.view.layoutSubtreeIfNeeded()
        if !leftColItem.isCollapsed {
            applyLeftColumnRowHeightsIfFlipped(from: layout)
        }
    }

    private func firstLeftColumnWidth(in layout: WorkspaceLayout) -> CGFloat? {
        for pl in layout.panels {
            switch pl.position {
            case .leftTop, .leftMid, .leftBottom: if let w = pl.width, w > 1 { return w }
            default: break
            }
        }
        return nil
    }

    private func setRootDividerForBottomHeight(_ wantBottom: CGFloat) {
        let sv = splitController.splitView
        guard !sv.isVertical, sv.subviews.count >= 2 else { return }
        sv.layoutSubtreeIfNeeded()
        let H = max(0, sv.bounds.height)
        guard H > 32 else { return }
        var bh = wantBottom
        bh = min(bh, bottomItem.maximumThickness)
        bh = max(bh, bottomItem.minimumThickness)
        bh = min(bh, H - topStripItem.minimumThickness)
        let topH = H - bh
        let p: CGFloat
        if sv.isFlipped {
            p = topH
        } else {
            p = bh
        }
        let lo = sv.minPossiblePositionOfDivider(at: 0)
        let hi = sv.maxPossiblePositionOfDivider(at: 0)
        let q = min(max(p, lo), hi)
        sv.setPosition(q, ofDividerAt: 0)
    }

    private func setMiddleRowLeftColumnWidth(_ wantLeft: CGFloat) {
        let sv = middleRowSplit.splitView
        guard sv.isVertical, sv.subviews.count >= 3 else { return }
        sv.layoutSubtreeIfNeeded()
        var w = wantLeft
        w = min(w, leftColItem.maximumThickness)
        w = max(w, leftColItem.minimumThickness)
        let lo = sv.minPossiblePositionOfDivider(at: 0)
        let hi = sv.maxPossiblePositionOfDivider(at: 0)
        let q = min(max(w, lo), hi)
        sv.setPosition(q, ofDividerAt: 0)
    }

    private func setMiddleRowRightPaneWidth(_ wantRight: CGFloat) {
        let sv = middleRowSplit.splitView
        guard sv.isVertical, sv.subviews.count >= 3 else { return }
        sv.layoutSubtreeIfNeeded()
        let W = max(0, sv.bounds.width)
        guard W > 100 else { return }
        var wr = wantRight
        wr = min(wr, rightItem.maximumThickness)
        wr = max(wr, rightItem.minimumThickness)
        let xFromLeft = W - wr
        let lo = sv.minPossiblePositionOfDivider(at: 1)
        let hi = sv.maxPossiblePositionOfDivider(at: 1)
        let q = min(max(xFromLeft, lo), hi)
        sv.setPosition(q, ofDividerAt: 1)
    }

    /// Only when the split is flipped (y from top) — non-flipped y-from-bottom math differs per OS version.
    private func applyLeftColumnRowHeightsIfFlipped(from layout: WorkspaceLayout) {
        let sv = leftColumnVC.splitView
        guard !sv.isVertical, sv.subviews.count >= 3, sv.isFlipped else { return }
        let h1 = layout.panels.first { $0.position == .leftTop }?.height
        let h2 = layout.panels.first { $0.position == .leftMid }?.height
        let h3 = layout.panels.first { $0.position == .leftBottom }?.height
        guard let a = h1, let b = h2, let c = h3, a > 1, b > 1, c > 1 else { return }
        sv.layoutSubtreeIfNeeded()
        let H = max(0, sv.bounds.height)
        guard H > 32 else { return }
        var r1 = min(max(a, leftTopItem.minimumThickness), leftTopItem.maximumThickness)
        var r2 = min(max(b, leftMidItem.minimumThickness), leftMidItem.maximumThickness)
        var r3 = min(max(c, leftBotItem.minimumThickness), leftBotItem.maximumThickness)
        let sum = r1 + r2 + r3
        if sum > H {
            let s = H / sum
            r1 *= s
            r2 *= s
            r3 *= s
        }
        sv.setPosition(clamp(r1, sv, 0), ofDividerAt: 0)
        sv.layoutSubtreeIfNeeded()
        sv.setPosition(clamp(r1 + r2, sv, 1), ofDividerAt: 1)
    }

    private func clamp(_ p: CGFloat, _ sv: NSSplitView, _ divider: Int) -> CGFloat {
        let lo = sv.minPossiblePositionOfDivider(at: divider)
        let hi = sv.maxPossiblePositionOfDivider(at: divider)
        return min(max(p, lo), hi)
    }

    /// Spec: `func currentLayout(name: String) -> WorkspaceLayout`
    func currentLayout(name: String) -> WorkspaceLayout {
        var rows: [PanelLayout] = []
        for id in PanelID.allCases {
            guard let p = panelById[id] else { continue }
            var w: CGFloat? = nil
            var h: CGFloat? = nil
            var f: CGRect? = nil
            if p.position == .floating, let win = p.floatingPanel { f = win.frame }
            else if p.position == .leftTop, leftTopC.hosted === p.viewController {
                w = max(0, leftTopC.view.bounds.width)
                h = max(0, leftTopC.view.bounds.height)
            } else if p.position == .leftMid, leftMidC.hosted === p.viewController {
                w = max(0, leftMidC.view.bounds.width)
                h = max(0, leftMidC.view.bounds.height)
            } else if p.position == .leftBottom, leftBotC.hosted === p.viewController {
                w = max(0, leftBotC.view.bounds.width)
                h = max(0, leftBotC.view.bounds.height)
            } else if p.position == .right, rightC.hosted === p.viewController { w = max(0, rightC.view.bounds.width) }
            else if p.position == .bottom, bottomC.hosted === p.viewController { h = max(0, bottomC.view.bounds.height) }
            rows.append(PanelLayout(panel: id, position: p.position, width: w, height: h, floatingFrame: f))
        }
        return WorkspaceLayout(name: name, panels: rows.sorted { $0.panel.rawValue < $1.panel.rawValue })
    }

    private func saveLastSnapshot() {
        let n = UserDefaults.standard.string(forKey: UD.name) ?? "last"
        persist(currentLayout(name: n))
    }

    private func persist(_ w: WorkspaceLayout) {
        if let d = try? JSONEncoder().encode(w) { UserDefaults.standard.set(d, forKey: UD.last) }
    }

    func loadLastLayout() -> WorkspaceLayout? {
        guard let d = UserDefaults.standard.data(forKey: UD.last) else { return nil }
        return try? JSONDecoder().decode(WorkspaceLayout.self, from: d)
    }

    /// Spec: `func saveLayout(name: String)` — stores named + last snapshot in UserDefaults.
    func saveLayout(name: String) {
        logDock("saveLayout(\"\(name)\")")
        let snap = currentLayout(name: name)
        if let d = try? JSONEncoder().encode(snap) {
            UserDefaults.standard.set(d, forKey: "layout.\(name)")
        }
        persist(snap)
    }

    /// Spec: `func loadLayout(name: String) -> WorkspaceLayout?`
    func loadLayout(name: String) -> WorkspaceLayout? {
        if let d = UserDefaults.standard.data(forKey: "layout.\(name)"), let l = try? JSONDecoder().decode(WorkspaceLayout.self, from: d) {
            return l
        }
        if name == WorkspaceLayout.browser.name { return .browser }
        return nil
    }

    private func refreshAllPanels() {
        for p in panelById.values {
            guard let d = p.viewController as? DemoPanelViewController else { continue }
            d.updatePositionText("Position: \(p.position.rawValue)")
        }
    }

    // MARK: NSWindowDelegate (floating)

    /// User closed the float shell. **State only** — no `hide`, no `removeFromDocking`, no `clearFloating…`
    /// (avoids double-reparent + pool-drain `objc_release` when AppKit is still tearing the window down).
    func windowWillClose(_ notification: Notification) {
        guard let w = notification.object as? NSWindow else { return }
        for (id, p) in panelById where p.floatingPanel === w {
            logDock("windowWillClose (user) float state only: \(id.rawValue)")
            w.delegate = nil
            p.floatingPanel = nil
            p.position = .hidden
            clearID(id)
            syncLeftColumnCollapse()
            rightItem.isCollapsed = rightC.hosted == nil
            bottomItem.isCollapsed = bottomC.hosted == nil
            DispatchQueue.main.async { [weak self] in
                self?.refreshAllPanels()
                self?.saveLastSnapshot()
            }
            return
        }
    }
}

// MARK: - Factory

extension WxxDockHost {
    static func makeWithRegisteredPanels() -> WxxDockHost {
        let m = WxxDockHost()
        let spec: [(PanelID, String)] = [
            (.files, "Files"), (.inspector, "Inspector"), (.chat, "AI Chat"),
            (.metadata, "Metadata"), (.log, "Log")
        ]
        for (i, t) in spec {
            let vc = DemoPanelViewController(panelID: i, title: t)
            m.register(DockPanel(id: i, title: t, viewController: vc))
        }
        InAppLog.line( "registered \(PanelID.allCases.map(\.rawValue).joined( separator: ", "))")
        return m
    }
}

// MARK: - Main window (no layout toolbar)
// Plain `NSWindow` + strong ref (not `NSWindowController`) so AppKit always has a real window to show.
// `NSWindowController` + `@MainActor` was a likely cause of “app runs, UI never appears” on some toolchains.

@objcMembers
final class WxxMainDockWindow: NSObject {
    let wxxDockHost: WxxDockHost
    let window: NSWindow
    override init() {
        let d = WxxDockHost.makeWithRegisteredPanels()
        self.wxxDockHost = d
        let style: NSWindow.StyleMask = [.titled, .closable, .miniaturizable, .resizable]
        let w = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1_200, height: 780),
            styleMask: style,
            backing: .buffered,
            defer: false
        )
        w.minSize = NSSize(width: 420, height: 400)
        w.contentMaxSize = NSSize(width: 16_000, height: 16_000)
        w.contentResizeIncrements = NSSize(width: 1, height: 1)
        w.title = "pm-image — Wxx docker (AppKit)"
        w.isReleasedWhenClosed = false
        w.isOpaque = true
        w.backgroundColor = .windowBackgroundColor
        w.titleVisibility = .visible
        w.setAccessibilityIdentifier("pm-main-window")
        w.contentViewController = d.splitController
        // Force view load + layout so the split is not 0×0 on first display.
        d.splitController.view.layoutSubtreeIfNeeded()
        PMWindowPlace.sizeAndCenterInVisibleFrame(w, contentSize: NSSize(width: 1_200, height: 780))
        self.window = w
        super.init()
        d.window = w
        w.toolbar = nil
    }

    /// Prefer a fresh `browser` layout on first run after this build (avoids bad saved JSON). Then restore or browser.
    func applyInitialOrSavedLayout() {
        let key = "WxxDock.layoutSchema"
        if UserDefaults.standard.integer(forKey: key) < 7 {
            UserDefaults.standard.set(7, forKey: key)
            UserDefaults.standard.removeObject(forKey: "WxxDock.lastLayoutJSON")
            UserDefaults.standard.removeObject(forKey: "WxxDock.lastLayoutName")
            UserDefaults.standard.removeObject(forKey: "DockingAppKit.lastLayoutJSON")
            UserDefaults.standard.removeObject(forKey: "DockingAppKit.lastLayoutName")
            wxxDockHost.apply(.browser)
            return
        }
        if let last = wxxDockHost.loadLastLayout() {
            wxxDockHost.apply(last)
        } else {
            wxxDockHost.apply(.browser)
        }
    }

    func show() {
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
    }

    /// XCUITest drags to the “window corner” hit **dock content / splitters**, not the `NSThemeFrame` resize strip.
    /// UI tests set `launchEnvironment["PM_UITEST_BUMP_SIZE"] = "1"`; we **grow the content size** on the next tick so layout/screenshot checks match a real frame resize.
    func applyUITestContentSizeBumpIfNeeded() {
        guard ProcessInfo.processInfo.environment["PM_UITEST_BUMP_SIZE"] == "1" else { return }
        let w = window
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            w.layoutIfNeeded()
            self.wxxDockHost.splitController.view.layoutSubtreeIfNeeded()
            var sz = w.contentLayoutRect.size
            sz.width += 140
            sz.height += 100
            w.setContentSize(sz)
            w.layoutIfNeeded()
            self.wxxDockHost.splitController.view.layoutSubtreeIfNeeded()
        }
    }

    func menuLayoutBrowser() { wxxDockHost.apply(.browser) }
    func menuSaveLayout() { wxxDockHost.saveLayout(name: "custom") }

}

// MARK: - WxxDockAppLauncher
/// Strong holder for the dock shell (call from `AppDelegate` or embed in another app’s delegate).
@objcMembers
final class WxxDockAppLauncher: NSObject {
    private(set) static var main: WxxMainDockWindow?
    static func show() {
        InAppLog.line("WxxDockAppLauncher.show()")
        let shell = WxxMainDockWindow()
        main = shell
        shell.applyInitialOrSavedLayout()
        shell.show()
        shell.applyUITestContentSizeBumpIfNeeded()
        NSApp.activate(ignoringOtherApps: true)
    }
}

// MARK: - AppDelegate
// Process entry is `pm_imageApp.swift` — `@main` on `PmImageApp` + `NSApplicationDelegateAdaptor` only. No main storyboard.
// If you merge with another target: remove that target’s second `@main` and call `WxxDockAppLauncher.show()` from your `AppDelegate`.
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var smokeTestWindow: NSWindow?

    func applicationWillFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        print("WxxDockerKit: applicationDidFinishLaunching (docking main path)")
        if ProcessInfo.processInfo.environment["PM_HELLO"] == "1" {
            openHelloSmokeTestWindow()
            return
        }
        if ProcessInfo.processInfo.environment["PM_SMOKE"] == "1" {
            openSmokeTestWindow()
            NSApp.activate(ignoringOtherApps: true)
            return
        }
        InAppLog.line("applicationDidFinishLaunching (docking main path)")
        WxxDockAppLauncher.show()
        guard let host = WxxDockAppLauncher.main else { return }
        NSApp.mainMenu = buildMainMenu(host: host)
    }

    /// Smallest possible window: set scheme env `PM_HELLO=1` — if this does not show, the delegate/entry is wrong.
    private func openHelloSmokeTestWindow() {
        let w = NSWindow(
            contentRect: NSRect(x: 200, y: 200, width: 600, height: 400),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        w.title = "HELLO (PM_HELLO=1)"
        w.isOpaque = true
        w.backgroundColor = .systemRed
        w.makeKeyAndOrderFront(nil)
        w.orderFrontRegardless()
        smokeTestWindow = w
        NSApp.activate(ignoringOtherApps: true)
    }

    private func openSmokeTestWindow() {
        let w = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 480, height: 320),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        w.title = "pm-image smoke (set PM_SMOKE=1 in scheme)"
        w.isOpaque = true
        w.backgroundColor = .systemRed
        let v = NSView()
        v.wantsLayer = true
        v.layer?.backgroundColor = NSColor.systemOrange.cgColor
        w.contentView = v
        w.setContentSize(NSSize(width: 480, height: 320))
        w.center()
        w.makeKeyAndOrderFront(nil)
        w.orderFrontRegardless()
        smokeTestWindow = w
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            if let s = smokeTestWindow { s.makeKeyAndOrderFront(nil) }
            else { WxxDockAppLauncher.main?.show() }
        }
        return true
    }
    /// `true` can quit immediately if AppKit does not count the new window as “visible” yet; keep `false` for this prototype.
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
    private func buildMainMenu(host: WxxMainDockWindow) -> NSMenu {
        let m = NSMenu()
        m.addItem(appMenuItem)
        m.addItem(viewLayoutMenuItem(host: host))
        m.addItem(windowMenuItem)
        return m
    }
    private var appMenuItem: NSMenuItem {
        let n = String(ProcessInfo.processInfo.processName)
        let sub = NSMenu()
        sub.addItem(NSMenuItem(title: "Quit \(n)", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        let it = NSMenuItem(title: n, action: nil, keyEquivalent: "")
        it.submenu = sub
        return it
    }
    private func viewLayoutMenuItem(host: WxxMainDockWindow) -> NSMenuItem {
        let sub = NSMenu()
        addItem(sub, "Layout: Browser", #selector(WxxMainDockWindow.menuLayoutBrowser), "1", host, .command)
        sub.addItem(.separator())
        addItem(sub, "Save “custom” layout to UserDefaults", #selector(WxxMainDockWindow.menuSaveLayout), "S", host, [.command, .shift])
        let v = NSMenuItem(title: "View", action: nil, keyEquivalent: "")
        v.submenu = sub
        return v
    }
    private var windowMenuItem: NSMenuItem {
        let sub = NSMenu()
        sub.addItem(NSMenuItem(title: "Minimize", action: #selector(NSWindow.miniaturize(_:)), keyEquivalent: "m"))
        let w = NSMenuItem(title: "Window", action: nil, keyEquivalent: "")
        w.submenu = sub
        return w
    }
    private func addItem(
        _ sub: NSMenu,
        _ title: String,
        _ act: Selector,
        _ key: String,
        _ t: AnyObject,
        _ mod: NSEvent.ModifierFlags
    ) {
        let n = NSMenuItem(title: title, action: act, keyEquivalent: key)
        n.keyEquivalentModifierMask = mod
        n.target = t
        sub.addItem(n)
    }
}
