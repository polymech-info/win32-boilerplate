//
//  PmChatLocalMediaSchemeHandler.swift
//  CodeEdit
//
//  WKWebView cannot load arbitrary `file://` images from a bundled `chat.html` document (unlike Win32
//  WebView2 `SetVirtualHostNameToFolderMapping`). Serves workspace files under `pmasset://…` for chat UI.
//

import Foundation
import UniformTypeIdentifiers
import WebKit

/// Serves local files for `<img src>` rewritten to `pmasset://local?p=<percent-encoded posix path>`.
final class PmChatLocalMediaSchemeHandler: NSObject, WKURLSchemeHandler {
    /// Workspace root (`CEWorkspaceFileManager.folderUrl`); only paths under this directory are served.
    var workspaceRootURL: URL?

    func webView(_ webView: WKWebView, start urlSchemeTask: WKURLSchemeTask) {
        guard let url = urlSchemeTask.request.url else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 400))
            return
        }
        guard url.scheme?.lowercased() == "pmasset" else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 400))
            return
        }
        guard let items = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems,
              let rawP = items.first(where: { $0.name == "p" })?.value,
              let path = rawP.removingPercentEncoding?.trimmingCharacters(in: .whitespacesAndNewlines),
              !path.isEmpty,
              path.hasPrefix("/")
        else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 404))
            return
        }

        let resolved = URL(fileURLWithPath: path).standardizedFileURL
        let p = resolved.path

        guard let root = workspaceRootURL?.standardizedFileURL else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 403))
            return
        }
        let rootPath = root.path
        guard p == rootPath || p.hasPrefix(rootPath + "/") else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 403))
            return
        }

        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: p, isDirectory: &isDir), !isDir.boolValue else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 404))
            return
        }

        guard let data = try? Data(contentsOf: resolved) else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 404))
            return
        }

        let ext = resolved.pathExtension
        let mime = UTType(filenameExtension: ext)?.preferredMIMEType ?? "application/octet-stream"
        let headers = ["Content-Type": mime, "Content-Length": "\(data.count)"]
        guard let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: "HTTP/1.1", headerFields: headers) else {
            urlSchemeTask.didFailWithError(NSError(domain: "PmChatMedia", code: 500))
            return
        }
        urlSchemeTask.didReceive(response)
        urlSchemeTask.didReceive(data)
        urlSchemeTask.didFinish()
    }

    func webView(_ webView: WKWebView, stop urlSchemeTask: WKURLSchemeTask) {}
}
