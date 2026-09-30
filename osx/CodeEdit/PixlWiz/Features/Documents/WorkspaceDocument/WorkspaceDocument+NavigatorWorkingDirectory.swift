//
//  WorkspaceDocument+NavigatorWorkingDirectory.swift
//  CodeEdit
//
//  Midnight Commander–style “current folder” for Polymech tools + chat, persisted per workspace.
//

import Foundation

extension WorkspaceDocument {
    /// Absolute path from workspace state, or `nil` if not set / removed.
    var navigatorWorkingDirectoryPath: String? {
        getFromWorkspaceState(.navigatorWorkingDirectoryPath) as? String
    }

    /// Returns the path only if it still exists and is a directory under the workspace root.
    func validatedNavigatorWorkingDirectoryPath() -> String? {
        guard let p = navigatorWorkingDirectoryPath, !p.isEmpty else { return nil }
        guard let root = workspaceFileManager?.folderUrl else { return nil }
        let rootPath = root.standardizedFileURL.path
        guard p == rootPath || p.hasPrefix(rootPath + "/") else { return nil }
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: p, isDirectory: &isDir), isDir.boolValue else { return nil }
        return p
    }

    /// Set the panel working directory (persisted). Pass `nil` to clear. Posts ``Notification.Name.polymechNavigatorContextChanged``.
    func setNavigatorWorkingDirectoryURL(_ url: URL?) {
        guard let root = workspaceFileManager?.folderUrl else { return }
        let rootPath = root.standardizedFileURL.path
        if let url {
            let p = url.standardizedFileURL.path
            guard p == rootPath || p.hasPrefix(rootPath + "/") else { return }
            var isDir: ObjCBool = false
            guard FileManager.default.fileExists(atPath: p, isDirectory: &isDir), isDir.boolValue else { return }
            addToWorkspaceState(key: .navigatorWorkingDirectoryPath, value: p)
        } else {
            addToWorkspaceState(key: .navigatorWorkingDirectoryPath, value: nil)
        }
        NotificationCenter.default.post(name: .polymechNavigatorContextChanged, object: self)
        NotificationCenter.default.post(name: .polymechNavigatorBrowseRootChanged, object: self)
    }

    /// Step “up” to the parent of the current working directory, or clear at workspace root.
    func goUpNavigatorWorkingDirectory() {
        guard let root = workspaceFileManager?.folderUrl else { return }
        let rootPath = root.standardizedFileURL.path
        guard let current = validatedNavigatorWorkingDirectoryPath() else { return }
        if current == rootPath {
            setNavigatorWorkingDirectoryURL(nil)
            return
        }
        let parent = URL(fileURLWithPath: current).deletingLastPathComponent().path
        if parent == rootPath {
            setNavigatorWorkingDirectoryURL(URL(fileURLWithPath: parent, isDirectory: true))
            return
        }
        if parent.hasPrefix(rootPath + "/") {
            var isDir: ObjCBool = false
            guard FileManager.default.fileExists(atPath: parent, isDirectory: &isDir), isDir.boolValue else {
                setNavigatorWorkingDirectoryURL(nil)
                return
            }
            setNavigatorWorkingDirectoryURL(URL(fileURLWithPath: parent, isDirectory: true))
        } else {
            setNavigatorWorkingDirectoryURL(nil)
        }
    }
}
