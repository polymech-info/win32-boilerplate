import SwiftUI

/// Parallels `CSettingsView` / `SettingsPanel.h` modes: structured controls + optional raw JSON.
struct PolymechImageInspectorView: View {
    @EnvironmentObject private var workflow: PolymechWorkflowState
    @EnvironmentObject private var workspace: WorkspaceDocument
    @State private var settings = PolymechImageSettingsStore.load()
    @State private var showAdvancedJSON = false
    /// Decoupled from `workflow` body refresh: navigator selection is updated without `@Published` (see `PolymechWorkflowState`).
    @State private var selectionContextLabel: String = ""
    @State private var imageInputSummaryLabel: String = ""

    var body: some View {
        Form {
            Section {
                Text(selectionContextLabel)
                    .font(.callout)
                if shouldShowImageInputSummary {
                    Text(imageInputSummaryLabel)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                if let c = workflow.command {
                    HStack(alignment: .firstTextBaseline, spacing: 6) {
                        Text(c.displayName).fontWeight(.semibold)
                    }
                    .font(.subheadline)
                } else {
                    Text("Use the title bar to choose a command. Options save as you edit; then Run.")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
            }
            commandForms
        }
        .onAppear {
            refreshInspectorContextLabels()
        }
        .onChange(of: settings) { _, new in
            PolymechImageSettingsStore.save(new)
        }
        .onReceive(NotificationCenter.default.publisher(for: .polymechNavigatorContextChanged)) { _ in
            refreshInspectorContextLabels()
        }
        .onChange(of: workspace.editorManager?.activeEditor.selectedTab?.file.id) { _, _ in
            refreshInspectorContextLabels()
        }
        .onChange(of: workflow.command) { _, _ in
            refreshInspectorContextLabels()
        }
        .safeAreaInset(edge: .bottom) {
            VStack(alignment: .leading, spacing: 8) {
                Text("Run in the title bar uses these options. Changes are saved to app storage as you edit.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                HStack {
                    Button("Reset all defaults") {
                        settings = PolymechImageSettings.defaults
                    }
                    Spacer()
                    Button("Save now") {
                        PolymechImageSettingsStore.save(settings)
                    }
                    .keyboardShortcut("s", modifiers: [.command])
                    .buttonStyle(.borderedProminent)
                }
            }
            .padding(8)
            .background(.bar)
        }
    }

    private var shouldShowImageInputSummary: Bool {
        switch workflow.command {
        case .some(.resize), .some(.meta), .some(.compress), .some(.transform):
            return true
        case .some(.find), .some(.chat), .none:
            return false
        }
    }

    private func refreshInspectorContextLabels() {
        selectionContextLabel = workflow.selectionDescription
        imageInputSummaryLabel = workflow.imageCommandInputsSummary
    }

    @ViewBuilder
    private var commandForms: some View {
        switch workflow.command {
        case .none:
            Section("Resize (reference)") {
                PolymechResizeFormView(json: $settings.resize)
            }
            Section("Meta") { PolymechMetaFormView(json: $settings.meta) }
            Section("Compress") { PolymechCompressFormView(json: $settings.compress) }
            Section("Find") { PolymechFindFormView(json: $settings.find) }
            Section("Transform") { PolymechTransformFormView(json: $settings.transform) }
        case .some(.resize):
            PolymechResizeFormView(json: $settings.resize)
        case .some(.meta):
            PolymechMetaFormView(json: $settings.meta)
        case .some(.compress):
            PolymechCompressFormView(json: $settings.compress)
        case .some(.find):
            PolymechFindFormView(json: $settings.find)
        case .some(.transform):
            PolymechTransformFormView(json: $settings.transform)
        case .some(.chat):
            chatInspectorSection
        }
    }

    @ViewBuilder
    private var advancedJSONEditors: some View {
        switch workflow.command {
        case .none:
            jsonField("Resize", $settings.resize)
            jsonField("Meta", $settings.meta)
            jsonField("Compress", $settings.compress)
            jsonField("Find", $settings.find)
            jsonField("Transform", $settings.transform)
        case .some(.resize):
            jsonField("Resize", $settings.resize)
        case .some(.meta):
            jsonField("Meta", $settings.meta)
        case .some(.compress):
            jsonField("Compress", $settings.compress)
        case .some(.find):
            jsonField("Find", $settings.find)
        case .some(.transform):
            jsonField("Transform", $settings.transform)
        case .some(.chat):
            EmptyView()
        }
    }

    @ViewBuilder
    private var chatInspectorSection: some View {
        Section {
            Button {
                workspace.openPolymechWebChatInInspector()
            } label: {
                Text(NSLocalizedString("Polymech.inspector.chat.openButton", comment: "Polymech inspector"))
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .frame(maxWidth: .infinity, alignment: .center)
            .disabled(!workspace.canOpenPolymechWebChatTab())
        } header: {
            Text(PolymechCommand.chat.displayName)
        }
    }

    private func jsonField(_ title: String, _ path: Binding<String>) -> some View {
        Section(title) {
            TextEditor(text: path)
                .font(.system(.body, design: .monospaced))
                .frame(minHeight: 100)
        }
    }
}
