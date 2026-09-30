//
//  WorkspaceDocument+PolymechChatHost.swift
//  CodeEdit
//
//  Parity with Win32 `ChatWebPanel` host messages that touch paths / Explorer
//  (`selectPathInExplorer` → `SelectPathInFileTree`, folder chip → `openFolderInExplorer`).
//

import AppKit
import Foundation

extension WorkspaceDocument {
    /// When the navigator is in MC browse (folder) mode, move the browse root so `path` appears in the
    /// top-level listing — same idea as Win32 `CMainFrame::SelectPathInFileTree` (navigate to parent, then select).
    @MainActor
    func polymechPrepareNavigatorToShow(path: String) {
        guard let root = workspaceFileManager?.folderUrl else { return }
        let norm = URL(fileURLWithPath: path).standardizedFileURL
        let rootPath = root.standardizedFileURL.path
        let p = norm.path
        guard p == rootPath || p.hasPrefix(rootPath + "/") else { return }
        guard validatedNavigatorWorkingDirectoryPath() != nil else { return }

        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: p, isDirectory: &isDir) else { return }

        if p == rootPath, isDir.boolValue {
            setNavigatorWorkingDirectoryURL(nil)
            return
        }

        let parentURL = norm.deletingLastPathComponent()
        var parentIsDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: parentURL.path, isDirectory: &parentIsDir),
              parentIsDir.boolValue else { return }
        guard parentURL.path == rootPath || parentURL.path.hasPrefix(rootPath + "/") else { return }
        setNavigatorWorkingDirectoryURL(parentURL)
    }
}
