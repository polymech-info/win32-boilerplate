//
//  ProjectNavigatorThumbView.swift
//  PixlWiz
//
//  Thumbnail grid for the Project Navigator.
//
//  Navigation parity with the tree view:
//    ← →         one item left / right
//    ↑ ↓         one grid-row up / down (column-count-aware)
//    Return      open file (pin tab) or drill into folder
//    Backspace / X1 mouse button   go up browse directory
//    X2 mouse button               drill into selected folder
//    Double-click                  same as Return, immediate (no 2-second wait)
//    Ctrl+scroll wheel / pinch     zoom tile size
//    Go-up                         re-selects the folder just left (mirrors tree)
//
//  Why @StateObject for selection state:
//    NSEvent local monitors capture closures by reference. A SwiftUI struct
//    cannot be captured by reference, so mutations to @State properties inside
//    a monitor closure would operate on a stale copy and never reach the view.
//    ThumbViewModel is a class — its @Published properties update SwiftUI correctly.
//

import AppKit
import ImageIO
import SwiftUI

// MARK: - ViewModel

final class ThumbViewModel: ObservableObject {

    @Published var selectedID: CEWorkspaceFile.ID?
    @Published var tileSize: CGFloat = 80
    @Published var gridWidth: CGFloat = 200    // updated by GeometryReader

    /// Bumped whenever the navigator browse root changes via notification.
    /// SwiftUI body must read this to subscribe — that's how Backspace's goUp()
    /// triggers a body re-evaluation (workspace.validatedNavigatorWorkingDirectoryPath()
    /// is a plain method, not a @Published, so SwiftUI cannot observe it directly).
    @Published var navigatorEpoch: Int = 0

    let minTile: CGFloat = 48
    let maxTile: CGFloat = 180
    let gridSpacing: CGFloat = 10

    // Set by the view on every render cycle.
    var filesSnapshot: [CEWorkspaceFile] = []

    /// Before going up: save the path we're leaving so we can re-select it.
    var pendingReselectPath: String?

    weak var workspace: WorkspaceDocument?
    weak var editorManager: EditorManager?

    private var eventMonitors: [Any] = []
    private var notificationObservers: [NSObjectProtocol] = []

    // Double-tap tracking (avoids SwiftUI's 2-second single-vs-double wait).
    private var lastTapID: CEWorkspaceFile.ID?
    private var lastTapDate: Date?
    private let doubleTapInterval: TimeInterval = 0.35

    // MARK: Computed

    /// Number of grid columns, estimated from current grid width and tile size.
    var columnCount: Int {
        max(1, Int((gridWidth + gridSpacing) / (tileSize + gridSpacing)))
    }

    // MARK: Monitor lifecycle

    func installMonitors() {
        guard eventMonitors.isEmpty else { return }

        // Subscribe to the same notifications the tree controller uses, so SwiftUI
        // re-renders when the navigator browse root changes (Backspace/goUp(), places
        // dropdown, "reveal in navigator", etc.). Without this, the body never
        // re-evaluates after goUp() and the user must press another key to force it.
        let browseObs = NotificationCenter.default.addObserver(
            forName: .polymechNavigatorBrowseRootChanged,
            object: nil,
            queue: .main
        ) { [weak self] n in
            guard let self,
                  let doc = n.object as? WorkspaceDocument,
                  doc === self.workspace else { return }
            self.navigatorEpoch &+= 1
        }
        let revealObs = NotificationCenter.default.addObserver(
            forName: .polymechRevealFileInNavigator,
            object: nil,
            queue: .main
        ) { [weak self] n in
            guard let self,
                  let doc = n.object as? WorkspaceDocument,
                  doc === self.workspace else { return }
            self.navigatorEpoch &+= 1
        }
        notificationObservers.append(contentsOf: [browseObs, revealObs])

        let keyMon = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self, NSApp.isActive else { return event }
            // Never steal events from an active text field (filter bar, rename, etc.).
            if let fr = NSApp.keyWindow?.firstResponder as? NSTextView, fr.isFieldEditor {
                return event
            }
            let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
            guard !flags.contains(.command),
                  !flags.contains(.option),
                  !flags.contains(.control) else { return event }

            switch event.keyCode {
            case 36, 76:    // Return / Enter
                DispatchQueue.main.async { self.openSelected() }
                return nil
            case 51:        // Backspace → go up browse directory if one is active
                if self.workspace?.validatedNavigatorWorkingDirectoryPath() != nil {
                    DispatchQueue.main.async { self.goUp() }
                }
                return nil
            case 123:       // ← move one item left
                DispatchQueue.main.async { self.moveSelection(by: -1) }
                return nil
            case 124:       // → move one item right
                DispatchQueue.main.async { self.moveSelection(by: 1) }
                return nil
            case 125:       // ↓ move one grid row down
                DispatchQueue.main.async { self.moveSelection(by: self.columnCount) }
                return nil
            case 126:       // ↑ move one grid row up
                DispatchQueue.main.async { self.moveSelection(by: -self.columnCount) }
                return nil
            default:
                return event
            }
        }

        // Ctrl+scroll → zoom tile size.
        let scrollMon = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) { [weak self] event in
            guard let self,
                  event.modifierFlags.contains(.control),
                  event.window === NSApp.keyWindow else { return event }
            let delta = event.scrollingDeltaY
            DispatchQueue.main.async {
                self.tileSize = clampCG(self.tileSize + delta * 1.5, self.minTile, self.maxTile)
            }
            return nil
        }

        // Mouse X1 (button 3) → go up; X2 (button 4) → drill in.
        // Don't require event.window to be non-nil — some mice deliver extra-button
        // events with a nil window even when the app is key.
        let mouseMon = NSEvent.addLocalMonitorForEvents(matching: .otherMouseDown) { [weak self] event in
            guard let self, NSApp.isActive else { return event }
            switch event.buttonNumber {
            case 3:     // X1 / Back button
                if self.workspace?.validatedNavigatorWorkingDirectoryPath() != nil {
                    DispatchQueue.main.async { self.goUp() }
                    return nil
                }
            case 4:     // X2 / Forward button
                DispatchQueue.main.async {
                    if let id = self.selectedID,
                       let file = self.filesSnapshot.first(where: { $0.id == id }),
                       file.isFolder {
                        self.workspace?.setNavigatorWorkingDirectoryURL(file.url)
                    }
                }
                return nil
            default:
                break
            }
            return event
        }

        [keyMon, scrollMon, mouseMon].compactMap { $0 }.forEach { eventMonitors.append($0) }
    }

    func removeMonitors() {
        eventMonitors.forEach { NSEvent.removeMonitor($0) }
        eventMonitors = []
        notificationObservers.forEach { NotificationCenter.default.removeObserver($0) }
        notificationObservers = []
    }

    // MARK: Actions

    /// Tap handler — fires immediately; distinguishes single/double without any wait.
    func handleTap(on file: CEWorkspaceFile) {
        let now = Date()
        let isDouble = lastTapID == file.id
            && lastTapDate.map { now.timeIntervalSince($0) < doubleTapInterval } == true
        lastTapID = file.id
        lastTapDate = now

        if isDouble {
            lastTapID = nil        // reset so next tap counts fresh
            lastTapDate = nil
            selectAndOpen(file, permanent: true)
        } else {
            selectAndOpen(file, permanent: false)
        }
    }

    func selectAndOpen(_ file: CEWorkspaceFile, permanent: Bool) {
        selectedID = file.id
        if file.isFolder {
            // Folders are navigation targets; clear the file-level selection context so
            // the chat doesn't treat a folder you're browsing into as a chat context item
            // (parity with tree mode: entering a folder deselects the previous row).
            DispatchQueue.main.async { [weak self] in
                self?.workspace?.polymechWorkflow.updateNavigatorSelection([])
            }
            if permanent { workspace?.setNavigatorWorkingDirectoryURL(file.url) }
        } else {
            // Mirror tree-mode outlineViewSelectionDidChange: propagate the tapped file
            // to chatSelectionPaths so it immediately appears in the chat filmstrip.
            let url = file.url
            DispatchQueue.main.async { [weak self] in
                self?.workspace?.polymechWorkflow.updateNavigatorSelection([url])
            }
            editorManager?.activeEditor.openTab(file: file, asTemporary: !permanent)
        }
    }

    func openSelected() {
        // Mirror moveSelection's fallback: if nothing is selected (or the
        // selection is stale after a directory change), pick the first item.
        if selectedID == nil || !filesSnapshot.contains(where: { $0.id == selectedID }) {
            selectedID = filesSnapshot.first?.id
        }
        guard let id = selectedID,
              let file = filesSnapshot.first(where: { $0.id == id }) else { return }
        if file.isFolder {
            workspace?.setNavigatorWorkingDirectoryURL(file.url)
        } else {
            editorManager?.activeEditor.openTab(file: file, asTemporary: false)
        }
    }

    /// Go up one browse directory, remembering the folder we're leaving so it
    /// can be re-selected once the grid reloads (mirrors tree's reselectFolderLeftBehind).
    func goUp() {
        guard let currentPath = workspace?.validatedNavigatorWorkingDirectoryPath() else { return }
        pendingReselectPath = currentPath       // absolute path of the folder being left
        workspace?.goUpNavigatorWorkingDirectory()
    }

    func moveSelection(by delta: Int) {
        let list = filesSnapshot
        guard !list.isEmpty else { return }
        let count = list.count
        if let cur = selectedID, let idx = list.firstIndex(where: { $0.id == cur }) {
            // Wrap around at boundaries so ↑ from the top goes to the last item
            // (and ↓ from the bottom goes to the first), matching common file-manager UX.
            let raw = idx + delta
            let wrapped = ((raw % count) + count) % count
            selectedID = list[wrapped].id
        } else {
            selectedID = list[delta >= 0 ? 0 : count - 1].id
        }
    }

    /// Called by the view after the file list updates; applies any pending re-selection.
    func applyPendingReselect(in files: [CEWorkspaceFile]) {
        guard let path = pendingReselectPath else { return }
        let want = URL(fileURLWithPath: path).standardizedFileURL.path
        if let match = files.first(where: { $0.url.standardizedFileURL.path == want }) {
            selectedID = match.id
            pendingReselectPath = nil
        }
    }
}

// MARK: - Main grid view

struct ProjectNavigatorThumbView: View {

    @EnvironmentObject var workspace: WorkspaceDocument
    @EnvironmentObject var editorManager: EditorManager

    @StateObject private var vm = ThumbViewModel()

    private var columns: [GridItem] {
        [GridItem(.adaptive(minimum: vm.tileSize, maximum: vm.tileSize + 24), spacing: vm.gridSpacing)]
    }

    // MARK: File list

    private var files: [CEWorkspaceFile] {
        guard let wfm = workspace.workspaceFileManager else { return [] }
        let rootPath: String
        if let browse = workspace.validatedNavigatorWorkingDirectoryPath() {
            rootPath = browse
        } else {
            rootPath = wfm.folderUrl.path
        }
        guard let root = wfm.getFile(rootPath),
              let children = wfm.childrenOfFile(root) else { return [] }

        let sorted: [CEWorkspaceFile]
        if workspace.sortFoldersOnTop {
            sorted = children.sorted {
                ($0.isFolder && !$1.isFolder)
                || ($0.isFolder == $1.isFolder
                    && $0.name.localizedCompare($1.name) == .orderedAscending)
            }
        } else {
            sorted = children.sorted { $0.name.localizedCompare($1.name) == .orderedAscending }
        }
        let q = workspace.navigatorFilter.trimmingCharacters(in: .whitespacesAndNewlines)
        return q.isEmpty ? sorted : sorted.filter { $0.name.localizedCaseInsensitiveContains(q) }
    }

    // MARK: Body

    var body: some View {
        // Reading vm.navigatorEpoch here makes SwiftUI subscribe to it; bumping it
        // (in our notification observer) forces a body re-eval. Without this,
        // Backspace's goUp() updates workspace state SwiftUI cannot observe
        // (it's a method, not a @Published), so the file list stays stale
        // until an unrelated state change (e.g. arrow key) forces a redraw.
        let _ = vm.navigatorEpoch
        let fileList = files
        return GeometryReader { geo in
            ScrollView {
                LazyVGrid(columns: columns, spacing: 12) {
                    ForEach(fileList, id: \.id) { file in
                        ThumbCell(
                            file: file,
                            isSelected: vm.selectedID == file.id,
                            tileSize: vm.tileSize
                        )
                        .contentShape(Rectangle())
                        .onTapGesture {
                            vm.handleTap(on: file)
                        }
                        // Drag source — same pasteboard type as the outline view's
                        // pasteboardWriterForItem, so PolymechChatShellDropWKWebView
                        // picks up the URL and forwards it to the filmstrip via addContextPaths.
                        .onDrag {
                            vm.selectedID = file.id
                            workspace.polymechWorkflow.updateNavigatorSelection(
                                file.isFolder ? [] : [file.url]
                            )
                            return NSItemProvider(object: file.url as NSURL)
                        }
                    }
                }
                .padding(10)
                .animation(.easeInOut(duration: 0.15), value: vm.tileSize)
            }
            .onChange(of: geo.size.width) { vm.gridWidth = geo.size.width }
            .onAppear { vm.gridWidth = geo.size.width }
        }
        // Pinch-to-zoom
        .gesture(
            MagnifyGesture()
                .onChanged { v in
                    vm.tileSize = clampCG(vm.tileSize * v.magnification, vm.minTile, vm.maxTile)
                }
        )
        // Sync file list into VM and apply pending re-selection after go-up.
        // Also auto-select the first item the moment files are available so
        // Enter/Backspace work without requiring an arrow-key press first
        // (files often load after onAppear, so we do this here too).
        .onChange(of: fileList.map(\.id)) {
            vm.filesSnapshot = fileList
            // Restore selection after go-up (re-selects the folder we just left).
            vm.applyPendingReselect(in: fileList)
            // After a directory change selectedID is often stale (points to a file
            // in the old directory). If it doesn't exist in the new list, pick first.
            let isStale = vm.selectedID.map { id in !fileList.contains(where: { $0.id == id }) } ?? true
            if isStale, let first = fileList.first {
                vm.selectedID = first.id
            }
        }
        .onAppear {
            vm.workspace    = workspace
            vm.editorManager = editorManager
            vm.filesSnapshot = fileList
            if vm.selectedID == nil {
                vm.selectedID = fileList.first?.id
            }
            vm.installMonitors()
        }
        .onDisappear {
            vm.removeMonitors()
        }
    }
}

// MARK: - Thumbnail cell

struct ThumbCell: View {

    let file: CEWorkspaceFile
    let isSelected: Bool
    let tileSize: CGFloat

    @State private var thumbnail: NSImage?

    private static let imageExts: Set<String> = [
        "jpg", "jpeg", "png", "gif", "heic", "heif",
        "tiff", "tif", "bmp", "webp", "ico",
    ]

    var body: some View {
        VStack(spacing: 4) {
            ZStack {
                RoundedRectangle(cornerRadius: 6)
                    .fill(isSelected
                          ? Color.accentColor.opacity(0.2)
                          : Color(nsColor: .quaternaryLabelColor).opacity(0.3))
                    .frame(width: tileSize, height: tileSize)

                thumb
                    .frame(width: tileSize - 10, height: tileSize - 10)
                    .clipShape(RoundedRectangle(cornerRadius: 4))
            }

            Text(file.name)
                .font(.system(size: Swift.max(9, tileSize * 0.135)))
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .foregroundStyle(isSelected ? Color.accentColor : Color.primary)
                .frame(maxWidth: tileSize + 16)
        }
        .task(id: file.url.path) { thumbnail = await loadThumb() }
    }

    @ViewBuilder private var thumb: some View {
        if let img = thumbnail {
            Image(nsImage: img).resizable().scaledToFit()
        } else {
            Image(nsImage: NSWorkspace.shared.icon(forFile: file.url.path))
                .resizable().scaledToFit()
        }
    }

    private func loadThumb() async -> NSImage? {
        guard !file.isFolder,
              Self.imageExts.contains(file.url.pathExtension.lowercased()) else { return nil }
        let url = file.url
        let cap = Int(tileSize * 2 * 2)   // 2× tile at 2× DPI → typically 256–640 px
        return await Task(priority: .utility) {
            ThumbImageLoader.load(at: url, maxPixels: cap)
        }.value
    }
}

// MARK: - Fast thumbnail loader

enum ThumbImageLoader {
    static func load(at url: URL, maxPixels: Int) -> NSImage? {
        let src = CGImageSourceCreateWithURL(url as CFURL,
            [kCGImageSourceShouldCache: false] as CFDictionary)
        guard let src else { return NSImage(contentsOf: url) }
        let opts: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform:  true,
            kCGImageSourceThumbnailMaxPixelSize:         Swift.max(64, maxPixels),
        ]
        if let cg = CGImageSourceCreateThumbnailAtIndex(src, 0, opts as CFDictionary) {
            return NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
        }
        return NSImage(contentsOf: url)
    }
}

// MARK: - Clamp helpers

private func clampCG(_ v: CGFloat, _ lo: CGFloat, _ hi: CGFloat) -> CGFloat {
    Swift.max(lo, Swift.min(hi, v))
}
private func clampInt(_ v: Int, _ lo: Int, _ hi: Int) -> Int {
    Swift.max(lo, Swift.min(hi, v))
}
