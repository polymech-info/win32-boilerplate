import AppKit
import SwiftUI

/// Payload for ``WorkspaceDocument.pixlwizSharePostSheet`` (SwiftUI `.sheet`).
struct PixlwizSharePostSheetContext: Identifiable, Equatable {
    let id = UUID()
    let imageURLs: [URL]

    static func == (lhs: PixlwizSharePostSheetContext, rhs: PixlwizSharePostSheetContext) -> Bool {
        lhs.id == rhs.id
    }
}

// MARK: - Sheet root (form → upload → success)

struct PixlwizSharePostSheetRoot: View {
    @EnvironmentObject private var workspace: WorkspaceDocument
    let context: PixlwizSharePostSheetContext

    @State private var titleText: String = ""
    @State private var descriptionText: String = ""
    /// Matches Win32 `IDC_PIXLWIZ_SHARE_PRIVATE`: when true → `private`.
    @State private var isPrivate: Bool = false
    /// When not private: checked → `public`, unchecked → `listed` (same as `IDC_PIXLWIZ_SHARE_IN_FEEDS`).
    @State private var showInPublicFeeds: Bool = true

    private enum Phase {
        case form
        case uploading
        case success(PolymechServiceCreatePostResult)

        var isUploading: Bool {
            if case .uploading = self { return true }
            return false
        }
    }

    @State private var phase: Phase = .form
    @State private var uploadError: String?

    var body: some View {
        NavigationStack {
            Group {
                switch phase {
                case .form:
                    formContent
                case .uploading:
                    VStack(spacing: 16) {
                        ProgressView()
                        Text(LocalizedStringKey("Polymech.share.uploading"))
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                case .success(let result):
                    successContent(result)
                }
            }
            .navigationTitle(Text(LocalizedStringKey("Polymech.share.sheetTitle")))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(LocalizedStringKey("Polymech.share.cancel")) {
                        closeSheet()
                    }
                    .disabled(phase.isUploading)
                }
            }
        }
        .frame(minWidth: 420, minHeight: 360)
        .onAppear {
            if titleText.isEmpty, let first = context.imageURLs.first {
                titleText = first.deletingPathExtension().lastPathComponent
            }
        }
        .alert("Error", isPresented: Binding(
            get: { uploadError != nil },
            set: { if !$0 { uploadError = nil } }
        )) {
            Button("OK", role: .cancel) { uploadError = nil }
        } message: {
            Text(uploadError ?? "")
        }
    }

    @ViewBuilder
    private var formContent: some View {
        Form {
            Section {
                Text(String.localizedStringWithFormat(
                    NSLocalizedString("Polymech.share.imageCount", comment: "Share sheet"),
                    context.imageURLs.count
                ))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Section(LocalizedStringKey("Polymech.share.titleField")) {
                TextField(LocalizedStringKey("Polymech.share.titlePlaceholder"), text: $titleText)
            }
            Section(LocalizedStringKey("Polymech.share.descriptionField")) {
                TextField(LocalizedStringKey("Polymech.share.descriptionPlaceholder"), text: $descriptionText, axis: .vertical)
                    .lineLimit(3 ... 8)
            }
            Section(LocalizedStringKey("Polymech.share.visibilitySection")) {
                Toggle(LocalizedStringKey("Polymech.share.private"), isOn: $isPrivate)
                    .onChange(of: isPrivate) { _, priv in
                        if priv { showInPublicFeeds = false }
                    }
                Toggle(LocalizedStringKey("Polymech.share.inPublicFeeds"), isOn: $showInPublicFeeds)
                    .disabled(isPrivate)
            }
            Section {
                Button(LocalizedStringKey("Polymech.share.share")) {
                    startUpload()
                }
                .keyboardShortcut(.defaultAction)
            }
        }
    }

    @ViewBuilder
    private func successContent(_ result: PolymechServiceCreatePostResult) -> some View {
        Form {
            Section {
                Text(LocalizedStringKey("Polymech.share.successMessage"))
                    .foregroundStyle(.secondary)
            }
            Section {
                LabeledContent(LocalizedStringKey("Polymech.share.postIdLabel"), value: result.postId)
                LabeledContent(LocalizedStringKey("Polymech.share.picturesLabel")) {
                    Text(String.localizedStringWithFormat(
                        NSLocalizedString("Polymech.share.picturesCount", comment: "Share success"),
                        result.pictureIds.count
                    ))
                }
            }
            Section {
                Button(LocalizedStringKey("Polymech.share.openInBrowser")) {
                    if let u = URL(string: result.viewURL) {
                        NSWorkspace.shared.open(u)
                    }
                }
                Button(LocalizedStringKey("Polymech.share.done")) {
                    closeSheet()
                }
                .keyboardShortcut(.defaultAction)
            }
        }
    }

    private func visibilityString() -> String {
        if isPrivate { return "private" }
        return showInPublicFeeds ? "public" : "listed"
    }

    private func startUpload() {
        uploadError = nil
        phase = .uploading
        let paths = context.imageURLs.map(\.path)
        let title = titleText.trimmingCharacters(in: .whitespacesAndNewlines)
        let desc = descriptionText.trimmingCharacters(in: .whitespacesAndNewlines)
        let vis = visibilityString()
        Task {
            do {
                let result = try await Task.detached(priority: .userInitiated) {
                    try PolymechImageEngine.runServiceCreatePost(
                        imagePaths: paths,
                        title: title.isEmpty ? nil : title,
                        description: desc.isEmpty ? nil : desc,
                        visibility: vis,
                        serverURL: nil
                    )
                }.value
                await MainActor.run {
                    phase = .success(result)
                }
            } catch {
                await MainActor.run {
                    phase = .form
                    uploadError = error.localizedDescription
                }
            }
        }
    }

    private func closeSheet() {
        workspace.pixlwizSharePostSheet = nil
    }
}
