import SwiftUI

struct AgentInspectorView: View {
    @Environment(\.settings) private var settings
    @EnvironmentObject private var workspace: WorkspaceDocument
    @EnvironmentObject private var workflow: PolymechWorkflowState

    private var chatURL: URL? {
        if let b = PolymechWebChat.bundledChatHTMLURL() { return b }
        if let w = workspace.fileURL, let r = PolymechWebChat.resolvedChatHTMLFromWorkspace(w) { return r }
        return nil
    }

    var body: some View {
        Group {
            if let chatURL, let editorManager = workspace.editorManager {
                VStack(alignment: .leading, spacing: 0) {
                    HStack {
                        Spacer(minLength: 0)
                        Button {
                            workspace.openPolymechWebChatTab()
                        } label: {
                            Text(NSLocalizedString("Polymech.chat.detachToEditor", comment: "Agent inspector"))
                        }
                        .controlSize(.small)
                        .help(NSLocalizedString("Polymech.menu.detachChatToEditor.help", comment: "Agent inspector"))
                    }
                    .padding(.horizontal, 8)
                    .padding(.top, 6)
                    PolymechWebChatWKView(
                        fileURL: chatURL,
                        readAccessURL: PolymechWebChat.readAccessDirectory(for: chatURL),
                        workflow: workflow,
                        editorManager: editorManager,
                        appLanguage: settings.general.appLanguage
                    )
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            } else {
                VStack(alignment: .leading, spacing: 10) {
                    Text(NSLocalizedString("Polymech.agentChat.unavailable.title", comment: "Agent inspector"))
                        .font(.headline)
                    Text(NSLocalizedString("Polymech.agentChat.unavailable.hint", comment: "Agent inspector"))
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                .padding(10)
            }
        }
    }
}
