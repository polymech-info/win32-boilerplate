import AppKit
import Foundation
import OSLog
import SwiftUI
import WebKit

// MARK: - Unified log (filter Console: subsystem com.polymech.pm-image, category WebChat)

private enum PolymechWebChatHostLog {
    static let log = Logger(subsystem: "com.polymech.pm-image", category: "WebChat")
    private static let chatEmbed = Logger(subsystem: "com.polymech.pm-image", category: "ChatEmbed")

    static func wkLoad(file: URL, readAccess: URL) {
        chatEmbed.notice(
            "wk loadFileURL file=\(file.path, privacy: .public) readAccess=\(readAccess.path, privacy: .public)"
        )
    }

    static func nav(_ message: String) {
        log.info("\(message, privacy: .public)")
    }

    static func navError(_ message: String) {
        log.error("\(message, privacy: .public)")
    }

    static func js(_ level: String, _ message: String) {
        let clipped = Self.clip(message, maxUTF8: 12_000)
        switch level {
        case "error": log.error("js \(clipped, privacy: .public)")
        case "warn": log.warning("js \(clipped, privacy: .public)")
        default: log.info("js[\(level, privacy: .public)] \(clipped, privacy: .public)")
        }
    }

    private static func clip(_ s: String, maxUTF8: Int) -> String {
        guard maxUTF8 > 0 else { return "" }
        if s.utf8.count <= maxUTF8 { return s }
        var out = String()
        out.reserveCapacity(min(maxUTF8, s.count))
        var used = 0
        for ch in s {
            let cost = String(ch).utf8.count
            if used + cost > maxUTF8 { break }
            out.append(ch)
            used += cost
        }
        return out + "…"
    }
}

// MARK: - Path resolution (bundled in .app, same as Win32 dist/chat.html + local workspace)

struct PolymechWebChat {
    private init() {}
    private static let candidateWorkspaceRelativePaths = [
        "packages/media/cpp/dist/chat.html",
        "apps/chat-next/dist-embed/chat.html",
        "packages/media/cpp/apps/chat-next/dist-embed/chat.html",
        "apps/chat/dist/chat.html",
        "packages/media/cpp/apps/chat/dist/chat.html",
    ]

    /// In-app copy of the WebView chat bundle (CMake → `Resources/PolymechChat/chat.html`).
    static func bundledChatHTMLURL() -> URL? {
        Bundle.main.url(
            forResource: "chat",
            withExtension: "html",
            subdirectory: "PolymechChat"
        )
    }

    /// Picks the first `chat.html` on disk (workspace = folder opened in CodeEdit), if any.
    static func resolvedChatHTMLFromWorkspace(_ workspaceRoot: URL) -> URL? {
        var base = workspaceRoot
        if !base.hasDirectoryPath { base = base.deletingLastPathComponent() }
        for rel in candidateWorkspaceRelativePaths {
            let u = base.appendingPathComponent(rel)
            if FileManager.default.isReadableFile(atPath: u.path) { return u.standardizedFileURL }
        }
        for rel in candidateWorkspaceRelativePaths {
            var p = base
            while !p.path.isEmpty, p.path != "/" {
                let u = p.appendingPathComponent(rel)
                if FileManager.default.isReadableFile(atPath: u.path) { return u.standardizedFileURL }
                p = p.deletingLastPathComponent()
            }
        }
        return nil
    }

    /// Directory WKWebView may read when loading `chat.html` (must be an ancestor of the file URL).
    /// Bundle: grant `Contents/Resources` so the file stays under read access (not only `PolymechChat/`).
    /// Workspace: `dist/` next to `dist/chat.html` (same layout as Win32 beside `pm-image`).
    static func readAccessDirectory(for chatHTML: URL) -> URL {
        let p = chatHTML.path
        if p.hasSuffix("PolymechChat/chat.html") || p.hasSuffix("PolymechChat/chat.htm") {
            return chatHTML.deletingLastPathComponent().deletingLastPathComponent()
        }
        return chatHTML.deletingLastPathComponent()
    }

    static func isPolymechChatFileURLForWebView(_ url: URL) -> Bool {
        let p = url.path
        if p.hasSuffix("PolymechChat/chat.html") || p.hasSuffix("PolymechChat/chat.htm") { return true }
        if p.hasSuffix("/dist/chat.html") || p.hasSuffix("\\dist\\chat.html") { return true }
        if p.hasSuffix("/dist-embed/chat.html") || p.hasSuffix("\\dist-embed\\chat.html") { return true }
        if p.contains("/apps/chat-next/") && (p.hasSuffix(".html") || p.hasSuffix(".htm")) { return true }
        if p.contains("/apps/chat/") && (p.hasSuffix(".html") || p.hasSuffix(".htm")) { return true }
        // Any `chat.html` under known embed / bundle folders (temp copies, symlinks, odd layouts).
        let last = url.lastPathComponent.lowercased()
        if last == "chat.html" || last == "chat.htm" {
            let pl = p.lowercased()
            if pl.contains("polymechchat")
                || pl.contains("/dist/")
                || pl.contains("\\dist\\")
                || pl.contains("dist-embed")
                || pl.contains("chat-next")
                || pl.contains("/apps/chat/")
            {
                return true
            }
        }
        return false
    }

    /// BCP-47-ish tag for `window.pmChat.setLocale` (see `apps/chat-next/src/pm-chat/i18n.js` and Win32 `ChatWebPanel::Impl::FlushContextToWeb`).
    static func webChatJSLocaleTag(appLanguage: SettingsData.AppLanguage) -> String {
        switch appLanguage {
        case .system:
            if let p = Locale.preferredLanguages.first, !p.isEmpty { return p }
            return "en"
        case .en: return "en"
        case .es: return "es"
        case .it: return "it"
        case .fr: return "fr"
        case .de: return "de"
        case .ptBR: return "pt-BR"
        }
    }

    /// Escapes a string for use inside a JavaScript string literal in `evaluateJavaScript`.
    static func javaScriptStringLiteral(_ s: String) -> String {
        var out = String()
        out.append("\"")
        for ch in s {
            switch ch {
            case "\\": out.append("\\\\")
            case "\"": out.append("\\\"")
            case "\n": out.append("\\n")
            case "\r": out.append("\\r")
            case "\u{2028}": out.append("\\u2028")
            case "\u{2029}": out.append("\\u2029")
            default: out.append(ch)
            }
        }
        out.append("\"")
        return out
    }

    // WKWebView-style bridge — at document start, before the bundle uses `postMessage` / `addEventListener('message')`.
    static var webkitBridgePolyfill: String {
        #"""
        (function () {
            if (window.__pmWkWebkitChatBridge) return;
            window.__pmWkWebkitChatBridge = true;
            if (!window.chrome) { window.chrome = {}; }
            var messageListeners = [];
            window.chrome.webview = {
                postMessage: function (m) {
                    var s = (typeof m === "string") ? m : (function () { try { return JSON.stringify(m); } catch (e) { return String(m); } }());
                    try { window.webkit.messageHandlers.pmChatHost.postMessage(s); } catch (e) {}
                },
                addEventListener: function (name, fn) {
                    if (name === "message") { messageListeners.push(fn); }
                },
                removeEventListener: function (name, fn) {
                    if (name !== "message") { return; }
                    for (var i = messageListeners.length - 1; i >= 0; i -= 1) {
                        if (messageListeners[i] === fn) { messageListeners.splice(i, 1); }
                    }
                }
            };
            window.__pmDispatchFromHost = function (raw) {
                var ev = { data: raw };
                for (var i = 0; i < messageListeners.length; i += 1) {
                    try { messageListeners[i](ev); } catch (e) {}
                }
            };
        }());
        """#
    }

    /// Forwards `console.log|warn|error|info|debug` plus uncaught exceptions / unhandled rejections to `pmChatWebLog` → unified log (category WebChat).
    static var webConsoleCaptureScript: String {
        #"""
        (function () {
            if (window.__pmWebChatConsoleCapture) return;
            window.__pmWebChatConsoleCapture = true;
            function send(level, args) {
                try {
                    var parts = [];
                    for (var i = 0; i < args.length; i++) {
                        var a = args[i];
                        if (typeof a === "string") parts.push(a);
                        else { try { parts.push(JSON.stringify(a)); } catch (e) { parts.push(String(a)); } }
                    }
                    var payload = JSON.stringify({ level: level, msg: parts.join(" ") });
                    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.pmChatWebLog) {
                        window.webkit.messageHandlers.pmChatWebLog.postMessage(payload);
                    }
                } catch (e) {}
            }
            ["log", "warn", "error", "info", "debug"].forEach(function (name) {
                var orig = console[name];
                console[name] = function () {
                    var args = Array.prototype.slice.call(arguments);
                    if (typeof orig === "function") { try { orig.apply(console, args); } catch (e) {} }
                    send(name, args);
                };
            });
            window.addEventListener("error", function (ev) {
                try {
                    var msg = (ev && ev.message) ? String(ev.message) : "window error";
                    if (ev && ev.filename) { msg += " @ " + ev.filename + ":" + (ev.lineno || 0) + ":" + (ev.colno || 0); }
                    if (ev && ev.error && ev.error.stack) { msg += "\n" + String(ev.error.stack); }
                    send("error", [msg]);
                } catch (e) {}
            });
            window.addEventListener("unhandledrejection", function (ev) {
                try {
                    var r = ev && ev.reason;
                    var msg = (r && typeof r === "object" && r.stack) ? String(r.stack) : String(r);
                    send("error", ["unhandledrejection: " + msg]);
                } catch (e) {}
            });
        })();
        """#
    }

    /// Rewrites `file://` and absolute `/…` `<img src>` to `pmasset://local?p=…` so ``PmChatLocalMediaSchemeHandler`` can
    /// serve bytes (WKWebView does not allow arbitrary `file:` subresources outside `allowingReadAccessTo` like Win32
    /// WebView2 virtual-host file mapping).
    static var macChatFileImageSrcRewriteScript: String {
        #"""
        (function () {
            if (window.__pmMacChatFileImageRewrite) return;
            window.__pmMacChatFileImageRewrite = true;
            function absPathFromSrc(s) {
                if (!s || typeof s !== "string") return null;
                if (/^pmasset:/i.test(s)) return null;
                if (/^(https?|data):/i.test(s)) return null;
                try {
                    if (/^file:\/\//i.test(s)) {
                        var u = new URL(s);
                        if (u.protocol === "file:") {
                            var q = u.pathname || "";
                            try { q = decodeURIComponent(q); } catch (e0) {}
                            if (q.indexOf("//") === 0) q = q.slice(1);
                            return q;
                        }
                    }
                    if (s.charCodeAt(0) === 47) return s;
                } catch (e1) {}
                return null;
            }
            function toPmAsset(abs) {
                return "pmasset://local?p=" + encodeURIComponent(abs);
            }
            function patchImg(el) {
                if (!el || el.tagName !== "IMG") return;
                var s = el.getAttribute("src");
                if (!s) return;
                var abs = absPathFromSrc(s);
                if (!abs) return;
                var n = toPmAsset(abs);
                if (n !== s) el.setAttribute("src", n);
            }
            function scan(root) {
                if (!root || !root.querySelectorAll) return;
                root.querySelectorAll("img[src]").forEach(patchImg);
            }
            var obs = new MutationObserver(function (records) {
                for (var i = 0; i < records.length; i++) {
                    var r = records[i];
                    if (r.type === "attributes" && r.attributeName === "src" && r.target && r.target.tagName === "IMG") {
                        patchImg(r.target);
                    }
                    if (!r.addedNodes) continue;
                    r.addedNodes.forEach(function (node) {
                        if (node.nodeType === 1) {
                            if (node.tagName === "IMG") patchImg(node);
                            scan(node);
                        }
                    });
                }
            });
            function start() {
                obs.observe(document.documentElement, { subtree: true, childList: true, attributes: true, attributeFilter: ["src"] });
                scan(document.documentElement);
            }
            if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start);
            else start();
        })();
        """#
    }
}

// MARK: - WebView (WK) + message bridge (parity with `window.chrome.webview` on WebView2)

struct PolymechWebChatContainerView: View {
    @Environment(\.settings) private var settings
    @EnvironmentObject private var workflow: PolymechWorkflowState
    @EnvironmentObject private var editorManager: EditorManager
    var fileURL: URL

    var body: some View {
        PolymechWebChatWKView(
            fileURL: fileURL,
            readAccessURL: PolymechWebChat.readAccessDirectory(for: fileURL),
            workflow: workflow,
            editorManager: editorManager,
            appLanguage: settings.general.appLanguage
        )
    }
}

struct PolymechWebChatWKView: NSViewRepresentable {
    var fileURL: URL
    var readAccessURL: URL
    var workflow: PolymechWorkflowState
    var editorManager: EditorManager
    /// Matches Win32 `CChatWebView::SetContext` + `SetDisplayLanguage` → `window.pmChat.setLocale` before `setStatus`.
    var appLanguage: SettingsData.AppLanguage

    final class Coordinator: NSObject, WKScriptMessageHandler, WKNavigationDelegate {
        var fileURL: URL
        var readAccessURL: URL
        var appLanguage: SettingsData.AppLanguage
        weak var workflow: PolymechWorkflowState?
        weak var editorManager: EditorManager?
        weak var webView: WKWebView?
        var loadedFilePath: String?
        var lastPushedKey: String?
        var lastPushedLocaleTag: String?
        var didAnnounceProviderForPage = false
        var turnTask: Task<Void, Never>?
        var contextNotification: NSObjectProtocol?
        /// Web drag/drop paths merged with Explorer selection (Win32 `context_extra`).
        private var contextExtraPaths: [String] = []
        /// Paths hidden via web filmstrip remove (Win32 `context_removed`).
        private var contextRemovedPaths: [String] = []
        /// Slim MCP catalog from `probe_mcp_config`; nil until the background probe finishes.
        /// Mirrors Win32 `mcp_web_catalog` / `mcp_catalog_loaded` (UWM_MCP_CATALOG_READY).
        private var mcpCatalog: [String: Any]?
        private var mcpCatalogProbeStarted = false
        /// Serves `pmasset://local?p=…` for chat `<img>` (see ``PolymechWebChat/macChatFileImageSrcRewriteScript``).
        let pmChatMediaSchemeHandler = PmChatLocalMediaSchemeHandler()

        init(fileURL: URL, readAccessURL: URL, appLanguage: SettingsData.AppLanguage) {
            self.fileURL = fileURL
            self.readAccessURL = readAccessURL
            self.appLanguage = appLanguage
        }

        func userContentController(
            _ userContentController: WKUserContentController,
            didReceive message: WKScriptMessage
        ) {
            if message.name == "pmChatWebLog" {
                if let s = message.body as? String,
                   let d = s.data(using: .utf8),
                   let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any],
                   let level = o["level"] as? String,
                   let msg = o["msg"] as? String
                {
                    PolymechWebChatHostLog.js(level, msg)
                } else if let s = message.body as? String {
                    PolymechWebChatHostLog.js("log", s)
                }
                return
            }
            guard message.name == "pmChatHost" else { return }
            let payload: String
            if let s = message.body as? String {
                payload = s
            } else if JSONSerialization.isValidJSONObject(message.body),
                      let d = try? JSONSerialization.data(withJSONObject: message.body, options: []),
                      let s = String(data: d, encoding: .utf8)
            {
                payload = s
            } else {
                return
            }
            hostReceivedJSON(payload, webView: webView)
        }

        private func hostReceivedJSON(_ s: String, webView: WKWebView?) {
            guard
                let d = s.data(using: .utf8),
                let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any],
                let kind = o["kind"] as? String
            else { return }

            switch kind {
            case "ready", "settings":
                pushContext(webView: webView)
                announceTextProviderIfNeeded(webView: webView)
                if kind == "ready" {
                    pushHostChatWebFromSettingsIfPresent(webView: webView)
                    startMcpCatalogProbeIfNeeded(webView: webView)
                }
            case "send", "sendTurn":
                if let p = o["prompt"] as? String {
                    let r = (o["router"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines)
                    let m = (o["model"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines)
                    let disableTools: [String]? = {
                        guard let arr = o["disable_tools"] as? [Any] else { return nil }
                        let names = arr.compactMap { $0 as? String }.map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty }
                        return names.isEmpty ? nil : names
                    }()
                    // Win32 parity: mcp_tools_enabled (bool, default true) + disabled_mcp_servers (string[]).
                    let mcpToolsEnabled = (o["mcp_tools_enabled"] as? Bool) ?? true
                    let disabledMcpServers: [String]? = {
                        guard let arr = o["disabled_mcp_servers"] as? [Any] else { return nil }
                        let names = arr.compactMap { $0 as? String }.filter { !$0.isEmpty }
                        return names.isEmpty ? nil : names
                    }()
                    runNativeTurn(
                        userPrompt: p,
                        routerOverride: (r?.isEmpty == false) ? r : nil,
                        modelOverride: (m?.isEmpty == false) ? m : nil,
                        disableTools: disableTools,
                        mcpToolsEnabled: mcpToolsEnabled,
                        disabledMcpServers: disabledMcpServers,
                        webView: webView
                    )
                } else {
                    pushContext(webView: webView)
                }
            case "stop":
                turnTask?.cancel()
                turnTask = nil
                if let wv = webView {
                    setWebChatBusy(false, in: wv)
                    pushPmChat("appendText", withJSONObject: ["role": "system", "text": "Stop requested. Current native turn cannot be interrupted yet."], in: wv)
                }
            case "clear":
                break
            case "addContextPaths":
                var paths: [String] = []
                if let arr = o["paths"] as? [Any] {
                    for el in arr {
                        guard let raw = el as? String else { continue }
                        paths.append(raw)
                    }
                }
                mergeAddContextPathStrings(paths, webView: webView)
            case "removeContextPaths":
                if let arr = o["paths"] as? [Any] {
                    for el in arr {
                        guard let raw = el as? String else { continue }
                        let p = raw.trimmingCharacters(in: .whitespacesAndNewlines)
                        if p.isEmpty { continue }
                        contextExtraPaths.removeAll { Self.pathsMatch($0, p) }
                        if !contextRemovedPaths.contains(where: { Self.pathsMatch($0, p) }) {
                            contextRemovedPaths.append(p)
                        }
                    }
                }
                lastPushedKey = nil
                pushContext(webView: webView)
            case "chatWebState":
                if let d = o["doc"] as? [String: Any] {
                    saveChatWebDocToSettings(d)
                }
            case "providerRpc":
                let reply = dispatchProviderRpcMac(o)
                var out: [String: Any] = ["kind": "hostProviderRpc"]
                for (k, v) in reply { out[k] = v }
                if let rpcId = o["rpcId"] {
                    out["id"] = rpcId
                } else if let id = o["id"] {
                    out["id"] = id
                }
                pushProviderRpcReplyToWeb(out)
            case "openPathDefault":
                if let p = o["path"] as? String {
                    handleChatHostOpenPathDefault(path: p)
                }
            case "openFolderInExplorer":
                if let p = o["path"] as? String {
                    handleChatHostOpenFolderInExplorer(path: p)
                }
            case "selectPathInExplorer":
                if let p = o["path"] as? String {
                    handleChatHostSelectPathInExplorer(path: p)
                }
            case "openPathInternal":
                if let p = o["path"] as? String {
                    handleChatHostOpenPathInternal(path: p)
                }
            case "openPathFullscreen":
                handleChatHostOpenPathFullscreen(o)
            case "toggleFileTree":
                // Parity with Win32 btnExplorerTip → toggle the navigator sidebar.
                handleChatHostToggleFileTree()
            default:
                break
            }
        }

        /// Parity with Win32 `btnExplorerTip` → toggles the navigator (project-tree) sidebar.
        private func handleChatHostToggleFileTree() {
            guard let ws = workflow?.workspace else { return }
            let wcs = ws.windowControllers.compactMap { $0 as? CodeEditWindowController }
            let wc = wcs.first { $0.window?.isKeyWindow == true } ?? wcs.first
            wc?.toggleFirstPanel()
        }

        /// Parity with Win32 `ChatWebPanel` `openPathDefault` → `pmui::shell::open_path`.
        private func handleChatHostOpenPathDefault(path: String) {
            let t = path.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !t.isEmpty else { return }
            let url = URL(fileURLWithPath: t).standardizedFileURL
            NSWorkspace.shared.open(url)
        }

        /// Parity with Win32 `openFolderInExplorer` → `pmui::shell::explore_folder` (Finder on macOS).
        private func handleChatHostOpenFolderInExplorer(path: String) {
            let t = path.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !t.isEmpty else { return }
            let url = URL(fileURLWithPath: t).standardizedFileURL
            var isDir: ObjCBool = false
            guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDir) else {
                NSWorkspace.shared.open(url)
                return
            }
            let folderURL = isDir.boolValue ? url : url.deletingLastPathComponent()
            NSWorkspace.shared.open(folderURL)
        }

        /// Parity with Win32 `selectPathInExplorer` → `UWM_SELECT_PATH_IN_EXPLORER` / `SelectPathInFileTree`.
        private func handleChatHostSelectPathInExplorer(path: String) {
            let t = path.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !t.isEmpty, let ws = workflow?.workspace else { return }
            let norm = URL(fileURLWithPath: t).standardizedFileURL
            let rootPath = ws.workspaceFileManager?.folderUrl.standardizedFileURL.path ?? ""
            let p = norm.path
            let underWorkspace = !rootPath.isEmpty && (p == rootPath || p.hasPrefix(rootPath + "/"))
            if underWorkspace {
                ws.polymechPrepareNavigatorToShow(path: p)
                NotificationCenter.default.post(
                    name: .polymechRevealFileInNavigator,
                    object: ws,
                    userInfo: ["path": p]
                )
            } else {
                var isDir: ObjCBool = false
                _ = FileManager.default.fileExists(atPath: p, isDirectory: &isDir)
                let folder = isDir.boolValue ? norm : norm.deletingLastPathComponent()
                NSWorkspace.shared.selectFile(isDir.boolValue ? nil : norm.path, inFileViewerRootedAtPath: folder.path)
            }
        }

        /// Parity with Win32 `openPathInternal` → centre file viewer; CodeEdit opens a **non-temporary** editor tab.
        private func handleChatHostOpenPathInternal(path: String) {
            let t = path.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !t.isEmpty, let em = editorManager, let ws = workflow?.workspace else { return }
            let norm = URL(fileURLWithPath: t).standardizedFileURL
            ws.polymechPrepareNavigatorToShow(path: norm.path)
            guard let file = ws.workspaceFileManager?.getFile(norm.path, createIfNotFound: true) else {
                NSWorkspace.shared.open(norm)
                return
            }
            em.activeEditor.openTab(file: file, asTemporary: false)
            NotificationCenter.default.post(
                name: .polymechRevealFileInNavigator,
                object: ws,
                userInfo: ["path": norm.path]
            )
        }

        /// Parity with Win32 `openPathFullscreen` → `pmui::chat_image_fullscreen_show` (no separate Mac viewer yet: open like internal).
        private func handleChatHostOpenPathFullscreen(_ o: [String: Any]) {
            func trim(_ s: String) -> String {
                s.trimmingCharacters(in: .whitespacesAndNewlines)
            }
            var paths: [String] = []
            if let arr = o["paths"] as? [Any] {
                for el in arr {
                    guard let s = el as? String else { continue }
                    let u = trim(s)
                    if !u.isEmpty { paths.append(u) }
                }
            }
            var idx = 0
            if let n = o["index"] as? Int {
                idx = max(0, n)
            } else if let n = o["index"] as? UInt64 {
                idx = Int(min(n, UInt64(max(paths.count - 1, 0))))
            } else if let n = o["index"] as? Int64, n >= 0 {
                idx = min(Int(n), max(paths.count - 1, 0))
            } else if let n = o["index"] as? Double {
                idx = max(0, min(Int(n), max(paths.count - 1, 0)))
            }

            var primary = ""
            if let s = o["path"] as? String { primary = trim(s) }
            if primary.isEmpty, !paths.isEmpty {
                if idx >= paths.count { idx = 0 }
                primary = paths[idx]
            } else if !primary.isEmpty, let j = paths.firstIndex(where: { $0.caseInsensitiveCompare(primary) == .orderedSame }) {
                idx = j
            } else if !primary.isEmpty {
                paths.append(primary)
                idx = paths.count - 1
            }
            guard !primary.isEmpty else { return }
            handleChatHostOpenPathInternal(path: primary)
        }

        /// Parity with Win32 `ChatWebPanel` `kind == "ready"`: rehydrate web chat state from `settings.json` → `chat_web`.
        fileprivate func pushHostChatWebFromSettingsIfPresent(webView: WKWebView?) {
            guard let wv = webView else { return }
            let root = PolymechProviderSettingsStore.readSettingsRoot()
            guard
                let cw = root["chat_web"] as? [String: Any],
                !cw.isEmpty
            else { return }
            var payload: [String: Any] = ["kind": "hostChatWeb", "doc": cw]
            guard
                let data = try? JSONSerialization.data(withJSONObject: payload, options: []),
                let jsonString = String(data: data, encoding: .utf8)
            else { return }
            let lit = PolymechWebChat.javaScriptStringLiteral(jsonString)
            let js = "try { if (window.__pmDispatchFromHost) { window.__pmDispatchFromHost(\(lit)) } } catch (e) {}"
            runJS(j: js, in: wv)
        }

        /// Parity with Win32 `ChatWebPanel` `kind == "chatWebState"`: `save_chat_web` / subtree `chat_web`.
        fileprivate func saveChatWebDocToSettings(_ doc: [String: Any]) {
            guard !doc.isEmpty else { return }
            do {
                var root = PolymechProviderSettingsStore.readSettingsRoot()
                root["chat_web"] = doc
                try PolymechProviderSettingsStore.writeSettingsRoot(root)
            } catch { /* best-effort; Win32 only logs a warning */ }
        }

        private static func pathsMatch(_ a: String, _ b: String) -> Bool {
            a.compare(b, options: .caseInsensitive) == .orderedSame
        }

        /// Win32 `addContextPaths` / shell `CF_HDROP` parity (also used by ``PolymechChatShellDropWKWebView``).
        fileprivate func mergeAddContextPathStrings(_ rawPaths: [String], webView: WKWebView?) {
            guard workflow != nil, editorManager != nil, webView != nil else { return }
            for raw in rawPaths {
                let p = raw.trimmingCharacters(in: .whitespacesAndNewlines)
                if p.isEmpty { continue }
                contextRemovedPaths.removeAll { Self.pathsMatch($0, p) }
                let dup = contextExtraPaths.contains { Self.pathsMatch($0, p) }
                if !dup { contextExtraPaths.append(p) }
            }
            lastPushedKey = nil
            // Dispatch to the next default-mode run-loop tick.
            //
            // performDragOperation (the NSDraggingDestination callback that calls us) runs
            // during AppKit's drag session in NSEventTrackingRunLoopMode. In that mode
            // WKWebView silently defers evaluateJavaScript calls; they don't execute until
            // after the drag ends. Meanwhile, the polymechNavigatorContextChanged
            // notification queued by updateNavigatorSelection (in draggingSession:willBeginAt:
            // or onDrag) fires on the SAME post-drag tick and — because lastPushedKey already
            // matches — causes pushContext to bail, leaving the filmstrip stale.
            //
            // Dispatching here means pushContext fires one tick AFTER the notification
            // (lastPushedKey is still nil at that point), so the JS executes reliably and
            // the filmstrip updates immediately on drop without requiring a follow-up
            // navigator interaction.
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.pushContext(webView: self.webView)
            }
        }

        private func pruneStaleContextRemoved(explorer: [String], extra: [String]) {
            let mergedSources = explorer + extra
            contextRemovedPaths.removeAll { r in
                !mergedSources.contains(where: { Self.pathsMatch($0, r) })
            }
        }

        private func mergedSelectionPaths(explorer: [String]) -> [String] {
            var out: [String] = []
            var seenLower = Set<String>()
            func consider(_ raw: String) {
                let p = raw.trimmingCharacters(in: .whitespacesAndNewlines)
                if p.isEmpty { return }
                if contextRemovedPaths.contains(where: { Self.pathsMatch($0, p) }) { return }
                let k = p.lowercased()
                if seenLower.contains(k) { return }
                seenLower.insert(k)
                out.append(p)
            }
            for p in explorer { consider(p) }
            for p in contextExtraPaths { consider(p) }
            return out
        }

        /// Background MCP catalog probe — parity with Win32 `chat_web_start_mcp_catalog_probe`.
        /// Called once when the web page sends `ready`. Runs `probe_mcp_config` on a detached
        /// task (can take several seconds if servers time out), then refreshes the web context
        /// so the sidebar receives `mcp_catalog` in `setStatus`.
        private func startMcpCatalogProbeIfNeeded(webView: WKWebView?) {
            guard !mcpCatalogProbeStarted else { return }
            mcpCatalogProbeStarted = true
            Task.detached(priority: .utility) { [weak self] in
                let catalog: [String: Any]
                do {
                    catalog = try PolymechImageEngine.runMcpProbe()
                } catch {
                    catalog = ["servers": [], "exists": false, "probe_error": error.localizedDescription]
                }
                await MainActor.run { [weak self] in
                    guard let self else { return }
                    self.mcpCatalog = catalog
                    self.lastPushedKey = nil          // force full re-push with mcp_catalog
                    self.pushContext(webView: self.webView)
                }
            }
        }

        fileprivate func pushContext(webView: WKWebView?) {
            guard let wv = webView, let wf = workflow, let em = editorManager else { return }
            let localeTag = PolymechWebChat.webChatJSLocaleTag(appLanguage: appLanguage)
            let explorerSel = wf.chatSelectionPaths(editorManager: em)
            pruneStaleContextRemoved(explorer: explorerSel, extra: contextExtraPaths)
            let selection = mergedSelectionPaths(explorer: explorerSel)
            let folder = wf.chatFolderHint(editorManager: em)
            let workspacePath = wf.chatWorkspacePath()
            let key = (selection + [folder, workspacePath]).joined(separator: "\u{1E}")
            if key == lastPushedKey, localeTag == lastPushedLocaleTag { return }
            lastPushedKey = key
            lastPushedLocaleTag = localeTag
            var o: [String: Any] = [
                "selection": selection,
                "folder": folder
            ]
            if !workspacePath.isEmpty { o["workspace"] = workspacePath }
            // Read explicit saved chat fields; do not fall back to active/default provider rows.
            let settingsRoot = PolymechProviderSettingsStore.readSettingsRoot()
            let chatSettings = settingsRoot["chat"] as? [String: Any] ?? [:]
            let savedRouter = chatSettings["router"] as? String ?? ""
            let savedModel = chatSettings["model"] as? String ?? ""
            if !savedRouter.isEmpty {
                o["saved_chat_router"] = savedRouter
                if !savedModel.isEmpty { o["saved_chat_model"] = savedModel }
            }
            // Win32 parity: include slim MCP catalog once the background probe has finished.
            if let catalog = mcpCatalog { o["mcp_catalog"] = catalog }
            runWebChatSetLocaleAndSetStatus(locale: localeTag, status: o, in: wv)
        }

        /// Same as Win32 `ChatWebPanel::Impl::FlushContextToWeb` — `setLocale` then `setStatus` on one tick.
        private func runWebChatSetLocaleAndSetStatus(locale: String, status: [String: Any], in webView: WKWebView) {
            guard
                let data = try? JSONSerialization.data(withJSONObject: status, options: []),
                let json = String(data: data, encoding: .utf8)
            else { return }
            let loc = PolymechWebChat.javaScriptStringLiteral(locale)
            let js = "try { if (window.pmChat) { if (window.pmChat.setLocale) { window.pmChat.setLocale(\(loc)); } if (window.pmChat.setStatus) { window.pmChat.setStatus(\(json)) } } } catch (e) {}"
            runJS(j: js, in: webView)
        }

        /// `window.pmChat` bridge — `setStatus` / `appendText` (for non-context pushes, locale is unchanged).
        private func pushPmChat(_ method: String, withJSONObject: [String: Any], in webView: WKWebView) {
            guard
                let data = try? JSONSerialization.data(withJSONObject: withJSONObject, options: []),
                let json = String(data: data, encoding: .utf8)
            else { return }
            // JSON object is valid as a JS value for the single argument: `setStatus` / `appendText`.
            let js = "try { if (window.pmChat && window.pmChat.\(method)) { window.pmChat.\(method)(\(json)) } } catch (e) {}"
            runJS(j: js, in: webView)
        }

        /// Toggles the chat UI “working” state so the last user message shows a spinner (`window.pmChat.setBusy`).
        private func setWebChatBusy(_ busy: Bool, in webView: WKWebView) {
            let s = busy ? "true" : "false"
            let js = "try { if (window.pmChat && window.pmChat.setBusy) { window.pmChat.setBusy(\(s)) } } catch (e) {}"
            runJS(j: js, in: webView)
        }

        private func runJS(j: String, in webView: WKWebView) {
            let win = webView.window
            let frBefore = win?.firstResponder
            let restoreNavigator: Bool = {
                guard let r = frBefore else { return false }
                if r is ProjectNavigatorNSOutlineView { return true }
                if let v = r as? NSView {
                    var c: NSView? = v
                    while let cur = c {
                        if cur.accessibilityIdentifier() == "ProjectNavigator" { return true }
                        c = cur.superview
                    }
                }
                return false
            }()
            webView.evaluateJavaScript(j) { [weak webView, weak win] _, _ in
                guard restoreNavigator, let w = win, let wv = webView, let original = frBefore else { return }
                DispatchQueue.main.async {
                    if let newFR = w.firstResponder as? NSView, newFR.isDescendant(of: wv) {
                        w.makeFirstResponder(original)
                    }
                }
            }
        }

        private func runNativeTurn(
            userPrompt: String,
            routerOverride: String? = nil,
            modelOverride: String? = nil,
            disableTools: [String]? = nil,
            mcpToolsEnabled: Bool = true,
            disabledMcpServers: [String]? = nil,
            webView: WKWebView?
        ) {
            guard let wv = webView else { return }
            guard turnTask == nil else {
                pushPmChat("appendText", withJSONObject: ["role": "system", "text": "A turn is already running. Please wait or press Stop."], in: wv)
                return
            }
            let prompt = userPrompt
            let selection: [String]
            let folder: String
            if let wf = self.workflow, let em = self.editorManager {
                let explorerSel = wf.chatSelectionPaths(editorManager: em)
                pruneStaleContextRemoved(explorer: explorerSel, extra: contextExtraPaths)
                selection = mergedSelectionPaths(explorer: explorerSel)
                folder = wf.chatFolderHint(editorManager: em)
            } else {
                selection = []
                folder = ""
            }
            let systemExtra = self.workflow?.chatNativeSystemContextExtra()
            setWebChatBusy(true, in: wv)
            turnTask = Task.detached(priority: .userInitiated) { [weak self] in
                let emit: ([String: Any]) -> Void = { row in
                    DispatchQueue.main.async {
                        guard let self, let live = self.webView else { return }
                        self.pushPmChat("appendText", withJSONObject: row, in: live)
                    }
                }
                do {
                    // C++ fill_chat_provider_from_runtime resolves router/model/credentials
                    // from the saved profile; only explicit overrides are passed here.
                    // Compact one-liner for tool arguments (mirrors Win32 `args_summary`).
                    func argsSummary(_ args: [String: Any]) -> String {
                        if let paths = args["paths"] as? [Any] {
                            var s = "\(paths.count) file(s)"
                            if let first = paths.first as? String {
                                let name = URL(fileURLWithPath: first).lastPathComponent
                                s += ": \(name)"
                            }
                            if paths.count > 1 { s += " +\(paths.count - 1) more" }
                            return s
                        }
                        if let path = args["path"] as? String {
                            return URL(fileURLWithPath: path).lastPathComponent
                        }
                        if let inputs = args["inputs"] as? [Any] {
                            var s = "\(inputs.count) input(s)"
                            if let first = inputs.first as? String { s += ": \(first)" }
                            return s
                        }
                        if let opts = args["options"] as? [String: Any],
                           let prompt = opts["prompt"] as? String, !prompt.isEmpty {
                            let p = prompt.count > 55 ? String(prompt.prefix(52)) + "…" : prompt
                            return "»\"\(p)\""
                        }
                        return ""
                    }

                    // Script runner for JS calls that are not `appendText` (startRun, appendRunChunk, finishRun).
                    let runScript: (String) -> Void = { js in
                        DispatchQueue.main.async {
                            guard let self, let live = self.webView else { return }
                            self.runJS(j: js, in: live)
                        }
                    }

                    let raw = try PolymechImageEngine.runChatTurn(
                        prompt: prompt,
                        selection: selection,
                        folderHint: folder,
                        systemExtra: systemExtra,
                        routerOverride: routerOverride,
                        modelOverride: modelOverride,
                        disableTools: disableTools,
                        mcpToolsEnabled: mcpToolsEnabled,
                        disabledMcpServers: disabledMcpServers
                    )
                    guard
                        let d = raw.data(using: .utf8),
                        let root = try JSONSerialization.jsonObject(with: d) as? [String: Any]
                    else {
                        emit(["role": "error", "text": "chat turn returned invalid JSON"])
                        DispatchQueue.main.async {
                            guard let s = self else { return }
                            s.endWebChatNativeTurnOnMain()
                        }
                        return
                    }
                    if let events = root["events"] as? [[String: Any]] {
                        for ev in events {
                            let kind = ev["kind"] as? String ?? ""
                            switch kind {

                            case "tool_call":
                                // Parity with Win32 EnqueueScript role:'tool' + ⚡ prefix + args summary.
                                let toolName = ev["tool"] as? String ?? "tool"
                                let detail = ev["detail"] as? [String: Any] ?? [:]
                                let args = (detail["arguments"] as? [String: Any]) ?? detail
                                let summary = argsSummary(args)
                                let text = "⚡ \(toolName)" + (summary.isEmpty ? "" : " \(summary)")
                                emit(["role": "tool", "text": text])

                            case "tool_result":
                                // ⚡ tool ✓ ok/total [± failed] + inline image/file previews.
                                let toolName = ev["tool"] as? String ?? "tool"
                                let detail = ev["detail"] as? [String: Any] ?? [:]
                                let envelope = detail["envelope"] as? [String: Any] ?? [:]
                                let sum = envelope["summary"] as? [String: Any] ?? [:]
                                let total = sum["total"] as? Int ?? 0
                                let okCnt = sum["succeeded"] as? Int ?? 0
                                let fail  = sum["failed"] as? Int ?? 0
                                var resultText = "⚡ \(toolName) ✓ \(okCnt)/\(max(total, okCnt))"
                                if fail > 0 { resultText += " (\(fail) failed)" }
                                // Optional replicate web URL (same as Win32 `— Replicate:` suffix).
                                let results = envelope["results"] as? [[String: Any]] ?? []
                                for r in results {
                                    guard r["ok"] as? Bool == true,
                                          let rObj = r["result"] as? [String: Any],
                                          let repUrl = rObj["replicate_web_url"] as? String,
                                          !repUrl.isEmpty else { continue }
                                    resultText += " — Replicate: \(repUrl)"
                                    break
                                }
                                // Inline image / file previews from result output_paths.
                                let imageExts: Set<String> = ["jpg", "jpeg", "png", "webp", "gif", "avif"]
                                for r in results {
                                    guard r["ok"] as? Bool == true,
                                          let outPath = r["output_path"] as? String,
                                          !outPath.isEmpty else { continue }
                                    let ext = URL(fileURLWithPath: outPath).pathExtension.lowercased()
                                    if imageExts.contains(ext) {
                                        emit(["role": "image", "text": outPath])
                                    } else if !ext.isEmpty {
                                        emit(["role": "file", "text": outPath])
                                    }
                                }
                                // Build extra JS props (toolProvider, toolModel, durationMs) for appendText.
                                var toolRow: [String: Any] = ["role": "tool", "text": resultText]
                                for r in results {
                                    guard let rObj = r["result"] as? [String: Any] else { continue }
                                    if toolRow["toolProvider"] == nil,
                                       let prov = rObj["provider"] as? String, !prov.isEmpty {
                                        toolRow["toolProvider"] = prov
                                    }
                                    if toolRow["toolModel"] == nil,
                                       let mdl = rObj["model"] as? String, !mdl.isEmpty {
                                        toolRow["toolModel"] = mdl
                                    }
                                    if toolRow["toolProvider"] != nil, toolRow["toolModel"] != nil { break }
                                }
                                if let durMs = detail["duration_ms"] as? Int { toolRow["durationMs"] = durMs }
                                emit(toolRow)

                            case "tool_file_progress":
                                // Parity with Win32 ToolFileProgress: `run` → startRun/appendRunChunk/finishRun;
                                // image_transform → role:'image'/'file' per output (in-flight previews).
                                let toolName = ev["tool"] as? String ?? ""
                                let detail = ev["detail"] as? [String: Any] ?? [:]
                                if toolName == "run" {
                                    let runId = detail["id"] as? String ?? ""
                                    let evt   = detail["event"] as? String ?? ""
                                    if evt == "start", !runId.isEmpty {
                                        let cmd = detail["command"] as? String ?? ""
                                        let idLit  = PolymechWebChat.javaScriptStringLiteral(runId)
                                        let cmdLit = PolymechWebChat.javaScriptStringLiteral(cmd)
                                        runScript("try { if (window.pmChat && window.pmChat.startRun) { window.pmChat.startRun(\(idLit), \(cmdLit)) } } catch (e) {}")
                                    } else if evt == "done", !runId.isEmpty {
                                        let exitCode = detail["exit_code"] as? Int ?? -1
                                        let durMs    = detail["duration_ms"] as? Int ?? 0
                                        let idLit = PolymechWebChat.javaScriptStringLiteral(runId)
                                        runScript("try { if (window.pmChat && window.pmChat.finishRun) { window.pmChat.finishRun(\(idLit), \(exitCode), \(durMs)) } } catch (e) {}")
                                    } else {
                                        let chunk  = detail["chunk"] as? String ?? ""
                                        let stream = detail["stream"] as? String ?? "stdout"
                                        if !chunk.isEmpty, !runId.isEmpty {
                                            let idLit  = PolymechWebChat.javaScriptStringLiteral(runId)
                                            let chLit  = PolymechWebChat.javaScriptStringLiteral(chunk)
                                            let stLit  = PolymechWebChat.javaScriptStringLiteral(stream)
                                            runScript("try { if (window.pmChat && window.pmChat.appendRunChunk) { window.pmChat.appendRunChunk(\(idLit), \(chLit), \(stLit)) } } catch (e) {}")
                                        }
                                    }
                                } else {
                                    // image_transform per-file progress: inline preview.
                                    let envelope = detail["envelope"] as? [String: Any] ?? [:]
                                    let results  = envelope["results"] as? [[String: Any]] ?? []
                                    let imageExts: Set<String> = ["jpg", "jpeg", "png", "webp", "gif", "avif"]
                                    for r in results {
                                        guard r["ok"] as? Bool == true,
                                              let outPath = r["output_path"] as? String,
                                              !outPath.isEmpty else { continue }
                                        let ext = URL(fileURLWithPath: outPath).pathExtension.lowercased()
                                        if imageExts.contains(ext) {
                                            emit(["role": "image", "text": outPath])
                                        } else if !ext.isEmpty {
                                            emit(["role": "file", "text": outPath])
                                        }
                                    }
                                }

                            case "thinking":
                                let t = ev["text"] as? String ?? ""
                                if !t.isEmpty { emit(["role": "thinking", "text": t]) }

                            case "assistant_text":
                                let t = ev["text"] as? String ?? ""
                                if !t.isEmpty { emit(["role": "assistant", "text": t]) }

                            case "error":
                                let t = ev["text"] as? String ?? "unknown error"
                                emit(["role": "error", "text": t])

                            default:
                                break
                            }
                        }
                    }
                    let ok = (root["ok"] as? Bool) ?? false
                    if !ok {
                        let err = (root["error"] as? String) ?? "turn failed"
                        emit(["role": "error", "text": err])
                    }
                } catch {
                    emit(["role": "error", "text": "chat turn failed: \(error.localizedDescription)"])
                }
                DispatchQueue.main.async {
                    guard let s = self else { return }
                    s.endWebChatNativeTurnOnMain()
                }
            }
        }

        private func endWebChatNativeTurnOnMain() {
            turnTask = nil
            if let wv = webView { setWebChatBusy(false, in: wv) }
        }

        /// Parity with Win32 `ChatWebPanel::provider_rpc_dispatch` — `callProviderRpc` / `hostProviderRpc`.
        private func dispatchProviderRpcMac(_ o: [String: Any]) -> [String: Any] {
            let method = o["method"] as? String ?? ""
            switch method {
            case "getChatFields":
                let root = PolymechProviderSettingsStore.readSettingsRoot()
                let c = (root["chat"] as? [String: Any]) ?? [:]
                // Return empty strings for unset fields; do not inject silent defaults.
                // Parity with Win32 ChatWebPanel::provider_rpc_dispatch getChatFields.
                let d: [String: Any] = [
                    "router": c["router"] as? String ?? "",
                    "model": c["model"] as? String ?? "",
                    "image_provider": c["image_provider"] as? String ?? "",
                    "image_model": c["image_model"] as? String ?? "",
                    "image_recognition_provider": c["image_recognition_provider"] as? String ?? "",
                    "image_recognition_model": c["image_recognition_model"] as? String ?? "",
                    "video_provider": c["video_provider"] as? String ?? "",
                    "video_model": c["video_model"] as? String ?? "",
                    "stt_provider": c["stt_provider"] as? String ?? "",
                    "stt_model": c["stt_model"] as? String ?? "",
                    "tts_provider": c["tts_provider"] as? String ?? "",
                    "tts_model": c["tts_model"] as? String ?? "",
                    "tts_voice_id": c["tts_voice_id"] as? String ?? "",
                ]
                return ["ok": true, "data": d]
            case "imageProviders":
                let rows = PolymechProviderSettingsStore.imageProviders()
                let arr: [[String: Any]] = rows.map { ["id": $0.name, "label": $0.name] }
                return ["ok": true, "data": arr]
            case "imageModels":
                let pid = o["provider"] as? String ?? ""
                guard let row = PolymechProviderSettingsStore.imageProvider(named: pid) else {
                    return ["ok": false, "error": "unknown image provider: \(pid)"]
                }
                let arr: [[String: Any]] = row.modelOptions.map { ["id": $0, "label": $0] }
                return ["ok": true, "data": arr]
            case "replicate":
                let op = o["op"] as? String ?? ""
                let rep = PolymechProviderSettingsStore.imageProvider(named: "replicate")
                var body: [String: Any] = ["op": op]
                body["api_key"] = rep?.apiKey ?? ""
                if let u = rep?.baseURL, !u.isEmpty {
                    body["base_url"] = u
                } else {
                    body["base_url"] = "https://api.replicate.com/v1"
                }
                if let fr = o["force_refresh"] as? Bool { body["force_refresh"] = fr }
                if op == "models" { body["collection"] = o["collection"] as? String ?? "official" }
                if op == "resolve_collection" { body["model_slug"] = o["model_slug"] as? String ?? "" }
                if op == "openapi_input_flat" { body["model_slug"] = o["model_slug"] as? String ?? "" }
                do {
                    let j = try PolymechImageEngine.runReplicateRequest(body)
                    guard
                        let d = j.data(using: .utf8),
                        let root = try JSONSerialization.jsonObject(with: d) as? [String: Any]
                    else { return ["ok": false, "error": "invalid Replicate JSON"] }
                    if op == "collections" {
                        return ["ok": true, "data": ["collections": root["collections"] ?? []]]
                    }
                    if op == "models" {
                        return ["ok": true, "data": ["models": root["models"] ?? []]]
                    }
                    if op == "resolve_collection" {
                        let col: Any = (root["collection"] as? String) ?? NSNull()
                        return ["ok": true, "data": ["collection": col]]
                    }
                    if op == "openapi_input_flat" {
                        let slug = root["model_slug"] as? String ?? ""
                        let req = root["required"] as? [Any] ?? []
                        let props = root["properties"] as? [String: Any] ?? [:]
                        return ["ok": true, "data": ["model_slug": slug, "required": req, "properties": props]]
                    }
                    return ["ok": false, "error": "replicate: unknown op"]
                } catch {
                    return ["ok": false, "error": error.localizedDescription]
                }
            case "openrouter":
                let op = o["op"] as? String ?? ""
                guard op == "models" else {
                    return ["ok": false, "error": "openrouter: unknown op"]
                }
                let textOR = PolymechProviderSettingsStore.textProviders().first(where: { $0.name == "openrouter" })
                let imgOR = PolymechProviderSettingsStore.imageProvider(named: "openrouter")
                var body: [String: Any] = [
                    "op": "models",
                    "force_refresh": o["force_refresh"] as? Bool ?? false,
                ]
                if let ak = o["api_key"] as? String, !ak.isEmpty {
                    body["api_key"] = ak
                } else {
                    body["api_key"] = textOR?.apiKey ?? imgOR?.apiKey ?? ""
                }
                if let bu = o["base_url"] as? String, !bu.isEmpty {
                    body["base_url"] = bu
                } else {
                    var u = textOR?.baseURL ?? imgOR?.baseURL ?? ""
                    if u.isEmpty { u = "https://openrouter.ai/api/v1" }
                    body["base_url"] = u
                }
                do {
                    let j = try PolymechImageEngine.runOpenRouterRequest(body)
                    guard
                        let d = j.data(using: .utf8),
                        let root = try JSONSerialization.jsonObject(with: d) as? [String: Any],
                        let arr = root["models"] as? [[String: Any]]
                    else { return ["ok": false, "error": "invalid OpenRouter JSON"] }
                    return ["ok": true, "data": ["models": arr]]
                } catch {
                    return ["ok": false, "error": error.localizedDescription]
                }
            case "saveChatFields":
                // Parity with Win32 ChatWebPanel::provider_rpc_dispatch saveChatFields.
                do {
                    var root = PolymechProviderSettingsStore.readSettingsRoot()
                    var c = (root["chat"] as? [String: Any]) ?? [:]
                    if let v = o["router"] as? String { c["router"] = v }
                    if let v = o["model"] as? String { c["model"] = v }
                    if let v = o["image_provider"] as? String { c["image_provider"] = v }
                    if let v = o["image_model"] as? String { c["image_model"] = v }
                    if let v = o["image_recognition_provider"] as? String { c["image_recognition_provider"] = v }
                    if let v = o["image_recognition_model"] as? String { c["image_recognition_model"] = v }
                    if let v = o["video_provider"] as? String { c["video_provider"] = v }
                    if let v = o["video_model"] as? String { c["video_model"] = v }
                    if let v = o["stt_provider"] as? String { c["stt_provider"] = v }
                    if let v = o["stt_model"] as? String { c["stt_model"] = v }
                    if let v = o["tts_provider"] as? String { c["tts_provider"] = v }
                    if let v = o["tts_model"] as? String { c["tts_model"] = v }
                    if let v = o["tts_voice_id"] as? String { c["tts_voice_id"] = v }
                    root["chat"] = c
                    try PolymechProviderSettingsStore.writeSettingsRoot(root)
                    return ["ok": true]
                } catch {
                    return ["ok": false, "error": error.localizedDescription]
                }
            case "audioProviders":
                // Parity with Win32 AudioSelectorController provider tables.
                let isStt = (o["stt"] as? Bool) ?? true
                let providers: [[String: Any]] = isStt
                    ? [["id": "pixlwiz", "label": "PixlWiz (proxy)"],
                       ["id": "elevenlabs", "label": "ElevenLabs (direct)"]]
                    : [["id": "pixlwiz", "label": "PixlWiz (proxy)"],
                       ["id": "elevenlabs", "label": "ElevenLabs (direct)"]]
                return ["ok": true, "data": providers]
            case "audioModels":
                let providerId = o["provider"] as? String ?? ""
                let isStt = (o["stt"] as? Bool) ?? true
                let models: [[String: Any]]
                switch providerId {
                case "elevenlabs":
                    models = isStt
                        ? [["id": "scribe_v2_realtime", "label": "Scribe v2 (real-time)"]]
                        : [["id": "eleven_v3",              "label": "ElevenLabs v3"],
                           ["id": "eleven_multilingual_v2", "label": "Multilingual v2"],
                           ["id": "eleven_turbo_v2_5",      "label": "Turbo v2.5 (low latency)"],
                           ["id": "eleven_flash_v2_5",      "label": "Flash v2.5 (ultra-fast)"]]
                default: // pixlwiz or unknown
                    models = isStt
                        ? [["id": "pixlwiz-speech-to-text", "label": "PixlWiz STT (Whisper-1)"]]
                        : [["id": "pixlwiz-speech",         "label": "PixlWiz TTS (Multilingual v2)"],
                           ["id": "pixlwiz-speech-turbo",   "label": "PixlWiz TTS Turbo (v2.5)"]]
                }
                return ["ok": true, "data": models]
            case "listChatSessions":
                return ["ok": true, "data": ["sessions": []]]
            case "loadChatSession":
                return ["ok": false, "error": "unimplemented"]
            case "saveChatSession":
                return ["ok": true]
            case "deleteChatSession":
                return ["ok": true]
            default:
                return ["ok": false, "error": "unknown method: \(method)"]
            }
        }

        private func pushProviderRpcReplyToWeb(_ payload: [String: Any]) {
            guard let wv = webView,
                  let data = try? JSONSerialization.data(withJSONObject: payload, options: []),
                  let jsonString = String(data: data, encoding: .utf8) else { return }
            let lit = PolymechWebChat.javaScriptStringLiteral(jsonString)
            let js = "try { if (window.__pmDispatchFromHost) { window.__pmDispatchFromHost(\(lit)) } } catch (e) {}"
            runJS(j: js, in: wv)
        }

        private func announceTextProviderIfNeeded(webView: WKWebView?) {
            guard let wv = webView, !didAnnounceProviderForPage else { return }
            didAnnounceProviderForPage = true
            let root = PolymechProviderSettingsStore.readSettingsRoot()
            let chat = root["chat"] as? [String: Any] ?? [:]
            let provider = chat["router"] as? String ?? ""
            let model = chat["model"] as? String ?? ""
            let line: String
            if provider.isEmpty {
                line = "Text provider not configured (set chat.router in settings)"
            } else {
                line = "Text provider: \(provider)" + (model.isEmpty ? "" : " (\(model))")
            }
            pushPmChat("appendText", withJSONObject: ["role": "system", "text": line], in: wv)
        }

        func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) {
            let u = webView.url?.absoluteString ?? fileURL.absoluteString
            PolymechWebChatHostLog.nav("webchat provisional url=\(u)")
        }

        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
            PolymechWebChatHostLog.navError("webchat provisional failed: \(error.localizedDescription)")
        }

        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
            PolymechWebChatHostLog.navError("webchat navigation failed: \(error.localizedDescription)")
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            self.webView = webView
            let u = webView.url?.absoluteString ?? fileURL.path
            PolymechWebChatHostLog.nav("webchat didFinish url=\(u)")
            pushContext(webView: webView)
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(fileURL: fileURL, readAccessURL: readAccessURL, appLanguage: appLanguage) }

    func makeNSView(context: Context) -> WKWebView {
        let c = context.coordinator
        c.workflow = workflow
        c.editorManager = editorManager
        if c.contextNotification == nil {
            c.contextNotification = NotificationCenter.default.addObserver(
                forName: .polymechNavigatorContextChanged,
                object: nil,
                queue: .main
            ) { [weak c] n in
                guard let c, let doc = n.object as? WorkspaceDocument, doc === c.workflow?.workspace else { return }
                c.pushContext(webView: c.webView)
            }
        }
        let config = WKWebViewConfiguration()
        config.setURLSchemeHandler(c.pmChatMediaSchemeHandler, forURLScheme: "pmasset")
        c.pmChatMediaSchemeHandler.workspaceRootURL = workflow.workspace?.workspaceFileManager?.folderUrl
        let imgRewrite = WKUserScript(
            source: PolymechWebChat.macChatFileImageSrcRewriteScript,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        )
        config.userContentController.addUserScript(imgRewrite)
        let consoleTap = WKUserScript(
            source: PolymechWebChat.webConsoleCaptureScript,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        )
        config.userContentController.addUserScript(consoleTap)
        let poly = WKUserScript(
            source: PolymechWebChat.webkitBridgePolyfill,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        )
        config.userContentController.addUserScript(poly)
        config.userContentController.add(context.coordinator, name: "pmChatWebLog")
        config.userContentController.add(context.coordinator, name: "pmChatHost")
        let wv = PolymechChatShellDropWKWebView(frame: .zero, configuration: config)
        wv.navigationDelegate = context.coordinator
        wv.onNativePathsDropped = { [weak c, weak wv] paths in
            guard let c, let wv else { return }
            c.mergeAddContextPathStrings(paths, webView: wv)
        }
        context.coordinator.webView = wv
        wv.allowsBackForwardNavigationGestures = true
        // Win32 WebView2 fills the dock; SwiftUI often gives WKWebView zero height until this is set.
        wv.translatesAutoresizingMaskIntoConstraints = true
        wv.autoresizingMask = [.width, .height]
        return wv
    }

    func updateNSView(_ webView: WKWebView, context: Context) {
        let c = context.coordinator
        c.fileURL = fileURL
        c.readAccessURL = readAccessURL
        c.workflow = workflow
        c.editorManager = editorManager
        c.webView = webView
        c.pmChatMediaSchemeHandler.workspaceRootURL = workflow.workspace?.workspaceFileManager?.folderUrl
        let langChanged = c.appLanguage != appLanguage
        c.appLanguage = appLanguage
        if langChanged {
            c.lastPushedKey = nil
            c.lastPushedLocaleTag = nil
            c.pushContext(webView: webView)
        }
        if c.loadedFilePath != fileURL.path {
            c.loadedFilePath = fileURL.path
            c.didAnnounceProviderForPage = false
            c.lastPushedKey = nil
            c.lastPushedLocaleTag = nil
            PolymechWebChatHostLog.wkLoad(file: fileURL, readAccess: readAccessURL)
            webView.loadFileURL(fileURL, allowingReadAccessTo: readAccessURL)
        }
    }

    static func dismantleNSView(_ nsView: WKWebView, coordinator: Coordinator) {
        if let o = coordinator.contextNotification {
            NotificationCenter.default.removeObserver(o)
            coordinator.contextNotification = nil
        }
        coordinator.turnTask?.cancel()
        coordinator.turnTask = nil
        nsView.stopLoading()
        nsView.configuration.userContentController.removeScriptMessageHandler(forName: "pmChatWebLog")
        nsView.configuration.userContentController.removeScriptMessageHandler(forName: "pmChatHost")
        // WKWebViewConfiguration owns the scheme handler and is deallocated with the WKWebView —
        // calling setURLSchemeHandler(nil:) on an already-registered scheme throws an
        // NSInvalidArgumentException, so we just let the config clean up itself.
    }
}

// MARK: - Win32 `CChatView`–style context (see `ChatPanel::SetContext`)

extension PolymechWorkflowState {
    /// Kept for call sites that do not have `EditorManager`; does not use the active editor tab.
    var explorerFolderPathForChat: String {
        chatFolderHint(editorManager: nil)
    }
}

extension WorkspaceDocument {
    @MainActor
    func openPolymechWebChatTab() {
        if let b = PolymechWebChat.bundledChatHTMLURL() {
            let file = CEWorkspaceFile(url: b)
            editorManager?.openTab(item: file, asTemporary: false)
            return
        }
        if let root = fileURL, let html = PolymechWebChat.resolvedChatHTMLFromWorkspace(root) {
            let file = CEWorkspaceFile(url: html)
            editorManager?.openTab(item: file, asTemporary: false)
        }
    }

    @MainActor
    func openPolymechWebChatInInspector() {
        guard canOpenPolymechWebChatTab() else { return }
        let keyWC = windowControllers.compactMap { $0 as? CodeEditWindowController }.first { $0.window?.isKeyWindow == true }
        let anyWC = windowControllers.compactMap { $0 as? CodeEditWindowController }.first
        (keyWC ?? anyWC)?.openPolymechWebChatInInspector()
    }

    @MainActor
    func canOpenPolymechWebChatTab() -> Bool {
        if PolymechWebChat.bundledChatHTMLURL() != nil { return true }
        if let root = fileURL { return PolymechWebChat.resolvedChatHTMLFromWorkspace(root) != nil }
        return false
    }
}
