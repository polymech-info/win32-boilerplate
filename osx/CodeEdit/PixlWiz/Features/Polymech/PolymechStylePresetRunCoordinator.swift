import SwiftUI

/// Shared “style preset” sheet + job enqueue (navigator **Transform** context menu and **View** menu).
///
/// Presents via ``WorkspaceDocument.polymechStylePresetSheet`` + SwiftUI `.sheet` — not a child
/// `NSWindow` + `NSHostingView` (those could crash in `objc_release` during teardown on some macOS / VM
/// setups when the queue UI updated in the same run loop).
enum PolymechStylePresetRunCoordinator {
    /// Label for a preset in any menu (matches ``ProjectNavigatorMenu`` / `transformSubmenu()`).
    nonisolated static func menuItemTitle(for p: PolymechStylePreset) -> String {
        if p.name.isEmpty { return p.prompt }
        if p.icon.isEmpty { return p.name }
        return "\(p.icon) \(p.name)"
    }

    /// **View** menu: `chat` selection + image inputs (same as toolbar / `effectiveImageInputURLs` when the navigator is empty).
    @MainActor
    static func mainMenuContext(workspace: WorkspaceDocument) -> (paths: [String], imageURLs: [URL]) {
        let w = workspace.polymechWorkflow
        let paths = w.chatSelectionPaths(editorManager: workspace.editorManager).filter { !$0.isEmpty }
        let imageURLs = w.effectiveImageInputURLs
        return (paths, imageURLs)
    }

    @MainActor
    static func canRunFromMainMenu(workspace: WorkspaceDocument?) -> Bool {
        guard let workspace else { return false }
        return !mainMenuContext(workspace: workspace).paths.isEmpty
    }

    /// Shows the Agent / Transform sheet for the given workspace (driven by ``WorkspaceDocument.polymechStylePresetSheet``).
    @MainActor
    static func present(
        preset: PolymechStylePreset,
        workspace: WorkspaceDocument,
        paths: [String],
        imageURLs: [URL]
    ) {
        guard !paths.isEmpty else { return }
        workspace.polymechStylePresetSheet = PolymechStylePresetSheetContext(
            preset: preset,
            paths: paths,
            imageURLs: imageURLs
        )
    }

    @MainActor
    @ViewBuilder
    static func stylePresetRunSheet(workspace: WorkspaceDocument, context: PolymechStylePresetSheetContext) -> some View {
        let preset = context.preset
        let paths = context.paths
        let imageURLs = context.imageURLs
        let doc = workspace
        let imageInputCount = imageURLs.count

        let dismissSoon: () -> Void = {
            DispatchQueue.main.async {
                workspace.polymechStylePresetSheet = nil
            }
        }

        PolymechTransformStylePresetRunView(
            preset: preset,
            imageInputCount: imageInputCount,
            onCancel: { dismissSoon() },
            onRunAgent: { finalPrompt in
                Task { @MainActor in
                    let w = doc.polymechWorkflow
                    let em = doc.editorManager
                    let folderHint = w.chatFolderHint(editorManager: em)
                    let systemExtra = w.chatNativeSystemContextExtra()
                    let t = finalPrompt.trimmingCharacters(in: .whitespacesAndNewlines)
                    let preview: String = t.count > 48 ? (String(t.prefix(45)) + "…") : t
                    let title = preset.icon.isEmpty ? preset.name : "\(preset.icon) \(preset.name)"
                    let jobTitle: String
                    if !preset.name.isEmpty {
                        jobTitle = String(
                            format: NSLocalizedString("Polymech.navigator.presetJobTitle", comment: "Navigator context"),
                            title,
                            preview
                        )
                    } else {
                        jobTitle = String(
                            format: NSLocalizedString("Polymech.chat.jobTitle", comment: "Navigator context"),
                            preview
                        )
                    }
                    _ = PolymechImageJobCenter.shared.run(title: jobTitle, sourcePath: paths.first, resultPath: nil) {
                        // Pass only explicit saved overrides; C++ runtime_settings resolves
                        // provider, model, and credentials from the app profile.
                        let rootRead = PolymechProviderSettingsStore.readSettingsRoot()
                        let c = (rootRead["chat"] as? [String: Any]) ?? [:]
                        let routerOverride = c["router"] as? String
                        let modelOverride = c["model"] as? String
                        return try PolymechImageEngine.runChatTurn(
                            prompt: finalPrompt,
                            selection: paths,
                            folderHint: folderHint,
                            systemExtra: systemExtra,
                            routerOverride: (routerOverride?.isEmpty == false) ? routerOverride : nil,
                            modelOverride: (modelOverride?.isEmpty == false) ? modelOverride : nil
                        )
                    }
                    dismissSoon()
                }
            },
            onRunTransform: { transformJson in
                let urls = imageURLs
                Task { @MainActor in
                    guard !urls.isEmpty else {
                        dismissSoon()
                        return
                    }
                    var s = PolymechImageSettingsStore.load()
                    s.transform = transformJson
                    let titleFmt = NSLocalizedString("Polymech.job.title.transform", comment: "Activity queue")
                    let items: [PolymechImageJobCenter.SequentialWorkItem] = urls.map { u in
                        let out = PolymechImageURL.siblingOutput(for: u, tag: "transform")
                        return PolymechImageJobCenter.SequentialWorkItem(
                            title: String(format: titleFmt, u.lastPathComponent),
                            sourcePath: u.path,
                            resultPath: out.path,
                            work: { try PolymechImageEngine.runTransform(input: u.path, output: out.path, settings: s) }
                        )
                    }
                    PolymechImageJobCenter.shared.runSequential(items)
                    dismissSoon()
                }
            }
        )
        .frame(minWidth: 520, minHeight: 480)
    }
}
