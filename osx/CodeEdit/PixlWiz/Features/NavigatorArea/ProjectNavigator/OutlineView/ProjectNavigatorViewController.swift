//
//  OutlineViewController.swift
//  CodeEdit
//
//  Created by Lukas Pistrol on 07.04.22.
//

import AppKit
import SwiftUI
import OSLog

/// A `NSViewController` that handles the **ProjectNavigatorView** in the **NavigatorArea**.
///
/// Adds a ``outlineView`` inside a ``scrollView`` which shows the folder structure of the
/// currently open project.
final class ProjectNavigatorViewController: NSViewController {
    static let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "",
        category: "ProjectNavigatorViewController"
    )

    var scrollView: NSScrollView!
    var outlineView: NSOutlineView!
    var noResultsLabel: NSTextField!

    /// Top-level rows: either `[workspace root]` in tree mode, or `[..] + children` of the persisted browse folder in panel mode.
    var content: [CEWorkspaceFile] {
        recomputeNavigatorContentIfNeeded()
        return navigatorContentRoots
    }

    private var navigatorContentCacheKey: String?
    private var navigatorContentRoots: [CEWorkspaceFile] = []
    var navigatorBrowseRootObserver: NSObjectProtocol?
    var polymechRevealFileObserver: NSObjectProtocol?

    /// After a browse-root reload, where to put selection and keyboard focus (set before `setNavigator` / `goUp`).
    private enum PendingNavigatorBrowseSelection {
        case none
        /// After entering a folder: select the `..` row and make the outline key.
        case focusUpRow
        /// After going up: re-select the folder we were just inside (its path in the new listing).
        case reselectFolderLeftBehind(path: String)
    }

    private var pendingNavigatorBrowseSelection: PendingNavigatorBrowseSelection = .none

    /// Call before `setNavigatorWorkingDirectoryURL` when **entering** a folder.
    func prepareNavigatorPanelFocusOnUpRowAfterNextReload() {
        pendingNavigatorBrowseSelection = .focusUpRow
    }

    /// Call before `goUpNavigatorWorkingDirectory` so we can re-select the folder being left.
    func prepareNavigatorReselectFolderAfterNextReload() {
        if let p = workspace?.validatedNavigatorWorkingDirectoryPath() {
            pendingNavigatorBrowseSelection = .reselectFolderLeftBehind(path: p)
        }
    }

    private func applyPendingNavigatorBrowseSelectionIfNeeded() {
        let work = pendingNavigatorBrowseSelection
        pendingNavigatorBrowseSelection = .none
        guard let outline = outlineView else { return }
        recomputeNavigatorContentIfNeeded()
        shouldSendSelectionUpdate = false
        switch work {
        case .none:
            shouldSendSelectionUpdate = true
            return
        case .focusUpRow:
            guard workspace?.validatedNavigatorWorkingDirectoryPath() != nil,
                  let first = content.first, first.isProjectNavigatorUpRow else {
                break
            }
            var r = outline.row(forItem: first)
            if r < 0, outline.numberOfRows > 0, let z = outline.item(atRow: 0) as? CEWorkspaceFile, z.isProjectNavigatorUpRow {
                r = 0
            }
            if r >= 0 {
                outline.selectRowIndexes(IndexSet(integer: r), byExtendingSelection: false)
                outline.scrollRowToVisible(r)
                outline.window?.makeFirstResponder(outline)
            }
        case .reselectFolderLeftBehind(let path):
            let want = URL(fileURLWithPath: path).standardizedFileURL.path
            for item in content where !item.isProjectNavigatorUpRow {
                guard item.url.standardizedFileURL.path == want else { continue }
                var r = outline.row(forItem: item)
                if r < 0 {
                    for row in 0..<outline.numberOfRows {
                        guard let c = outline.item(atRow: row) as? CEWorkspaceFile, !c.isProjectNavigatorUpRow,
                              c.url.standardizedFileURL.path == want else { continue }
                        r = row
                        break
                    }
                }
                if r >= 0 {
                    outline.selectRowIndexes(IndexSet(integer: r), byExtendingSelection: false)
                    outline.scrollRowToVisible(r)
                    outline.window?.makeFirstResponder(outline)
                }
                break
            }
        }
        shouldSendSelectionUpdate = true
    }

    func invalidateNavigatorContentCache() {
        navigatorContentCacheKey = nil
        navigatorContentRoots = []
    }

    private func recomputeNavigatorContentIfNeeded() {
        guard let w = workspace, let wfm = w.workspaceFileManager,
              let root = wfm.getFile(wfm.folderUrl.path) else {
            navigatorContentRoots = []
            navigatorContentCacheKey = "empty"
            return
        }
        let folderURL = wfm.folderUrl
        let rootPath = folderURL.standardizedFileURL.path
        guard let browse = w.validatedNavigatorWorkingDirectoryPath() else {
            if navigatorContentCacheKey == "tree" { return }
            navigatorContentRoots = [root]
            navigatorContentCacheKey = "tree"
            return
        }
        guard let browseFile = wfm.getFile(browse) else {
            if navigatorContentCacheKey == "tree" { return }
            navigatorContentRoots = [root]
            navigatorContentCacheKey = "tree"
            return
        }
        let up = Self.makeProjectNavigatorUpRow(
            browsePath: browse,
            workspaceRootPath: rootPath,
            workspaceRootURL: folderURL
        )
        let children = (wfm.childrenOfFile(browseFile) ?? []).sorted { lhs, rhs in
            w.sortFoldersOnTop ? (lhs.isFolder && !rhs.isFolder) : (lhs.name < rhs.name)
        }
        navigatorContentRoots = [up] + children
        navigatorContentCacheKey = "browse:\(browse)"
    }

    /// “No filter results” anchor: the folder whose top-level children we filter (workspace root in tree, browse folder in panel).
    func navigatorFilterAnchorFile() -> CEWorkspaceFile? {
        recomputeNavigatorContentIfNeeded()
        guard let w = workspace, let wfm = w.workspaceFileManager,
              let root = wfm.getFile(wfm.folderUrl.path) else { return nil }
        if let p = w.validatedNavigatorWorkingDirectoryPath(), let bf = wfm.getFile(p) {
            return bf
        }
        return root
    }

    var filteredContentChildren: [CEWorkspaceFile: [CEWorkspaceFile]] = [:]
    var expandedItems: Set<CEWorkspaceFile> = []

    weak var workspace: WorkspaceDocument?
    weak var editor: Editor?

    var iconColor: SettingsData.FileIconStyle = .color {
        willSet {
            if newValue != iconColor {
                outlineView?.reloadData()
            }
        }
    }

    var fileExtensionsVisibility: SettingsData.FileExtensionsVisibility = .showAll
    var shownFileExtensions: SettingsData.FileExtensions = .default
    var hiddenFileExtensions: SettingsData.FileExtensions = .default

    var rowHeight: Double = 22 {
        willSet {
            if newValue != rowHeight {
                outlineView.rowHeight = newValue
                outlineView.reloadData()
            }
        }
    }

    /// This helps determine whether or not to send an `openTab` when the selection changes.
    /// Used b/c the state may update when the selection changes, but we don't necessarily want
    /// to open the file a second time.
    var shouldSendSelectionUpdate: Bool = true

    var shouldReloadAfterDoneEditing: Bool = false

    /// Local `keyDown` monitor so Return/Backspace work when a row subview (not the outline) is first responder.
    var navigatorKeyDownEventMonitor: Any?

    var filterIsEmpty: Bool {
        workspace?.navigatorFilter.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == true
    }

    /// Setup the ``scrollView`` and ``outlineView``
    override func loadView() {
        self.scrollView = NSScrollView()
        self.scrollView.hasVerticalScroller = true
        self.view = scrollView

        self.outlineView = ProjectNavigatorNSOutlineView()
        (self.outlineView as? ProjectNavigatorNSOutlineView)?.projectNavigatorViewController = self
        self.outlineView.dataSource = self
        self.outlineView.delegate = self
        self.outlineView.autosaveExpandedItems = true
        self.outlineView.autosaveName = workspace?.workspaceFileManager?.folderUrl.path ?? ""
        self.outlineView.headerView = nil
        self.outlineView.menu = ProjectNavigatorMenu(self)
        self.outlineView.menu?.delegate = self
        self.outlineView.doubleAction = #selector(onItemDoubleClicked)
        self.outlineView.allowsMultipleSelection = true

        self.outlineView.setAccessibilityIdentifier("ProjectNavigator")
        self.outlineView.setAccessibilityLabel("Project Navigator")

        let column = NSTableColumn(identifier: .init(rawValue: "Cell"))
        column.title = "Cell"
        outlineView.addTableColumn(column)

        outlineView.setDraggingSourceOperationMask(.move, forLocal: false)
        outlineView.registerForDraggedTypes([.fileURL])

        scrollView.documentView = outlineView
        scrollView.contentView.automaticallyAdjustsContentInsets = false
        scrollView.contentView.contentInsets = .init(top: 10, left: 0, bottom: 0, right: 0)
        scrollView.scrollerStyle = .overlay
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true

        outlineView.reloadData()

        navigatorBrowseRootObserver = NotificationCenter.default.addObserver(
            forName: .polymechNavigatorBrowseRootChanged,
            object: nil,
            queue: .main
        ) { [weak self] n in
            guard let self, let doc = n.object as? WorkspaceDocument, doc === self.workspace else { return }
            self.reloadOutlineForBrowseRootChange()
        }

        polymechRevealFileObserver = NotificationCenter.default.addObserver(
            forName: .polymechRevealFileInNavigator,
            object: nil,
            queue: .main
        ) { [weak self] n in
            guard
                let self,
                let doc = n.object as? WorkspaceDocument, doc === self.workspace,
                let p = n.userInfo?["path"] as? String, !p.isEmpty
            else { return }
            self.select(by: .codeEditor(p), forcesReveal: true)
        }

        if outlineView.numberOfRows > 0 {
            applyDefaultFirstExpandForNavigator()
        }

        /// Get autosave expanded items.
        for row in 0..<outlineView.numberOfRows {
            if let item = outlineView.item(atRow: row) as? CEWorkspaceFile {
                if outlineView.isItemExpanded(item) {
                    expandedItems.insert(item)
                }
            }
        }

        /// "No Filter Results" label.
        noResultsLabel = NSTextField(labelWithString: "No Filter Results")
        noResultsLabel.isHidden = true
        noResultsLabel.font = NSFont.systemFont(ofSize: 16)
        noResultsLabel.textColor = NSColor.secondaryLabelColor
        outlineView.addSubview(noResultsLabel)
        noResultsLabel.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            noResultsLabel.centerXAnchor.constraint(equalTo: outlineView.centerXAnchor),
            noResultsLabel.centerYAnchor.constraint(equalTo: outlineView.centerYAnchor)
        ])

        installNavigatorKeyboardEventMonitor()
    }

    init() {
        super.init(nibName: nil, bundle: nil)
    }

    deinit {
        if let o = navigatorBrowseRootObserver {
            NotificationCenter.default.removeObserver(o)
            navigatorBrowseRootObserver = nil
        }
        if let o = polymechRevealFileObserver {
            NotificationCenter.default.removeObserver(o)
            polymechRevealFileObserver = nil
        }
        if let m = navigatorKeyDownEventMonitor {
            NSEvent.removeMonitor(m)
            navigatorKeyDownEventMonitor = nil
        }
        outlineView?.removeFromSuperview()
        scrollView?.removeFromSuperview()
        noResultsLabel?.removeFromSuperview()
    }

    required init?(coder: NSCoder) {
        fatalError()
    }

    /// Forces to reveal the selected file through the command regardless of the auto reveal setting
    @objc
    func revealFile(_ sender: Any) {
        updateSelection(itemID: workspace?.editorManager?.activeEditor.selectedTab?.file.id, forcesReveal: true)
    }

    /// Updates the selection of the ``outlineView`` whenever it changes.
    ///
    /// Most importantly when the `id` changes from an external view.
    /// - Parameter itemID: The id of the file or folder.
    /// - Parameter forcesReveal: The boolean to indicates whether or not it should force to reveal the selected file.
    func updateSelection(itemID: String?, forcesReveal: Bool = false) {
        guard let itemID else {
            outlineView.deselectRow(outlineView.selectedRow)
            return
        }
        self.select(by: .codeEditor(itemID), forcesReveal: forcesReveal)
    }

    /// Expand or collapse the folder on double click
    @objc
    private func onItemDoubleClicked() {
        /// If there are multiples items selected, don't do anything, just like in Xcode.
        guard outlineView.selectedRowIndexes.count == 1 else { return }

        guard let item = outlineView.item(atRow: outlineView.clickedRow) as? CEWorkspaceFile else { return }

        if item.isProjectNavigatorUpRow {
            prepareNavigatorReselectFolderAfterNextReload()
            workspace?.goUpNavigatorWorkingDirectory()
            return
        }
        if item.isFolder {
            prepareNavigatorPanelFocusOnUpRowAfterNextReload()
            workspace?.setNavigatorWorkingDirectoryURL(item.url)
        } else if Settings[\.navigation].navigationStyle == .openInTabs {
            workspace?.editorManager?.activeEditor.openTab(file: item, asTemporary: false)
        }
    }

    /// Get the appropriate color for the items icon depending on the users preferences.
    /// - Parameter item: The `FileItem` to get the color for
    /// - Returns: A `NSColor` for the given `FileItem`.
    private func color(for item: CEWorkspaceFile) -> NSColor {
        if !item.isFolder && iconColor == .color {
            return NSColor(item.iconColor)
        } else {
            return .secondaryLabelColor
        }
    }

    func handleFilterChange() {
        filteredContentChildren.removeAll()
        if let w = workspace {
            outlineView.autosaveExpandedItems = w.validatedNavigatorWorkingDirectoryPath() == nil
        }
        outlineView.reloadData()
        primeNavigatorFilterAnchorIfNeeded()

        guard let workspace else { return }

        /// If the filter is empty, show all items and restore the expanded state.
        if workspace.sourceControlFilter || !filterIsEmpty {
            outlineView.autosaveExpandedItems = false
            /// Expand from the first expandable top-level item (e.g. skip the “..” row in panel mode).
            for item in content where outlineView.isExpandable(item) {
                outlineView.expandItem(item, expandChildren: true)
                break
            }
        } else {
            restoreExpandedState()
            if workspace.validatedNavigatorWorkingDirectoryPath() == nil {
                outlineView.autosaveExpandedItems = true
            }
        }

        if let anchor = navigatorFilterAnchorFile(), let children = filteredContentChildren[anchor] {
            if children.isEmpty {
                noResultsLabel.isHidden = false
                outlineView.hideRows(at: IndexSet(integer: 0))
            } else {
                noResultsLabel.isHidden = true
            }
        }
    }

    /// Call after browse root changes (separate from selection-only context updates) so the top-level `..` + list reloads.
    func reloadOutlineForBrowseRootChange() {
        guard outlineView != nil, workspace != nil else { return }
        expandedItems.removeAll()
        if let w = workspace {
            outlineView.autosaveExpandedItems = w.validatedNavigatorWorkingDirectoryPath() == nil
        }
        invalidateNavigatorContentCache()
        handleFilterChange()
        applyPendingNavigatorBrowseSelectionIfNeeded()
    }

    fileprivate func applyDefaultFirstExpandForNavigator() {
        guard outlineView.numberOfRows > 0,
              let it = outlineView.item(atRow: 0) as? CEWorkspaceFile,
              outlineView.isExpandable(it) else { return }
        outlineView.expandItem(it, expandChildren: false)
    }

    /// Checks if the given filter matches the name of the item or any of its children.
    func fileSearchMatches(_ filter: String, for item: CEWorkspaceFile, sourceControlFilter: Bool) -> Bool {
        guard !filterIsEmpty || sourceControlFilter else {
            return true
        }
        if item.isProjectNavigatorUpRow { return false }

        if sourceControlFilter {
            if item.gitStatus != nil && item.gitStatus != GitStatus.none &&
                (filterIsEmpty || item.name.localizedCaseInsensitiveContains(filter)) {
                saveAllContentChildren(for: item)
                return true
            }
        } else if item.name.localizedCaseInsensitiveContains(filter) {
            saveAllContentChildren(for: item)
            return true
        }

        if let children = workspace?.workspaceFileManager?.childrenOfFile(item) {
            return children.contains { fileSearchMatches(filter, for: $0, sourceControlFilter: sourceControlFilter) }
        }

        return false
    }

    /// Saves all children of a given folder item to the filtered content cache.
    /// This is specially useful when the name of a folder matches the search.
    /// Just like in Xcode, this shows all the content of the folder.
    private func saveAllContentChildren(for item: CEWorkspaceFile) {
        guard item.isFolder, !item.isProjectNavigatorUpRow, filteredContentChildren[item] == nil else { return }

        if let children = workspace?.workspaceFileManager?.childrenOfFile(item) {
            filteredContentChildren[item] = children
            for child in children.filter({ $0.isFolder }) {
                saveAllContentChildren(for: child)
            }
        }
    }

    /// Restores the expanded state of items when finish searching.
    private func restoreExpandedState() {
        let copy = expandedItems
        for top in content where outlineView.isExpandable(top) {
            outlineView.collapseItem(top, collapseChildren: true)
        }

        for item in copy {
            expandParentsRecursively(of: item)
            outlineView.expandItem(item)
        }

        expandedItems = copy
    }

    /// Recursively expands all parent items of a given item in the outline view.
    /// The order of the items may get lost in the `expandedItems` set.
    /// This means that a children item might be expanded before its parent, causing it not to really expand.
    private func expandParentsRecursively(of item: CEWorkspaceFile) {
        if let parent = item.parent {
            expandParentsRecursively(of: parent)
            outlineView.expandItem(parent)
        }
    }

    private static func makeProjectNavigatorUpRow(
        browsePath: String,
        workspaceRootPath: String,
        workspaceRootURL: URL
    ) -> CEWorkspaceFile {
        let parentURL: URL
        if browsePath == workspaceRootPath {
            parentURL = workspaceRootURL
        } else {
            parentURL = URL(fileURLWithPath: browsePath).deletingLastPathComponent()
        }
        let id = "__pmNavUp:\(browsePath)"
        let f = CEWorkspaceFile(id: id, url: parentURL, changeType: nil, staged: nil)
        f.isProjectNavigatorUpRow = true
        return f
    }
}
