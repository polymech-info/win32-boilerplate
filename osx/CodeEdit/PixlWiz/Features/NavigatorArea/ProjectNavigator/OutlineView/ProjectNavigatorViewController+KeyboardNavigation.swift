//
//  ProjectNavigatorViewController+KeyboardNavigation.swift
//  CodeEdit
//
//  Return — on “..” go up; on a folder set persisted browse root (list becomes .. + that folder’s children).
//  Backspace — go up the working directory first; else move selection to parent in the tree.
//  Command+Delete — Move to Trash (no menu key equivalent before).
//  Command+C / Command+V — copy file URLs, paste with name_2 / name_3 / … in the target directory (or text → new file from clipboard).
//

import AppKit
import SwiftUI

extension ProjectNavigatorViewController {
    /// Handles Return / Backspace, and when a row (not a field editor) is first responder, Command shortcuts for copy / paste / trash.
    func installNavigatorKeyboardEventMonitor() {
        guard navigatorKeyDownEventMonitor == nil, outlineView != nil else { return }
        navigatorKeyDownEventMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self, let o = self.outlineView else { return event }
            // Thumb mode installs its own monitor — don't compete with it.
            guard Settings.shared.preferences.general.navigatorLayout == .tree else { return event }
            guard event.window == o.window else { return event }
            let first = o.window?.firstResponder
            guard Self.responderInNavigatorKeyLoop(outline: o, firstResponder: first) else { return event }
            if let tv = first as? NSTextView, tv.isFieldEditor, let v = first as? NSView, v.isDescendant(of: o) {
                return event
            }
            let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
            if flags.contains(.option) || flags.contains(.control) { return event }

            // Command+Delete, Command+C, Command+V (not Shift+Command to leave Undo, etc. free)
            if flags.contains(.command), !flags.contains(.shift) {
                let ch = (event.charactersIgnoringModifiers ?? "").lowercased()
                if ch == "c" {
                    self.navigatorCommandCopy()
                    return nil
                }
                if ch == "v" {
                    self.navigatorCommandPaste()
                    return nil
                }
                if event.keyCode == 51 || event.keyCode == 117 {
                    self.navigatorCommandMoveToTrash()
                    return nil
                }
            }
            if flags.contains(.command) { return event }
            if flags.contains(.shift) { return event }

            switch event.keyCode {
            case 36, 76:
                self.navigatorHandleReturnKey()
                return nil
            case 51:
                self.navigatorHandleBackspaceKey()
                return nil
            default:
                return event
            }
        }
    }

    fileprivate static func responderInNavigatorKeyLoop(outline: NSOutlineView, firstResponder: NSResponder?) -> Bool {
        if firstResponder === outline { return true }
        guard let v = firstResponder as? NSView else { return false }
        return v.isDescendant(of: outline)
    }

    /// Return / Enter — on “..”: go up; on a folder: set persisted browse root (top-level list updates); on a file: open.
    func navigatorHandleReturnKey() {
        guard outlineView.selectedRowIndexes.count == 1,
              let item = outlineView.item(atRow: outlineView.selectedRow) as? CEWorkspaceFile else { return }

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

    /// Backspace — if a panel working directory is set, go up (MC-style); else select the parent row in the outline.
    func navigatorHandleBackspaceKey() {
        if workspace?.validatedNavigatorWorkingDirectoryPath() != nil {
            prepareNavigatorReselectFolderAfterNextReload()
            workspace?.goUpNavigatorWorkingDirectory()
            return
        }
        guard outlineView.selectedRowIndexes.count == 1,
              let item = outlineView.item(atRow: outlineView.selectedRow) as? CEWorkspaceFile,
              let parent = item.parent as? CEWorkspaceFile else { return }

        let row = outlineView.row(forItem: parent)
        guard row >= 0 else { return }

        shouldSendSelectionUpdate = false
        outlineView.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        shouldSendSelectionUpdate = true
        outlineView.scrollRowToVisible(row)
    }

    // MARK: - Command shortcuts (File ▸ … parity)

    private func navigatorSelectedFilesExcludingUpRow() -> [CEWorkspaceFile] {
        outlineView.selectedRowIndexes
            .compactMap { outlineView.item(atRow: $0) as? CEWorkspaceFile }
            .filter { !$0.isProjectNavigatorUpRow }
    }

    /// Directory that receives a paste: single selected folder, else the parent of the first selected row, else the visible browse/root folder.
    private func navigatorPasteTargetDirectoryURL() -> URL? {
        guard let w = workspace, let wfm = w.workspaceFileManager else { return nil }
        let selected = navigatorSelectedFilesExcludingUpRow()
        if let one = selected.first, selected.count == 1, one.isFolder { return one.url }
        if let first = selected.first { return first.url.deletingLastPathComponent() }
        if let p = w.validatedNavigatorWorkingDirectoryPath() { return URL(fileURLWithPath: p, isDirectory: true) }
        return wfm.folderUrl
    }

    fileprivate func navigatorRefreshAfterFileOperation() {
        outlineView.reloadData()
        filteredContentChildren.removeAll()
    }

    private func navigatorCommandCopy() {
        let items = navigatorSelectedFilesExcludingUpRow()
        guard !items.isEmpty else {
            NSSound.beep()
            return
        }
        let urls: [URL] = items.map(\.url)
        let pboard = NSPasteboard.general
        pboard.clearContents()
        pboard.writeObjects(urls as [NSURL])
    }

    private func navigatorCommandPaste() {
        guard let destParent = navigatorPasteTargetDirectoryURL(),
            let wfm = workspace?.workspaceFileManager
        else {
            NSSound.beep()
            return
        }
        let pboard = NSPasteboard.general
        if let raw = pboard.readObjects(forClasses: [NSURL.self], options: nil), !raw.isEmpty {
            let fileURLs: [URL] = raw.compactMap { o in
                if let u = o as? URL { return u }
                if let u = o as? NSURL { return u as URL }
                return nil
            }
            if !fileURLs.isEmpty {
                do {
                    for u in fileURLs {
                        try wfm.copyResolvingNameCollision(from: u.standardizedFileURL, toDestinationDirectory: destParent)
                    }
                } catch {
                    let alert = NSAlert(error: error)
                    alert.addButton(withTitle: "Dismiss")
                    alert.runModal()
                }
                navigatorRefreshAfterFileOperation()
                return
            }
        }
        if let s = pboard.string(forType: .string), !s.isEmpty,
            let m = outlineView.menu as? ProjectNavigatorMenu,
            let parent = wfm.getFile(destParent.path(percentEncoded: false)) {
            m.workspace = workspace
            m.item = parent
            m.newFileFromClipboard()
            navigatorRefreshAfterFileOperation()
        } else {
            NSSound.beep()
        }
    }

    private func navigatorCommandMoveToTrash() {
        guard let wfm = workspace?.workspaceFileManager else {
            NSSound.beep()
            return
        }
        let items = navigatorSelectedFilesExcludingUpRow().filter { $0.url != wfm.folderUrl }
        guard !items.isEmpty else {
            NSSound.beep()
            return
        }
        do {
            try items.forEach { item in
                withAnimation {
                    self.editor?.closeTab(file: item)
                }
                guard FileManager.default.fileExists(atPath: item.url.path) else { return }
                try workspace?.workspaceFileManager?.trash(file: item)
            }
            navigatorRefreshAfterFileOperation()
        } catch {
            let alert = NSAlert(error: error)
            alert.addButton(withTitle: "Dismiss")
            alert.runModal()
        }
    }
}
