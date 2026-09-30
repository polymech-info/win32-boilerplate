//
//  ProjectNavigatorViewController+NSOutlineViewDataSource.swift
//  CodeEdit
//
//  Created by Khan Winter on 7/13/24.
//

import AppKit

extension ProjectNavigatorViewController: NSOutlineViewDataSource {
    /// The workspace root or the browse folder is not always a visible row in panel mode; the filter cache is keyed by that folder for “no results” — call this after reload when a filter is active.
    func primeNavigatorFilterAnchorIfNeeded() {
        guard let workspace, workspace.sourceControlFilter || !filterIsEmpty,
              let anchor = navigatorFilterAnchorFile() else { return }
        if filteredContentChildren[anchor] != nil { return }
        _ = getOutlineViewItems(for: anchor)
    }

    /// Retrieves the children of a given item for the outline view, applying the current filter if necessary.
    private func getOutlineViewItems(for item: CEWorkspaceFile) -> [CEWorkspaceFile] {
        if item.isProjectNavigatorUpRow { return [] }
        if let cachedChildren = filteredContentChildren[item] {
            return cachedChildren
                .sorted { lhs, rhs in
                    workspace?.sortFoldersOnTop == true ? lhs.isFolder && !rhs.isFolder : lhs.name < rhs.name
                }
        }

        if let workspace, let children = workspace.workspaceFileManager?.childrenOfFile(item) {
            if !workspace.navigatorFilter.isEmpty || workspace.sourceControlFilter {
                let filteredChildren = children.filter {
                    fileSearchMatches(
                        workspace.navigatorFilter,
                        for: $0,
                        sourceControlFilter: workspace.sourceControlFilter
                    )
                }

                filteredContentChildren[item] = filteredChildren
                return filteredChildren
            }

            return children
                .sorted { lhs, rhs in
                    workspace.sortFoldersOnTop ? lhs.isFolder && !rhs.isFolder : lhs.name < rhs.name
                }
        }

        return []
    }

    func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
        if let item = item as? CEWorkspaceFile {
            return getOutlineViewItems(for: item).count
        }
        return content.count
    }

    func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
        if let item = item as? CEWorkspaceFile {
            return getOutlineViewItems(for: item)[index]
        }
        return content[index]
    }

    func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
        if let item = item as? CEWorkspaceFile {
            if item.isProjectNavigatorUpRow { return false }
            return item.isFolder
        }
        return false
    }

    /// Write dragged file(s) to pasteboard
    func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> NSPasteboardWriting? {
        guard let fileItem = item as? CEWorkspaceFile, !fileItem.isProjectNavigatorUpRow else { return nil }
        return fileItem.url as NSURL
    }

    /// Called just before the drag session begins.
    ///
    /// AppKit may deselect items or shift focus during the drag, which fires
    /// `outlineViewSelectionDidChange` → `updateNavigatorSelection([])` *before*
    /// the drop target's `performDragOperation` runs.  That races with
    /// `PolymechChatShellDropWKWebView.onNativePathsDropped` and can cause the
    /// chat to push an empty-context `pushContext` whose key then matches the
    /// subsequent (correct) push — so the filmstrip appears to need a folder
    /// navigation to refresh.
    ///
    /// Fix: snapshot the exact dragged URLs here (while the selection is still
    /// intact) and push them as `navigatorSelectionURLs` so the chat already
    /// has the correct context before any selection bookkeeping fires.
    func outlineView(
        _ outlineView: NSOutlineView,
        draggingSession session: NSDraggingSession,
        willBeginAt screenPoint: NSPoint,
        forItems draggedItems: [Any]
    ) {
        let urls = draggedItems.compactMap { ($0 as? CEWorkspaceFile)?.url }
            .filter { !($0.path.isEmpty) }
        guard !urls.isEmpty else { return }
        workspace?.polymechWorkflow.updateNavigatorSelection(urls)
    }

    /// Declare valid drop target
    func outlineView(
        _ outlineView: NSOutlineView,
        validateDrop info: NSDraggingInfo,
        proposedItem item: Any?,
        proposedChildIndex index: Int
    ) -> NSDragOperation {
        guard let fileItem = item as? CEWorkspaceFile, !fileItem.isProjectNavigatorUpRow else { return [] }
        // -1 index indicates that we are hovering over a row in outline view (folder or file)
        if index == -1 {
            if !fileItem.isFolder {
                outlineView.setDropItem(fileItem.parent, dropChildIndex: index)
            }
            return info.draggingSourceOperationMask == .copy ? .copy : .move
        }
        return []
    }

    /// Handle successful or unsuccessful drop
    func outlineView(
        _ outlineView: NSOutlineView,
        acceptDrop info: NSDraggingInfo,
        item: Any?,
        childIndex index: Int
    ) -> Bool {
        guard let pasteboardItems = info.draggingPasteboard.readObjects(forClasses: [NSURL.self]) else { return false }
        let fileItemURLS = pasteboardItems.compactMap { $0 as? URL }

        guard let fileItemDestination = item as? CEWorkspaceFile, !fileItemDestination.isProjectNavigatorUpRow else { return false }
        let destParentURL = fileItemDestination.url

        for fileItemURL in fileItemURLS {
            let destURL = destParentURL.appending(path: fileItemURL.lastPathComponent)
            // cancel dropping file item on self or in parent directory
            if fileItemURL == destURL || fileItemURL == destParentURL {
                return false
            }

            // Needs to come before call to .removeItem or else race condition occurs
            var srcFileItem: CEWorkspaceFile? = workspace?.workspaceFileManager?.getFile(fileItemURL.path)
            // If srcFileItem is nil, fileItemUrl is an external file url.
            if srcFileItem == nil {
                srcFileItem = CEWorkspaceFile(url: URL(fileURLWithPath: fileItemURL.path))
            }

            guard let srcFileItem else {
                return false
            }

            if CEWorkspaceFile.fileManager.fileExists(atPath: destURL.path) {
                let shouldReplace = replaceFileDialog(fileName: fileItemURL.lastPathComponent)
                guard shouldReplace else {
                    return false
                }
                do {
                    try CEWorkspaceFile.fileManager.removeItem(at: destURL)
                } catch {
                    fatalError(error.localizedDescription)
                }
            }
            if info.draggingSourceOperationMask == .copy {
                self.copyFile(file: srcFileItem, to: destURL)
            } else {
                self.moveFile(file: srcFileItem, to: destURL)
            }
        }
        return true
    }

    func replaceFileDialog(fileName: String) -> Bool {
        let alert = NSAlert()
        alert.messageText = """
        A file or folder with the name \(fileName) already exists in the destination folder. Do you want to replace it?
        """
        alert.informativeText = "This action is irreversible!"
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Replace")
        alert.addButton(withTitle: "Cancel")
        return alert.runModal() == .alertFirstButtonReturn
    }
}
