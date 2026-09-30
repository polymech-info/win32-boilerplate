import SwiftUI

/// Edits the `chat` object in the shared `settings.json` (Win32 `ChatProviderDlg` + `ChatPanel` /
/// `path_tool_executor` `image_provider` / `image_model`). Text fields drive the LLM agent; image
/// fields default image tools when the model omits `provider` / `model` in options.
struct PolymechChatSettingsView: View {
    @State private var router: String = ""
    @State private var model: String = ""
    @State private var apiKey: String = ""
    @State private var baseURL: String = ""
    @State private var timeoutMs: Int = 60_000
    @State private var maxIterations: Int = 8
    @State private var imageProvider: String = ""
    @State private var imageModel: String = ""
    @State private var imageRecognitionProvider: String = ""
    @State private var imageRecognitionModel: String = ""
    @State private var videoProvider: String = ""
    @State private var videoModel: String = ""
    @State private var sttProvider: String = ""
    @State private var sttModel: String = ""
    @State private var ttsProvider: String = ""
    @State private var ttsModel: String = ""
    @State private var ttsVoiceId: String = ""
    @State private var statusMessage: String = ""

    // Mirrors Win32 AudioSelectorController provider/model tables.
    private static let sttProviders: [(id: String, label: String)] = [
        ("pixlwiz",    "PixlWiz (proxy)"),
        ("elevenlabs", "ElevenLabs (direct)"),
    ]
    private static let ttsProviders: [(id: String, label: String)] = [
        ("pixlwiz",    "PixlWiz (proxy)"),
        ("elevenlabs", "ElevenLabs (direct)"),
    ]
    private static let sttModels: [String: [(id: String, label: String)]] = [
        "pixlwiz":    [("pixlwiz-speech-to-text", "PixlWiz STT (Whisper-1)")],
        "elevenlabs": [("scribe_v2_realtime",      "Scribe v2 (real-time)")],
    ]
    private static let ttsModels: [String: [(id: String, label: String)]] = [
        "pixlwiz":    [
            ("pixlwiz-speech",       "PixlWiz TTS (Multilingual v2)"),
            ("pixlwiz-speech-turbo", "PixlWiz TTS Turbo (v2.5)"),
        ],
        "elevenlabs": [
            ("eleven_v3",              "ElevenLabs v3"),
            ("eleven_multilingual_v2", "Multilingual v2"),
            ("eleven_turbo_v2_5",      "Turbo v2.5 (low latency)"),
            ("eleven_flash_v2_5",      "Flash v2.5 (ultra-fast)"),
        ],
    ]
    private static let elevenLabsKnownVoices: [(id: String, label: String)] = [
        ("tLK6fPv15M0oKv4V3ACR", "Sarah — Mature, Reassuring, Confident"),
        ("Xb7hH8MSUJpSbSDYk0k2", "Alice — Clear, Engaging Educator"),
        ("XrExE9yKIg1WjnnlVkGX", "Matilda — Knowledgeable, Professional"),
        ("onwK4e9ZLuTAKqWW03F9", "Daniel — Steady Broadcaster"),
        ("nPczCjzI2devNBz1zQrb", "Brian — Deep, Resonant and Comforting"),
        ("pNInz6obpgDQGcFmaJgB", "Adam — Dominant, Firm"),
        ("JBFqnCBsd6RMkjVDRZzb", "George — Warm, Captivating Storyteller"),
        ("cgSgspJ2msm6clMCkdW9", "Jessica — Playful, Bright, Warm"),
        ("SAz9YHcvj6GT2YYXdXww", "River — Relaxed, Neutral, Informative"),
        ("TX3LPaxmHKxFdv7VOQHJ", "Liam — Energetic, Social Media Creator"),
    ]

    var body: some View {
        SettingsForm {
            Section {
                Text("Shared file: `\(PolymechProviderSettingsStore.sharedSettingsFileURL.path)`")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                Button("Reload from disk") { loadFromDisk() }
            } header: {
                Text("Storage")
            }

            Section {
                Picker("Text router (LLM)", selection: $router) {
                    ForEach(PolymechProviderSettingsStore.textProviders().map(\.name), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                SecureField("API key", text: $apiKey)
                TextField("Base URL (optional; blank = router default)", text: $baseURL)
                TextField("Model id", text: $model)
                if router == "openrouter" {
                    PolymechOpenRouterModelBrowser(
                        apiKey: $apiKey,
                        baseURL: $baseURL,
                        modelId: $model,
                        modelOptions: nil
                    )
                }
                HStack {
                    Text("Timeout (ms)")
                    Spacer()
                    TextField("", value: $timeoutMs, format: .number)
                        .frame(width: 100)
                }
                HStack {
                    Text("Max tool iterations")
                    Spacer()
                    TextField("", value: $maxIterations, format: .number)
                        .frame(width: 60)
                }
            } header: {
                Text("Text chat (llm::agent)")
            } footer: {
                Text("Maps to `settings.json` → `chat` (router, model, api_key, base_url, timeout_ms, max_iterations). Same waterfall as Windows `make_provider_for_turn` / `pm_polymech_chat_turn` + `fill_chat_provider_defaults`.")
                    .font(.caption)
            }

            Section {
                Picker("Image provider (tool defaults)", selection: $imageProvider) {
                    ForEach(PolymechProviderSettingsStore.imageProviders().map(\.name), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                TextField("Image model id", text: $imageModel)
                if imageProvider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: chatReplicateApiKeyBinding,
                        baseURL: chatReplicateBaseURLBinding,
                        modelSlug: $imageModel,
                        modelOptions: nil,
                        command: nil
                    )
                }
            } header: {
                Text("Image tools (agent)")
            } footer: {
                Text("Stored as `chat.image_provider` and `chat.image_model`. Used when an image tool call does not set `options.provider` / `options.model` (see `path_tool_executor` / Windows Chat Provider dialog).")
                    .font(.caption)
            }

            Section {
                Picker("Vision / image recognition (defaults)", selection: $imageRecognitionProvider) {
                    Text("(unset)").tag("")
                    ForEach(PolymechProviderSettingsStore.imageProviders().map(\.name), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                TextField("Recognition model id", text: $imageRecognitionModel)
                if imageRecognitionProvider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: chatReplicateApiKeyBinding,
                        baseURL: chatReplicateBaseURLBinding,
                        modelSlug: $imageRecognitionModel,
                        modelOptions: nil,
                        command: nil
                    )
                }
            } header: {
                Text("Image recognition (vision)")
            } footer: {
                Text("`chat.image_recognition_provider` / `chat.image_recognition_model` — same keys as Windows `ChatProviderDlg` / `settings_store.hpp` ChatProviderSettings (2b).")
                    .font(.caption)
            }

            Section {
                Picker("Video generation (defaults)", selection: $videoProvider) {
                    Text("(unset)").tag("")
                    ForEach(PolymechProviderSettingsStore.imageProviders().map(\.name), id: \.self) { name in
                        Text(name).tag(name)
                    }
                }
                TextField("Video model id (e.g. Replicate slug)", text: $videoModel)
                if videoProvider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: chatReplicateApiKeyBinding,
                        baseURL: chatReplicateBaseURLBinding,
                        modelSlug: $videoModel,
                        modelOptions: nil,
                        command: nil
                    )
                }
            } header: {
                Text("Video (`create_video`)")
            } footer: {
                Text("`chat.video_provider` / `chat.video_model` — defaults for the `create_video` tool (Win32 ChatProviderDlg section; `settings_portable.cpp`).")
                    .font(.caption)
            }

            // ── Voice & Audio (STT / TTS) — Win32 ChatProviderDlg group_audio ──
            Section {
                Picker("STT provider", selection: $sttProvider) {
                    Text("(unset)").tag("")
                    ForEach(Self.sttProviders, id: \.id) { p in Text(p.label).tag(p.id) }
                }
                if let models = Self.sttModels[sttProvider], !models.isEmpty {
                    Picker("STT model", selection: $sttModel) {
                        ForEach(models, id: \.id) { m in Text(m.label).tag(m.id) }
                    }
                } else {
                    TextField("STT model id", text: $sttModel)
                }
                Picker("TTS provider", selection: $ttsProvider) {
                    Text("(unset)").tag("")
                    ForEach(Self.ttsProviders, id: \.id) { p in Text(p.label).tag(p.id) }
                }
                if let models = Self.ttsModels[ttsProvider], !models.isEmpty {
                    Picker("TTS model", selection: $ttsModel) {
                        ForEach(models, id: \.id) { m in Text(m.label).tag(m.id) }
                    }
                } else {
                    TextField("TTS model id", text: $ttsModel)
                }
                if ttsProvider == "elevenlabs" {
                    Picker("TTS voice (ElevenLabs)", selection: $ttsVoiceId) {
                        Text("(type custom below)").tag("")
                        ForEach(Self.elevenLabsKnownVoices, id: \.id) { v in
                            Text(v.label).tag(v.id)
                        }
                    }
                    if ttsVoiceId.isEmpty || !Self.elevenLabsKnownVoices.contains(where: { $0.id == ttsVoiceId }) {
                        TextField("Voice ID (ElevenLabs UUID)", text: $ttsVoiceId)
                    }
                }
            } header: {
                Text("Voice & Audio (STT / TTS)")
            } footer: {
                Text("`chat.stt_provider` / `chat.stt_model` / `chat.tts_provider` / `chat.tts_model` / `chat.tts_voice_id` — same keys as Win32 `ChatProviderDlg` group_audio (AudioSelectorController + ElevenLabsSelectorController).")
                    .font(.caption)
            }

            Section {
                Button("Save to disk") { saveToDisk() }
            }

            if !statusMessage.isEmpty {
                Section {
                    Text(statusMessage)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(.horizontal, 10)
        .padding(.top, 6)
        .padding(.bottom, 10)
        .onAppear { loadFromDisk() }
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
        .onChange(of: imageRecognitionProvider) { _, p in
            guard !p.isEmpty else { return }
            if let row = PolymechProviderSettingsStore.imageProviders().first(where: { $0.name == p }) {
                if imageRecognitionModel.isEmpty { imageRecognitionModel = row.defaultModel }
            }
        }
        .onChange(of: videoProvider) { _, p in
            guard !p.isEmpty else { return }
            if let row = PolymechProviderSettingsStore.imageProviders().first(where: { $0.name == p }) {
                if videoModel.isEmpty { videoModel = row.defaultModel }
            }
        }
        .onChange(of: sttProvider) { _, p in
            guard !p.isEmpty else { return }
            if let first = Self.sttModels[p]?.first {
                if sttModel.isEmpty || Self.sttModels.values.flatMap({ $0 }).allSatisfy({ $0.id != sttModel }) {
                    sttModel = first.id
                }
            }
        }
        .onChange(of: ttsProvider) { _, p in
            guard !p.isEmpty else { return }
            if let first = Self.ttsModels[p]?.first {
                if ttsModel.isEmpty || Self.ttsModels.values.flatMap({ $0 }).allSatisfy({ $0.id != ttsModel }) {
                    ttsModel = first.id
                }
            }
            if p != "elevenlabs" { ttsVoiceId = "" }
        }
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

    private func loadFromDisk() {
        let root = PolymechProviderSettingsStore.readSettingsRoot()
        let c = (root["chat"] as? [String: Any]) ?? [:]
        // Show saved explicit values only; do not inject active/default provider fallbacks.
        router = c["router"] as? String ?? ""
        model = c["model"] as? String ?? ""
        apiKey = c["api_key"] as? String ?? ""
        baseURL = c["base_url"] as? String ?? ""
        timeoutMs = intFromChat(c, "timeout_ms", 60_000)
        maxIterations = intFromChat(c, "max_iterations", 8)
        if maxIterations < 1 { maxIterations = 8 }
        imageProvider = c["image_provider"] as? String ?? ""
        imageModel = c["image_model"] as? String ?? ""
        imageRecognitionProvider = c["image_recognition_provider"] as? String ?? ""
        imageRecognitionModel = c["image_recognition_model"] as? String ?? ""
        videoProvider = c["video_provider"] as? String ?? ""
        videoModel = c["video_model"] as? String ?? ""
        sttProvider = c["stt_provider"] as? String ?? ""
        sttModel = c["stt_model"] as? String ?? ""
        ttsProvider = c["tts_provider"] as? String ?? ""
        ttsModel = c["tts_model"] as? String ?? ""
        ttsVoiceId = c["tts_voice_id"] as? String ?? ""
        statusMessage = "Reloaded `chat` from disk."
    }

    private func saveToDisk() {
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
        if imageRecognitionProvider.isEmpty {
            chat.removeValue(forKey: "image_recognition_provider")
            chat.removeValue(forKey: "image_recognition_model")
        } else {
            chat["image_recognition_provider"] = imageRecognitionProvider
            chat["image_recognition_model"] = imageRecognitionModel
        }
        if videoProvider.isEmpty {
            chat.removeValue(forKey: "video_provider")
            chat.removeValue(forKey: "video_model")
        } else {
            chat["video_provider"] = videoProvider
            chat["video_model"] = videoModel
        }
        if sttProvider.isEmpty {
            chat.removeValue(forKey: "stt_provider")
            chat.removeValue(forKey: "stt_model")
        } else {
            chat["stt_provider"] = sttProvider
            chat["stt_model"] = sttModel
        }
        if ttsProvider.isEmpty {
            chat.removeValue(forKey: "tts_provider")
            chat.removeValue(forKey: "tts_model")
            chat.removeValue(forKey: "tts_voice_id")
        } else {
            chat["tts_provider"] = ttsProvider
            chat["tts_model"] = ttsModel
            if ttsProvider == "elevenlabs", !ttsVoiceId.isEmpty {
                chat["tts_voice_id"] = ttsVoiceId
            } else {
                chat.removeValue(forKey: "tts_voice_id")
            }
        }
        root["chat"] = chat
        do {
            try PolymechProviderSettingsStore.writeSettingsRoot(root)
            statusMessage = "Saved `chat` to `settings.json`."
        } catch {
            statusMessage = "Save failed: \(error.localizedDescription)"
        }
    }
}
