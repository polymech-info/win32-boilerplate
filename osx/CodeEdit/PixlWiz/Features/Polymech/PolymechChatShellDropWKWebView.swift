//
//  PolymechChatShellDropWKWebView.swift
//  CodeEdit
//
//  macOS counterpart to Win32 `ChatShellDropTarget` + `CF_HDROP` (`ChatWebPanel.cpp`): drags from
//  AppKit (Project Navigator `NSURL` pasteboard, Finder, …) do not populate WKWebView’s DOM
//  `DataTransfer`, so `installContextFileDrop` in chat-next never sees paths. Accept file drops at
//  the NSView layer and forward POSIX paths to the host bridge (same contract as `addContextPaths`).
//

import AppKit
import WebKit

final class PolymechChatShellDropWKWebView: WKWebView {
    /// Called on the main thread with absolute file paths from the pasteboard.
    var onNativePathsDropped: (([String]) -> Void)?

    override init(frame frameRect: NSRect, configuration: WKWebViewConfiguration) {
        super.init(frame: frameRect, configuration: configuration)
        Self.registerFileDragTypes(on: self)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        Self.registerFileDragTypes(on: self)
    }

    private static func registerFileDragTypes(on view: NSView) {
        view.registerForDraggedTypes([
            .fileURL,
            NSPasteboard.PasteboardType("NSFilenamesPboardType"),
            .URL,
        ])
    }

    private static func filePaths(from pasteboard: NSPasteboard) -> [String] {
        var out: [String] = []
        var seenLower = Set<String>()
        func add(_ raw: String) {
            let t = raw.trimmingCharacters(in: .whitespacesAndNewlines)
            if t.isEmpty { return }
            let k = t.lowercased()
            if seenLower.contains(k) { return }
            seenLower.insert(k)
            out.append(t)
        }

        if let list = pasteboard.propertyList(forType: NSPasteboard.PasteboardType("NSFilenamesPboardType")) as? [String] {
            for s in list { add(s) }
        }

        let classes: [AnyClass] = [NSURL.self]
        if let objects = pasteboard.readObjects(forClasses: classes, options: [.urlReadingFileURLsOnly: true]) {
            for obj in objects {
                if let u = obj as? URL, u.isFileURL { add(u.path) }
            }
        } else if let objects = pasteboard.readObjects(forClasses: classes, options: nil) {
            for obj in objects {
                if let u = obj as? URL, u.isFileURL { add(u.path) }
            }
        }

        return out
    }

    private func pathsIfFileDrop(_ info: NSDraggingInfo) -> [String]? {
        let paths = Self.filePaths(from: info.draggingPasteboard)
        return paths.isEmpty ? nil : paths
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        if pathsIfFileDrop(sender) != nil { return .copy }
        return super.draggingEntered(sender)
    }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        if pathsIfFileDrop(sender) != nil { return .copy }
        return super.draggingUpdated(sender)
    }

    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool {
        if pathsIfFileDrop(sender) != nil { return true }
        return super.prepareForDragOperation(sender)
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        if let paths = pathsIfFileDrop(sender) {
            onNativePathsDropped?(paths)
            return true
        }
        return super.performDragOperation(sender)
    }
}
