import Foundation

enum PolymechCommand: String, CaseIterable, Identifiable, Sendable {
    case resize
    case meta
    case compress
    case find
    case transform
    case chat

    var id: String { rawValue }

    /// English-style short label (stable for logs if needed); prefer `displayName` in UI.
    var rawDisplayName: String {
        switch self {
        case .resize: "Resize"
        case .meta: "Meta"
        case .compress: "Compress"
        case .find: "Find"
        case .transform: "Transform"
        case .chat: "Chat"
        }
    }

    /// User-visible, localized (toolbar, inspector).
    var displayName: String {
        switch self {
        case .resize: NSLocalizedString("Polymech.command.resize", comment: "Polymech toolbar")
        case .meta: NSLocalizedString("Polymech.command.meta", comment: "Polymech toolbar")
        case .compress: NSLocalizedString("Polymech.command.compress", comment: "Polymech toolbar")
        case .find: NSLocalizedString("Polymech.command.find", comment: "Polymech toolbar")
        case .transform: NSLocalizedString("Polymech.command.transform", comment: "Polymech toolbar")
        case .chat: NSLocalizedString("Polymech.command.chat", comment: "Polymech toolbar")
        }
    }
}

@MainActor
final class PolymechWorkflowState: ObservableObject {
    /// Command user chose on the toolbar (arm — does not run until **Run**).
    @Published var command: PolymechCommand?
    /// URLs currently selected in the project navigator (files and/or folders).
    /// Not `@Published`: updating it used to re-render SwiftUI and `PolymechWebChatWKView`, whose `evaluateJavaScript` path stole keyboard focus from the outline. Chat/tooling refresh via ``Notification.Name.polymechNavigatorContextChanged`` instead.
    private(set) var navigatorSelectionURLs: [URL] = []

    weak var workspace: WorkspaceDocument?

    init(workspace: WorkspaceDocument? = nil) {
        self.workspace = workspace
    }

    func updateNavigatorSelection(_ urls: [URL]) {
        if urls.map(\.path) == navigatorSelectionURLs.map(\.path) { return }
        navigatorSelectionURLs = urls
        if let workspace {
            NotificationCenter.default.post(name: .polymechNavigatorContextChanged, object: workspace)
        }
    }

    /// Image files: navigator selection (files and **recursively** from selected folders), else the
    /// active tab when the navigator has no selection (e.g. only the `..` row was sending URLs).
    var effectiveImageInputURLs: [URL] {
        if !navigatorSelectionURLs.isEmpty {
            let fromNav = Self.imageURLsResolvingFolderSelections(navigatorSelectionURLs)
            if !fromNav.isEmpty { return fromNav }
            return []
        }
        if let u = workspace?.editorManager?.activeEditor.selectedTab?.file.url, PolymechImageURL.isLikelyImageFile(u) {
            return [u]
        }
        return []
    }

    /// Short note for the inspector (Resize / Meta / …): how many inputs **Run** will use.
    var imageCommandInputsSummary: String {
        let urls = effectiveImageInputURLs
        if urls.isEmpty {
            if !navigatorSelectionURLs.isEmpty {
                return NSLocalizedString(
                    "Polymech.input.noImagesUnderSelection",
                    comment: "Polymech inspector"
                )
            }
            return NSLocalizedString(
                "Polymech.input.noImageInput",
                comment: "Polymech inspector"
            )
        }
        if urls.count == 1 {
            return String(
                format: NSLocalizedString("Polymech.input.runUsesOne", comment: "Polymech inspector"),
                urls[0].lastPathComponent
            )
        }
        return String(
            format: NSLocalizedString("Polymech.input.runUsesMany", comment: "Polymech inspector"),
            urls.count,
            urls[0].lastPathComponent,
            urls.count - 1
        )
    }

    /// For **Find** — search roots from navigator or workspace / editor.
    var effectiveFindRootPaths: [String] {
        if let p = workspace?.validatedNavigatorWorkingDirectoryPath() {
            return [p]
        }
        if !navigatorSelectionURLs.isEmpty {
            return navigatorSelectionURLs.map { u in
                if directoryURLIfExists(u) { return u.path }
                return u.deletingLastPathComponent().path
            }
        }
        if let w = workspace?.fileURL {
            if directoryURLIfExists(w) { return [w.path] }
            return [w.deletingLastPathComponent().path]
        }
        if let u = workspace?.editorManager?.activeEditor.selectedTab?.file.url {
            return [u.deletingLastPathComponent().path]
        }
        return []
    }

    func canRun(pendingCommand: PolymechCommand) -> Bool {
        switch pendingCommand {
        case .find:
            return !effectiveFindRootPaths.isEmpty
        case .chat:
            return workspace.map { $0.canOpenPolymechWebChatTab() } ?? false
        case .resize:
            guard !effectiveImageInputURLs.isEmpty else { return false }
            let s = PolymechImageSettingsStore.load()
            return PolymechResizeFormData.from(json: s.resize).isValidForEngineRun
        case .meta, .compress, .transform:
            return !effectiveImageInputURLs.isEmpty
        }
    }

    var canRun: Bool {
        guard let c = command else { return false }
        return canRun(pendingCommand: c)
    }

    var selectionDescription: String {
        var lines: [String] = []
        if let root = workspace?.workspaceFileManager?.folderUrl {
            lines.append(
                String(
                    format: NSLocalizedString("Polymech.selection.workspaceLine", comment: "Polymech help"),
                    root.lastPathComponent
                )
            )
        }
        if let p = workspace?.validatedNavigatorWorkingDirectoryPath() {
            lines.append(
                String(
                    format: NSLocalizedString("Polymech.selection.workingFolderLine", comment: "Polymech help"),
                    URL(fileURLWithPath: p).lastPathComponent
                )
            )
        }
        if !navigatorSelectionURLs.isEmpty {
            if navigatorSelectionURLs.count == 1, let f = navigatorSelectionURLs.first {
                var isDir: ObjCBool = false
                let kind: String
                if FileManager.default.fileExists(atPath: f.path, isDirectory: &isDir), isDir.boolValue {
                    kind = NSLocalizedString("Polymech.kind.folder", comment: "")
                } else {
                    kind = NSLocalizedString("Polymech.kind.file", comment: "")
                }
                lines.append(
                    String(
                        format: NSLocalizedString("Polymech.selection.oneItem", comment: "Polymech help"),
                        f.lastPathComponent,
                        kind
                    )
                )
            } else {
                lines.append(
                    String(
                        format: NSLocalizedString("Polymech.selection.multi", comment: "Polymech help"),
                        navigatorSelectionURLs.count
                    )
                )
            }
        } else {
            if let f = workspace?.editorManager?.activeEditor.selectedTab?.file {
                lines.append(
                    String(
                        format: NSLocalizedString("Polymech.selection.activeTabNoNav", comment: "Polymech help"),
                        f.name
                    )
                )
            }
        }
        if lines.isEmpty, let f = workspace?.editorManager?.activeEditor.selectedTab?.file {
            return String(
                format: NSLocalizedString("Polymech.selection.activeTabOnly", comment: "Polymech help"),
                f.name
            )
        }
        if lines.isEmpty {
            return NSLocalizedString("Polymech.selection.noFile", comment: "Polymech help")
        }
        return lines.joined(separator: "\n")
    }

    func setCommand(_ c: PolymechCommand) {
        if c == .chat {
            workspace?.openPolymechWebChatInInspector()
        }
        command = c
        NotificationCenter.default.post(
            name: .polymechCommandArmed,
            object: c
        )
    }

    func run(jobs: PolymechImageJobCenter) {
        guard let c = command, canRun(pendingCommand: c) else { return }
        let s = PolymechImageSettingsStore.load()
        switch c {
        case .resize:
            let rd = PolymechResizeFormData.from(json: s.resize)
            let workspaceDoc = self.workspace
            for u in effectiveImageInputURLs {
                let infix = rd.effectiveBasenameInfix()
                let out = PolymechImageURL.imageCommandOutput(
                    input: u,
                    outputMode: rd.output_mode,
                    basenameInfix: infix
                )
                let ip = u.path
                let inPlace = rd.output_mode == "in_place"
                let titleU = u.lastPathComponent
                let resizeTitle: String = inPlace
                    ? String(
                        format: NSLocalizedString("Polymech.job.title.resizeInPlace", comment: "Activity queue"),
                        titleU
                    )
                    : String(
                        format: NSLocalizedString("Polymech.job.title.resizeTo", comment: "Activity queue"),
                        titleU,
                        out.lastPathComponent
                    )
                let resPath: String? = inPlace ? ip : out.path
                jobs.run(title: resizeTitle, sourcePath: ip, resultPath: resPath) {
                    let r = try PolymechImageEngine.runResize(input: ip, output: out.path, settings: s)
                    if inPlace {
                        let url = u
                        let doc = workspaceDoc
                        DispatchQueue.main.async {
                            NotificationCenter.default.post(
                                name: .polymechImageFileDidWriteInPlace,
                                object: doc,
                                userInfo: ["url": url]
                            )
                        }
                    }
                    return r
                }
            }
        case .meta:
            for u in effectiveImageInputURLs {
                let ip = u.path
                jobs.run(
                    title: String(
                        format: NSLocalizedString("Polymech.job.title.meta", comment: "Activity queue"),
                        u.lastPathComponent
                    ),
                    sourcePath: ip,
                    resultPath: nil
                ) {
                    try PolymechImageEngine.runMeta(input: ip, settings: s)
                }
            }
        case .compress:
            let cd = PolymechCompressFormData.from(json: s.compress)
            let workspaceForCompress = self.workspace
            for u in effectiveImageInputURLs {
                let infix = cd.effectiveBasenameInfix()
                let out = PolymechImageURL.imageCommandOutput(
                    input: u,
                    outputMode: cd.output_mode,
                    basenameInfix: infix
                )
                let ip = u.path
                let inPlace = cd.output_mode == "in_place"
                let titleU = u.lastPathComponent
                let compressTitle: String = inPlace
                    ? String(
                        format: NSLocalizedString("Polymech.job.title.compressInPlace", comment: "Activity queue"),
                        titleU
                    )
                    : String(
                        format: NSLocalizedString("Polymech.job.title.compressTo", comment: "Activity queue"),
                        titleU,
                        out.lastPathComponent
                    )
                let resCPath: String? = inPlace ? ip : out.path
                jobs.run(title: compressTitle, sourcePath: ip, resultPath: resCPath) {
                    let r = try PolymechImageEngine.runCompress(input: ip, output: out.path, settings: s)
                    if inPlace {
                        let url = u
                        let doc = workspaceForCompress
                        DispatchQueue.main.async {
                            NotificationCenter.default.post(
                                name: .polymechImageFileDidWriteInPlace,
                                object: doc,
                                userInfo: ["url": url]
                            )
                        }
                    }
                    return r
                }
            }
        case .find:
            let roots = effectiveFindRootPaths
            guard !roots.isEmpty else { return }
            let findPath: String
            if roots.count == 1 {
                findPath = roots[0]
            } else {
                findPath = roots.joined(separator: " · ")
            }
            jobs.run(
                title: NSLocalizedString("Polymech.job.title.find", comment: "Activity queue"),
                sourcePath: findPath,
                resultPath: nil
            ) {
                try PolymechImageEngine.runFind(inputs: roots, settings: s)
            }
        case .transform:
            for u in effectiveImageInputURLs {
                let out = PolymechImageURL.siblingOutput(for: u, tag: "transform")
                let ip = u.path
                jobs.run(
                    title: String(
                        format: NSLocalizedString("Polymech.job.title.transform", comment: "Activity queue"),
                        u.lastPathComponent
                    ),
                    sourcePath: ip,
                    resultPath: out.path
                ) {
                    try PolymechImageEngine.runTransform(input: ip, output: out.path, settings: s)
                }
            }
        case .chat:
            workspace?.openPolymechWebChatInInspector()
        }
    }

    func clearCommand() {
        command = nil
    }

    private func directoryURLIfExists(_ url: URL) -> Bool {
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDir) else { return false }
        return isDir.boolValue
    }

    // MARK: - Chat / agent context (Win32 `ChatPanel::SetContext`: explorer + active editor)

    /// UTF-8 paths: project navigator selection **plus** the active editor tab when it is not already selected
    /// (so an open preview / editor tab is still in chat context while the outline keeps another selection).
    func chatSelectionPaths(editorManager: EditorManager?) -> [String] {
        let em = editorManager ?? workspace?.editorManager
        var out: [String] = []
        var seenLower = Set<String>()
        func add(_ path: String) {
            let t = path.trimmingCharacters(in: .whitespacesAndNewlines)
            if t.isEmpty { return }
            let k = t.lowercased()
            if seenLower.contains(k) { return }
            seenLower.insert(k)
            out.append(t)
        }
        for u in navigatorSelectionURLs { add(u.path) }
        if let u = em?.activeEditor.selectedTab?.file.url { add(u.path) }
        return out
    }

    /// Folder for tool / status resolution: persisted “entered” directory (MC-style), else selection, else workspace.
    func chatFolderHint(editorManager: EditorManager?) -> String {
        if let p = workspace?.validatedNavigatorWorkingDirectoryPath() {
            return p
        }
        let first = navigatorSelectionURLs.first
            ?? editorManager?.activeEditor.selectedTab?.file.url
            ?? workspace?.editorManager?.activeEditor.selectedTab?.file.url
        if let u = first {
            if directoryURLIfExists(u) { return u.path }
            return u.deletingLastPathComponent().path
        }
        if let w = workspace?.fileURL {
            if directoryURLIfExists(w) { return w.path }
            return w.deletingLastPathComponent().path
        }
        return ""
    }

    /// UTF-8 path of the opened CodeEdit workspace / project root, for chat UI and the native agent.
    func chatWorkspacePath() -> String {
        workspace?.workspaceFileManager?.folderUrl.path ?? ""
    }

    /// Extra system context for the native chat turn: workspace + browse folder (paths also appear in `selection` / `folder`).
    func chatNativeSystemContextExtra() -> String? {
        var lines: [String] = []
        let w = chatWorkspacePath()
        if !w.isEmpty { lines.append("The CodeEdit workspace (project) root is: \(w)") }
        if let p = workspace?.validatedNavigatorWorkingDirectoryPath(), !p.isEmpty {
            lines.append("The navigator’s current working folder (browse) is: \(p)")
        }
        if lines.isEmpty { return nil }
        return lines.joined(separator: "\n")
    }

    /// Expands folders to image files (same logic as ``effectiveImageInputURLs`` for a navigator multi-select). Used by the **Transform** tab of the style-preset sheet and the image toolbar.
    nonisolated static func imageURLsForCommand(from selectedFiles: [CEWorkspaceFile]) -> [URL] {
        imageURLsResolvingFolderSelections(selectedFiles.map(\.url))
    }

    nonisolated private static func imageURLsResolvingFolderSelections(_ urls: [URL]) -> [URL] {
        if urls.isEmpty { return [] }
        var seen = Set<String>()
        var out: [URL] = []
        let fm = FileManager.default
        for u in urls {
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: u.path, isDirectory: &isDir) else { continue }
            if isDir.boolValue {
                guard let en = fm.enumerator(
                    at: u,
                    includingPropertiesForKeys: [.isRegularFileKey],
                    options: [.skipsHiddenFiles]
                ) else { continue }
                for case let fileURL as URL in en {
                    if (try? fileURL.resourceValues(forKeys: [.isRegularFileKey]))?.isRegularFile != true { continue }
                    guard PolymechImageURL.isLikelyImageFile(fileURL) else { continue }
                    let path = fileURL.path
                    if seen.insert(path).inserted { out.append(fileURL) }
                }
            } else if PolymechImageURL.isLikelyImageFile(u), seen.insert(u.path).inserted {
                out.append(u)
            }
        }
        return out.sorted { $0.path < $1.path }
    }
}

extension Notification.Name {
    static let polymechCommandArmed = Notification.Name("PolymechCommandArmed")
    /// Posted after resize/compress wrote **in place**; `userInfo["url"]` is the `file://` `URL` (refresh open previews).
    static let polymechImageFileDidWriteInPlace = Notification.Name("polymechImageFileDidWriteInPlace")
    /// Posted with `object: WorkspaceDocument` when navigator selection or working-directory changes (Polymech / chat context).
    static let polymechNavigatorContextChanged = Notification.Name("polymechNavigatorContextChanged")
    /// Posted with `object: WorkspaceDocument` when the persisted navigator working directory (browse root) changes — outline should reload; do not use for “selection only” updates.
    static let polymechNavigatorBrowseRootChanged = Notification.Name("polymechNavigatorBrowseRootChanged")
    /// Reveal a path in the project navigator. `object` is the `WorkspaceDocument`; `userInfo["path"]` is a UTF-8 filesystem path.
    static let polymechRevealFileInNavigator = Notification.Name("polymechRevealFileInNavigator")
}
