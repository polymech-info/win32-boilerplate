//
//  PolymechFileViewerWebView.swift
//  PixlWiz
//
//  Persistent-WKWebView viewer host — parity with Win32 CViewerWebPanel / CMainFrame ownership.
//
//  Architecture:
//    PolymechViewerPageHost  — ObservableObject, owns WKWebView for its lifetime.
//                             Loads viewer.html ONCE in init(). Never reloaded.
//    PolymechViewerWebViewRef — thin NSViewRepresentable that wraps host.webView.
//                             dismantleNSView is intentionally a no-op so the
//                             WKWebView (and its WebContent process) stays alive
//                             across file-type switches, tab changes, etc.
//
//  File delivery (macOS — no virtual-host server; parity notes):
//    Markdown  : features.markdownText   = UTF-8 (≤ maxInlineBytes) + markdownBaseUrl = file://folder/
//    Text/code : features.hostedFileText = UTF-8 (≤ maxInlineBytes)
//                OR features.hostedFileUrl = file:// + vw_hosted_read bridge for larger files
//    PDF / 3D / spreadsheet / video :
//                features.hostedFileUrl / threeModelUrl = file:// URI
//                → web side sends {t:"vw_hosted_read", id, url} when it needs the bytes
//                → host streams base64 chunks back via __pmDispatchFromViewer
//    Save      : web sends {t:"vw_save_file", content} → host writes to native file
//    Agent     : polymechImageFileDidWriteInPlace → {t:"vw_tool_file_reloaded"} push + status refresh
//

import AppKit
import Foundation
import OSLog
import SwiftUI
import WebKit

// MARK: - PolymechViewer (bundle resolution + file-kind mapping)

enum PolymechViewer {
    /// In-app copy of viewer.html (CMake + Xcode script → Resources/PolymechViewer/viewer.html).
    static func bundledViewerHTMLURL() -> URL? {
        Bundle.main.url(forResource: "viewer", withExtension: "html", subdirectory: "PolymechViewer")
    }

    /// WKWebView read-access root: grant Contents/Resources so bundle assets are reachable.
    static func readAccessDirectory(for viewerHTML: URL) -> URL {
        if viewerHTML.path.hasSuffix("PolymechViewer/viewer.html") {
            return viewerHTML.deletingLastPathComponent().deletingLastPathComponent()
        }
        return viewerHTML.deletingLastPathComponent()
    }

    /// Map file extension → viewerKind (mirrors CFileViewer::OpenFile priority).
    static func viewerKind(for url: URL) -> String {
        switch url.pathExtension.lowercased() {
        case "md", "markdown":                      return "markdown"
        case "pdf":                                 return "pdf"
        case "mp4", "mov", "m4v", "avi", "mkv",
             "webm", "ogg":                         return "video"
        case "csv", "tsv", "xlsx", "xls", "ods":   return "spreadsheet"
        case "gltf", "glb", "obj", "stl",
             "3ds", "ply", "dae",
             "dxf",                                 // 2D CAD — rendered by DxfNativeLayer in ThreeDViewer
             "step", "stp":                         // Parametric solid — parsed by occt-import-js WASM
                                                    return "three"
        case "html", "htm":                         return "html"
        default:                                    return "text"
        }
    }

    /// Max file size for inline UTF-8 injection (markdown / text / code).
    static let maxInlineBytes = 2 * 1024 * 1024   // 2 MB

    /// Per-kind byte cap for vw_hosted_read (mirrors pmui::viewer_document_max_bytes_for_path_w /
    /// pmui::viewer_3d_max_bytes_for_ext on Win32).
    static func maxBytesForKind(_ kind: String, ext: String) -> Int {
        switch kind {
        case "pdf":         return 50 * 1024 * 1024   // 50 MB
        case "spreadsheet": return 10 * 1024 * 1024   // 10 MB
        case "video":       return 100 * 1024 * 1024  // 100 MB (web blob URL cap)
        case "html":        return 10 * 1024 * 1024
        case "three":
            switch ext {
            case "glb", "gltf":        return 64 * 1024 * 1024   // binary mesh
            case "obj", "ply":         return 10 * 1024 * 1024
            case "dxf":                return 20 * 1024 * 1024   // 2D CAD ASCII
            case "step", "stp":        return 50 * 1024 * 1024   // parametric solid (WASM parse)
            default:                   return 20 * 1024 * 1024
            }
        default:            return maxInlineBytes
        }
    }

    /// Escapes a string for embedding as a JS string literal inside evaluateJavaScript
    /// (parity with PolymechWebChat.javaScriptStringLiteral).
    static func jsStringLiteral(_ s: String) -> String {
        var out = "\""
        for ch in s {
            switch ch {
            case "\\": out += "\\\\"
            case "\"": out += "\\\""
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\u{2028}": out += "\\u2028"
            case "\u{2029}": out += "\\u2029"
            default: out.append(ch)
            }
        }
        out += "\""
        return out
    }

    /// Two-way bridge: routes chrome.webview.postMessage → webkit.messageHandlers.pmViewerHost
    /// and supports addEventListener('message') for host→web delivery via window.__pmDispatchFromViewer.
    /// Replaces the old one-way shim that had a no-op addEventListener (breaking onHostMessage).
    static var bridgePolyfill: String {
        #"""
        (function () {
            if (window.__pmViewerHostBridge) return;
            window.__pmViewerHostBridge = true;
            var listeners = [];
            var wkh = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.pmViewerHost;
            var bridge = {
                postMessage: function (s) {
                    var m = (typeof s === 'string') ? s : (function () { try { return JSON.stringify(s); } catch (e) { return ''; } }());
                    if (wkh) { try { wkh.postMessage(m); } catch (e) {} }
                },
                addEventListener: function (type, fn) {
                    if (type === 'message' && typeof fn === 'function') listeners.push(fn);
                },
                removeEventListener: function (type, fn) {
                    if (type !== 'message') return;
                    for (var i = listeners.length - 1; i >= 0; i--) {
                        if (listeners[i] === fn) { listeners.splice(i, 1); break; }
                    }
                }
            };
            if (!window.chrome) window.chrome = {};
            window.chrome.webview = bridge;
            // Called by native to deliver messages to JS listeners (vw_hosted_read_*, vw_save_file_result, etc.)
            window.__pmDispatchFromViewer = function (raw) {
                var ev = { data: raw };
                for (var i = 0; i < listeners.length; i++) {
                    try { listeners[i](ev); } catch (e) {}
                }
            };
        })();
        """#
    }
}

// MARK: - PolymechViewerPageHost

/// Owns one WKWebView for the lifetime of its enclosing SwiftUI state.
/// Matches Win32: CViewerWebPanel is a member of CMainFrame, created once, never destroyed.
final class PolymechViewerPageHost: ObservableObject {

    private static let log = Logger(subsystem: "com.polymech.pm-image", category: "Viewer")

    private(set) var webView: WKWebView
    private let coordinator: Coordinator

    /// URL last pushed to the web page (used to avoid redundant setStatus calls).
    private(set) var currentURL: URL?

    init() {
        let coord = Coordinator()
        let config = WKWebViewConfiguration()

        let polyfill = WKUserScript(
            source: PolymechViewer.bridgePolyfill,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        )
        config.userContentController.addUserScript(polyfill)
        // Single handler for ALL web→host messages (vw_console, vw_hosted_read, vw_save_file, vw_editor_mode).
        config.userContentController.add(coord, name: "pmViewerHost")

        let wv = WKWebView(frame: .zero, configuration: config)
        wv.navigationDelegate = coord
        wv.translatesAutoresizingMaskIntoConstraints = true
        wv.autoresizingMask = [.width, .height]

        self.coordinator = coord
        self.webView     = wv
        coord.host = self

        // Load viewer.html immediately — by the time the first file is clicked,
        // React will already be mounted and window.pmViewer ready.
        loadViewerHTML()
    }

    deinit {
        webView.stopLoading()
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "pmViewerHost")
    }

    // MARK: Load viewer.html (called once)

    private func loadViewerHTML() {
        guard let viewerHTML = PolymechViewer.bundledViewerHTMLURL() else {
            Self.log.error("viewer.html not found in bundle — run: cd apps/viewer-next && npm run build:embed")
            webView.loadHTMLString(
                """
                <html><body style='font-family:sans-serif;color:#ccc;background:#1e1e1e;padding:24px'>
                <b>viewer.html not found in app bundle.</b><br><br>
                Run from <code>packages/media/cpp/apps/viewer-next</code>:<br>
                <code>npm run build:embed</code>
                </body></html>
                """,
                baseURL: nil
            )
            return
        }
        let readAccess = PolymechViewer.readAccessDirectory(for: viewerHTML)
        Self.log.info("viewer: loading \(viewerHTML.path, privacy: .public) readAccess=\(readAccess.path, privacy: .public)")
        webView.loadFileURL(viewerHTML, allowingReadAccessTo: readAccess)
    }

    // MARK: Show a file (called on every navigation)

    func show(url: URL) {
        guard url != currentURL else { return }
        currentURL = url
        coordinator.show(url: url)
    }

    /// Re-push status for the current file (e.g. after an appearance setting change).
    func refreshTheme() {
        guard let url = currentURL else { return }
        coordinator.refreshTheme(url: url)
    }

    /// Push a JSON message from native to the web viewer
    /// (parity with Win32 CViewerWebPanel::PostRawJson / Impl::post_web_message_json).
    func postToViewer(_ payload: [String: Any]) {
        coordinator.postToViewer(payload, in: webView)
    }

    // MARK: Coordinator (navigation + message handler)

    final class Coordinator: NSObject, WKNavigationDelegate, WKScriptMessageHandler {
        private static let log = Logger(subsystem: "com.polymech.pm-image", category: "Viewer")

        weak var host: PolymechViewerPageHost?
        private var pageIsLoaded = false
        private var pendingURL: URL?

        /// Absolute path of the file currently shown — used for vw_hosted_read URL matching and vw_save_file.
        private(set) var currentFileAbsPath: String = ""
        /// Set when the Monaco editor kind is active; mirrors Win32 Impl::editor_mode_active.
        private(set) var editorModeActive: Bool = false
        /// Observer for polymechImageFileDidWriteInPlace (agent tool write → vw_tool_file_reloaded).
        private var fileWriteToken: NSObjectProtocol?

        override init() {
            super.init()
            fileWriteToken = NotificationCenter.default.addObserver(
                forName: .polymechImageFileDidWriteInPlace,
                object: nil,
                queue: .main
            ) { [weak self] note in
                guard let self,
                      let wv = self.host?.webView,
                      let writtenURL = note.userInfo?["url"] as? URL else { return }
                let path = writtenURL.path
                // Notify the web side (mirrors Win32 CFileViewer: PostRawJson for live agent updates).
                self.postToViewer(["t": "vw_tool_file_reloaded", "path": path], in: wv)
                // If the written file is the one currently shown, refresh the status push.
                if !self.currentFileAbsPath.isEmpty,
                   writtenURL.standardizedFileURL.path ==
                   URL(fileURLWithPath: self.currentFileAbsPath).standardizedFileURL.path,
                   let cur = self.host?.currentURL {
                    // Force re-push even though URL hasn't changed (same-file agent update).
                    self.pushStatus(for: cur, in: wv)
                }
            }
        }

        deinit {
            if let t = fileWriteToken { NotificationCenter.default.removeObserver(t) }
        }

        func show(url: URL, force: Bool = false) {
            pendingURL = url
            if pageIsLoaded, let wv = host?.webView {
                pendingURL = nil
                pushStatus(for: url, in: wv)
            }
        }

        func refreshTheme(url: URL) {
            guard pageIsLoaded, let wv = host?.webView else { return }
            pushStatus(for: url, in: wv)
        }

        // MARK: WKNavigationDelegate

        func webView(_ wv: WKWebView, didFinish _: WKNavigation!) {
            pageIsLoaded = true
            if let pending = pendingURL {
                pendingURL = nil
                pushStatus(for: pending, in: wv)
            }
        }

        func webView(_ wv: WKWebView, didFail _: WKNavigation!, withError error: Error) {
            Self.log.error("viewer navigation failed: \(error.localizedDescription, privacy: .public)")
        }

        func webView(_ wv: WKWebView, didFailProvisionalNavigation _: WKNavigation!, withError error: Error) {
            Self.log.error("viewer provisional failed: \(error.localizedDescription, privacy: .public)")
        }

        // MARK: WKScriptMessageHandler — all pmViewerHost messages dispatched here

        func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
            guard message.name == "pmViewerHost" else { return }
            let raw: String
            if let s = message.body as? String {
                raw = s
            } else if JSONSerialization.isValidJSONObject(message.body),
                      let d = try? JSONSerialization.data(withJSONObject: message.body),
                      let s = String(data: d, encoding: .utf8) {
                raw = s
            } else { return }

            guard let d = raw.data(using: .utf8),
                  let body = try? JSONSerialization.jsonObject(with: d) as? [String: Any],
                  let t = body["t"] as? String else { return }

            guard let wv = host?.webView else { return }

            switch t {
            case "vw_console":
                let lvl = body["level"] as? String ?? "info"
                let msg = body["msg"] as? String ?? ""
                switch lvl {
                case "error": Self.log.error("viewer js: \(msg, privacy: .public)")
                case "warn":  Self.log.warning("viewer js: \(msg, privacy: .public)")
                default:      Self.log.info("viewer js: \(msg, privacy: .public)")
                }

            case "vw_hosted_read":
                // Parity with Win32 Impl::handle_vw_hosted_read — stream base64 chunks to the web.
                handleHostedRead(body, in: wv)

            case "vw_save_file":
                // Parity with Win32 Impl::handle_vw_save_file — write editor content to native file.
                handleSaveFile(body, in: wv)

            case "vw_editor_mode":
                // Monaco editor active: mirrors Win32 impl->editor_mode_active.
                // (Future: could suppress keyboard-shortcut forwarding from this flag.)
                editorModeActive = body["active"] as? Bool ?? false

            default:
                break
            }
        }

        // MARK: Status push — parity with CViewerWebPanel::FlushContextToWeb

        private func pushStatus(for fileURL: URL, in wv: WKWebView) {
            guard !fileURL.path.isEmpty else { return }

            let absPath: String = {
                let norm = fileURL.standardizedFileURL
                let abs  = (try? URL(resolvingAliasFileAt: norm)) ?? norm
                return abs.path
            }()
            currentFileAbsPath = absPath

            let kind    = PolymechViewer.viewerKind(for: fileURL)
            let dark    = NSApp.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            let folder  = fileURL.deletingLastPathComponent().path
            let ext     = fileURL.pathExtension.lowercased()
            let maxB    = PolymechViewer.maxBytesForKind(kind, ext: ext)

            // Build features dict — mirrors Win32 FlushContextToWeb / SetHostedFileClientPreview /
            // SetMarkdownClientPreview / SetThreeClientPreview.
            var features: [String: Any] = [
                "theme":          dark ? "dark" : "light",
                "hostedFileName": fileURL.lastPathComponent,
            ]

            // Resolve file size (best-effort; skip on stat failure).
            let fileSize: Int? = {
                guard let attrs = try? FileManager.default.attributesOfItem(atPath: absPath) else { return nil }
                return attrs[.size] as? Int
            }()
            if let sz = fileSize { features["hostedFileSizeBytes"] = sz }

            switch kind {

            case "markdown":
                do {
                    let text = try String(contentsOf: fileURL, encoding: .utf8)
                    if text.utf8.count <= PolymechViewer.maxInlineBytes {
                        features["markdownText"] = text
                        // Provide base URL so relative image/link paths can be resolved by the viewer.
                        // file:// base — web side may attempt fetch (works only within readAccess).
                        features["markdownBaseUrl"] = "file://\(folder)/"
                    } else {
                        features["hostedFileError"] = "Markdown too large for inline preview (\((text.utf8.count) / 1024) KB — limit \(PolymechViewer.maxInlineBytes / 1024) KB)"
                    }
                } catch {
                    features["hostedFileError"] = error.localizedDescription
                    Self.log.error("viewer read error \(fileURL.lastPathComponent, privacy: .public): \(error.localizedDescription, privacy: .public)")
                }

            case "text":
                do {
                    let sz = fileSize ?? 0
                    if sz <= PolymechViewer.maxInlineBytes {
                        let text = try String(contentsOf: fileURL, encoding: .utf8)
                        features["hostedFileText"] = text
                    } else {
                        // Large text: use vw_hosted_read bridge (no URL scheme needed).
                        features["hostedFileUrl"]     = "file://\(absPath)"
                        features["hostedFileMaxBytes"] = maxB
                    }
                } catch {
                    features["hostedFileError"] = error.localizedDescription
                }

            case "three":
                let sz = fileSize ?? 0
                if sz > maxB {
                    features["threeError"] = "3D file too large for preview (\(sz / (1024*1024)) MB — limit \(maxB / (1024*1024)) MB)"
                } else {
                    features["threeModelUrl"]       = "file://\(absPath)"
                    features["threeFileName"]       = fileURL.lastPathComponent
                    features["threeFileSizeBytes"]  = sz
                    features["threeMaxBytes"]       = maxB
                }

            default:
                // pdf, video, spreadsheet, html: pass file:// URL; web uses vw_hosted_read to fetch bytes.
                let sz = fileSize ?? 0
                features["hostedFileUrl"]      = "file://\(absPath)"
                features["hostedFileMaxBytes"] = maxB
                if sz > 0 { features["hostedFileSizeBytes"] = sz }
            }

            let status: [String: Any] = [
                "selection":  [absPath],
                "folder":     folder,
                "viewerKind": kind,
                "features":   features,
            ]
            guard let data = try? JSONSerialization.data(withJSONObject: status),
                  let json = String(data: data, encoding: .utf8) else { return }

            // Resolve locale from system (mirrors Win32 ui_locale from AppearanceSettings).
            let localeTag: String = {
                if let p = Locale.preferredLanguages.first, !p.isEmpty { return p }
                return "en"
            }()
            let localeLit = PolymechViewer.jsStringLiteral(localeTag)

            // Mirrors Win32 FlushContextToWeb + PushFontExtraToWeb.
            // Use setTimeout retry in case React is still mounting on first call.
            let js = """
            (function () {
                var s = \(json);
                function flush() {
                    if (window.pmViewer) {
                        if (typeof window.pmViewer.setLocale === 'function') window.pmViewer.setLocale(\(localeLit));
                        if (typeof window.pmViewer.setStatus === 'function') window.pmViewer.setStatus(s);
                        if (typeof window.pmViewer.setFontExtraPt === 'function') window.pmViewer.setFontExtraPt(0);
                    }
                }
                if (window.pmViewer && typeof window.pmViewer.setStatus === 'function') {
                    flush();
                } else {
                    setTimeout(flush, 150);
                }
            })();
            """
            wv.evaluateJavaScript(js) { _, err in
                if let err { Self.log.error("pmViewer.setStatus: \(err.localizedDescription, privacy: .public)") }
            }
        }

        // MARK: Host → Web push

        /// Deliver a JSON payload to all `onHostMessage` subscribers in the web viewer.
        /// Parity with Win32 CViewerWebPanel::Impl::post_web_message_json / PostRawJson.
        func postToViewer(_ payload: [String: Any], in wv: WKWebView) {
            guard let data = try? JSONSerialization.data(withJSONObject: payload),
                  let jsonStr = String(data: data, encoding: .utf8) else { return }
            let lit = PolymechViewer.jsStringLiteral(jsonStr)
            let js = "try { if (window.__pmDispatchFromViewer) { window.__pmDispatchFromViewer(\(lit)) } } catch (e) {}"
            wv.evaluateJavaScript(js)
        }

        // MARK: vw_hosted_read — parity with Impl::handle_vw_hosted_read

        private func handleHostedRead(_ body: [String: Any], in wv: WKWebView) {
            let id  = body["id"]  as? String ?? ""
            let url = body["url"] as? String ?? ""
            guard !id.isEmpty else { return }

            let replyErr: (String) -> Void = { [weak self] code in
                guard let self else { return }
                self.postToViewer(["t": "vw_hosted_read_err", "id": id, "err": code], in: wv)
            }

            // Resolve native path from the file:// URL we put in hostedFileUrl / threeModelUrl.
            let nativePath: String
            if !currentFileAbsPath.isEmpty,
               (url == "file://\(currentFileAbsPath)" || url.isEmpty) {
                nativePath = currentFileAbsPath
            } else if url.hasPrefix("file://") {
                nativePath = String(url.dropFirst("file://".count))
                    .removingPercentEncoding ?? String(url.dropFirst("file://".count))
            } else {
                replyErr("url_mismatch")
                return
            }

            guard !nativePath.isEmpty else { replyErr("no_path"); return }

            let fileURL = URL(fileURLWithPath: nativePath)
            guard FileManager.default.isReadableFile(atPath: nativePath) else {
                replyErr("not_file"); return
            }

            // Stream on background thread; dispatch chunks back on main (evaluateJavaScript requirement).
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                guard let self else { return }
                do {
                    let attrs = try FileManager.default.attributesOfItem(atPath: nativePath)
                    let size  = (attrs[.size] as? Int) ?? 0

                    DispatchQueue.main.async { [weak self] in
                        guard let self else { return }
                        self.postToViewer(["t": "vw_hosted_read_meta", "id": id, "size": size], in: wv)
                    }

                    let fh = try FileHandle(forReadingFrom: fileURL)
                    defer { fh.closeFile() }

                    let chunkSize = 1024 * 1024  // 1 MB raw → ~1.3 MB base64 (fewer round-trips for large files)
                    var offset = 0

                    while offset < size {
                        // readData(ofLength:) returns empty Data at EOF (no throw).
                        let data = fh.readData(ofLength: min(chunkSize, size - offset))
                        if data.isEmpty { break }
                        let b64 = data.base64EncodedString()
                        let chunkOff = offset
                        DispatchQueue.main.async { [weak self] in
                            guard let self else { return }
                            self.postToViewer([
                                "t": "vw_hosted_read_chunk",
                                "id": id,
                                "off": chunkOff,
                                "b64": b64,
                            ], in: wv)
                        }
                        offset += data.count
                    }

                    DispatchQueue.main.async { [weak self] in
                        guard let self else { return }
                        self.postToViewer(["t": "vw_hosted_read_done", "id": id], in: wv)
                    }
                } catch {
                    DispatchQueue.main.async { [weak self] in
                        guard let self else { return }
                        self.postToViewer(["t": "vw_hosted_read_err", "id": id, "err": "read_failed"], in: wv)
                    }
                }
            }
        }

        // MARK: vw_save_file — parity with Impl::handle_vw_save_file

        private func handleSaveFile(_ body: [String: Any], in wv: WKWebView) {
            let content = body["content"] as? String ?? ""

            let reply: (Bool, String?) -> Void = { [weak self] ok, errCode in
                guard let self else { return }
                var o: [String: Any] = ["t": "vw_save_file_result", "ok": ok]
                if let c = errCode { o["err"] = c }
                self.postToViewer(o, in: wv)
            }

            // Resolve save path — mirrors Win32: native_hosted_read_abs_path_w ?? selection[0].
            let savePath = currentFileAbsPath
            guard !savePath.isEmpty else { reply(false, "no_path"); return }

            let fileURL = URL(fileURLWithPath: savePath)
            guard FileManager.default.isReadableFile(atPath: savePath) else {
                reply(false, "not_file"); return
            }

            do {
                try content.write(to: fileURL, atomically: true, encoding: .utf8)
                Self.log.info("viewer vw_save_file: saved \(savePath, privacy: .public) (\(content.utf8.count) B)")
                reply(true, nil)
            } catch {
                Self.log.error("viewer vw_save_file: write failed \(savePath, privacy: .public): \(error.localizedDescription, privacy: .public)")
                reply(false, "write_failed")
            }
        }
    }
}

// MARK: - PolymechViewerWebViewRef

/// Thin NSViewRepresentable that wraps the host's persistent WKWebView.
/// dismantleNSView is intentionally a no-op: the host owns the webView lifecycle
/// so it survives file-type switches that remove this view from the SwiftUI tree.
struct PolymechViewerWebViewRef: NSViewRepresentable {
    let host: PolymechViewerPageHost
    let url: URL

    func makeNSView(context: Context) -> WKWebView {
        host.show(url: url)
        return host.webView
    }

    func updateNSView(_ wv: WKWebView, context: Context) {
        host.show(url: url)
    }

    static func dismantleNSView(_ nsView: WKWebView, coordinator: ()) {
        // No-op: host owns the WKWebView; destroying this ref must not kill the page.
    }
}
