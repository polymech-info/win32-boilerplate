import AppKit
import SwiftUI

private enum PresetRunTab: String, CaseIterable {
    case agent
    case transform
}

/// Dialog: style preset + **Agent** (PolyMech Chat settings + native `runChatTurn`) or **Transform** (image transform options + `runTransform` in the job queue, one file after another).
struct PolymechTransformStylePresetRunView: View {
    let preset: PolymechStylePreset
    let imageInputCount: Int
    let onCancel: () -> Void
    let onRunAgent: (String) -> Void
    let onRunTransform: (String) -> Void

    @State private var selectedTab: PresetRunTab = .agent

    @State private var prompt: String = ""
    @State private var router: String = ""
    @State private var model: String = ""
    @State private var apiKey: String = ""
    @State private var baseURL: String = ""
    @State private var timeoutMs: Int = 60_000
    @State private var maxIterations: Int = 8
    @State private var imageProvider: String = ""
    @State private var imageModel: String = ""
    @State private var transformJson: String = PolymechImageSettings.defaults.transform
    @State private var errorMessage: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(
                String(
                    format: NSLocalizedString("Polymech.navigator.presetFor", comment: "Transform preset"),
                    preset.icon.isEmpty ? preset.name : "\(preset.icon) \(preset.name)"
                )
            )
            .font(.headline)
            .padding(.bottom, 4)
            Text(NSLocalizedString("Polymech.navigator.presetHint", comment: "Transform preset"))
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.bottom, 8)
            Picker("", selection: $selectedTab) {
                Text(NSLocalizedString("Polymech.presetRun.tabAgent", comment: "Preset run tab"))
                    .tag(PresetRunTab.agent)
                Text(NSLocalizedString("Polymech.presetRun.tabTransform", comment: "Preset run tab"))
                    .tag(PresetRunTab.transform)
            }
            .labelsHidden()
            .pickerStyle(.segmented)
            .padding(.bottom, 8)
            if selectedTab == .transform {
                Text(NSLocalizedString("Polymech.presetRun.transformHint", comment: "Preset run transform tab"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.bottom, 6)
            }
            if !errorMessage.isEmpty {
                Text(errorMessage)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .padding(.bottom, 6)
            }
            ScrollView {
                Group {
                    switch selectedTab {
                    case .agent:
                        agentForm
                    case .transform:
                        Form { PolymechTransformFormView(json: $transformJson) }
                            .formStyle(.grouped)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack {
                Spacer()
                Button(NSLocalizedString("Polymech.navigator.chatRun.cancel", comment: "")) { onCancel() }
                    .keyboardShortcut(.cancelAction)
                Button(NSLocalizedString("Polymech.transform.run", comment: "Transform preset")) { runTapped() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(isRunDisabled)
            }
            .padding(.top, 10)
        }
        .padding(16)
        .frame(minWidth: 520, minHeight: 520)
        .onAppear {
            loadAgentFromSettings()
            loadTransformFromStore()
        }
        .onChange(of: router) { _, r in
            if let row = PolymechProviderSettingsStore.textProviders().first(where: { $0.name == r }) {
                if model.isEmpty { model = row.defaultModel }
                if baseURL.isEmpty { baseURL = row.baseURL }
            }
        }
        .onChange(of: imageProvider) { _, p in
            if let row = PolymechProviderSettingsStore.imageProviders().first(where: { $0.name == p }) {
                if imageModel.isEmpty { imageModel = row.defaultModel }
            }
        }
    }

    private var isRunDisabled: Bool {
        switch selectedTab {
        case .agent:
            return prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        case .transform:
            return imageInputCount == 0
        }
    }

    @ViewBuilder
    private var agentForm: some View {
        VStack(alignment: .leading, spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
                Text(NSLocalizedString("Polymech.navigator.chatRun.prompt", comment: ""))
                    .font(.subheadline.weight(.semibold))
                TextEditor(text: $prompt)
                    .font(.body)
                    .frame(minHeight: 100, maxHeight: 180)
                    .overlay {
                        RoundedRectangle(cornerRadius: 4, style: .continuous)
                            .strokeBorder(Color(nsColor: .separatorColor), lineWidth: 1)
                    }
            }
            Group {
                Text(NSLocalizedString("Polymech.transform.textProviderSection", comment: "Transform preset"))
                    .font(.subheadline.weight(.semibold))
                Picker(
                    NSLocalizedString("Polymech.navigator.chatRun.provider", comment: ""),
                    selection: $router
                ) {
                    ForEach(PolymechProviderSettingsStore.textProviders().map(\.name), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                SecureField(
                    NSLocalizedString("Polymech.transform.apiKey", comment: "Transform preset"),
                    text: $apiKey
                )
                .textFieldStyle(.roundedBorder)
                TextField(
                    NSLocalizedString("Polymech.transform.baseURL", comment: "Transform preset"),
                    text: $baseURL
                )
                .textFieldStyle(.roundedBorder)
                if router != "openrouter", !textModelOptions.isEmpty {
                    Picker(
                        NSLocalizedString("Polymech.navigator.chatRun.modelPreset", comment: ""),
                        selection: $model
                    ) {
                        ForEach(textModelOptions, id: \.self) { m in
                            Text(m).tag(m)
                        }
                    }
                }
                TextField(
                    NSLocalizedString("Polymech.navigator.chatRun.modelId", comment: ""),
                    text: $model
                )
                .textFieldStyle(.roundedBorder)
                if router == "openrouter" {
                    PolymechOpenRouterModelBrowser(
                        apiKey: $apiKey,
                        baseURL: $baseURL,
                        modelId: $model,
                        modelOptions: nil
                    )
                }
                HStack {
                    Text(NSLocalizedString("Polymech.transform.timeout", comment: "Transform preset"))
                    Spacer()
                    TextField("", value: $timeoutMs, format: .number)
                        .frame(width: 100)
                }
                HStack {
                    Text(NSLocalizedString("Polymech.transform.maxIter", comment: "Transform preset"))
                    Spacer()
                    TextField("", value: $maxIterations, format: .number)
                        .frame(width: 60)
                }
            }
            Group {
                Text(NSLocalizedString("Polymech.transform.imageProviderSection", comment: "Transform preset"))
                    .font(.subheadline.weight(.semibold))
                Picker(
                    NSLocalizedString("Polymech.transform.imageProvider", comment: "Transform preset"),
                    selection: $imageProvider
                ) {
                    ForEach(PolymechProviderSettingsStore.imageProviders().map(\.name), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                if !imageModelOptions.isEmpty {
                    Picker(
                        NSLocalizedString("Polymech.transform.imageModelPreset", comment: "Transform preset"),
                        selection: $imageModel
                    ) {
                        ForEach(imageModelOptions, id: \.self) { m in
                            Text(m).tag(m)
                        }
                    }
                }
                TextField(
                    NSLocalizedString("Polymech.transform.imageModelId", comment: "Transform preset"),
                    text: $imageModel
                )
                .textFieldStyle(.roundedBorder)
                if imageProvider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: chatReplicateApiKeyBinding,
                        baseURL: chatReplicateBaseURLBinding,
                        modelSlug: $imageModel,
                        modelOptions: nil,
                        command: nil
                    )
                }
            }
        }
    }

    private var textModelOptions: [String] {
        PolymechProviderSettingsStore.textProviders().first { $0.name == router }?.modelOptions ?? []
    }

    private var imageModelOptions: [String] {
        PolymechProviderSettingsStore.imageProviders().first { $0.name == imageProvider }?.modelOptions ?? []
    }

    private var chatReplicateApiKeyBinding: Binding<String> {
        Binding(
            get: { PolymechProviderSettingsStore.imageProvider(named: "replicate")?.apiKey ?? "" },
            set: { _ in }
        )
    }

    private var chatReplicateBaseURLBinding: Binding<String> {
        Binding(
            get: {
                PolymechProviderSettingsStore.imageProvider(named: "replicate")?.baseURL
                    ?? "https://api.replicate.com/v1"
            },
            set: { _ in }
        )
    }

    private func intFromChat(_ c: [String: Any], _ k: String, _ def: Int) -> Int {
        if let n = c[k] as? Int { return n }
        if let n = c[k] as? NSNumber { return n.intValue }
        return def
    }

    private func loadAgentFromSettings() {
        let root = PolymechProviderSettingsStore.readSettingsRoot()
        let c = (root["chat"] as? [String: Any]) ?? [:]
        prompt = preset.prompt
        router = c["router"] as? String ?? ""
        model = c["model"] as? String ?? ""
        apiKey = c["api_key"] as? String ?? ""
        baseURL = c["base_url"] as? String ?? ""
        timeoutMs = intFromChat(c, "timeout_ms", 60_000)
        maxIterations = intFromChat(c, "max_iterations", 8)
        if maxIterations < 1 { maxIterations = 8 }
        imageProvider = c["image_provider"] as? String ?? ""
        imageModel = c["image_model"] as? String ?? ""
    }

    private func loadTransformFromStore() {
        var t = PolymechTransformFormData.from(json: PolymechImageSettingsStore.load().transform)
        t.prompt = preset.prompt
        transformJson = t.toJSON()
    }

    private func runTapped() {
        errorMessage = ""
        switch selectedTab {
        case .agent:
            runAgentTapped()
        case .transform:
            runTransformTapped()
        }
    }

    private func runAgentTapped() {
        let p = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !p.isEmpty else { return }
        var root = PolymechProviderSettingsStore.readSettingsRoot()
        var chat: [String: Any] = (root["chat"] as? [String: Any]) ?? [:]
        chat["router"] = router
        chat["model"] = model
        chat["api_key"] = apiKey
        chat["base_url"] = baseURL
        chat["timeout_ms"] = timeoutMs
        chat["max_iterations"] = maxIterations
        chat["image_provider"] = imageProvider
        chat["image_model"] = imageModel
        root["chat"] = chat
        do {
            try PolymechProviderSettingsStore.writeSettingsRoot(root)
            errorMessage = ""
            onRunAgent(p)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func runTransformTapped() {
        guard imageInputCount > 0 else {
            errorMessage = NSLocalizedString("Polymech.presetRun.noImageSelection", comment: "Preset run")
            return
        }
        var t = PolymechTransformFormData.from(json: transformJson)
        if t.prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            t.prompt = preset.prompt
        }
        onRunTransform(t.toJSON())
    }
}
