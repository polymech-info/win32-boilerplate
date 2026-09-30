import Foundation

/// `defaultTextProviders` = LLM / chat (OpenAI-style) rows; `defaultImageProviders` = image
/// backends (transform, meta, etc.). Replicate image discovery uses the shared bridge; do not
/// mix text router ids with image `provider` ids in UI copy.
struct PolymechProviderRow: Equatable {
    var name: String
    var apiKey: String
    var baseURL: String
    var defaultModel: String
    var modelOptions: [String]
}

enum PolymechProviderSettingsStore {
    /// Canonical image / audio providers — mirrors Win32 media::settings::known_providers order.
    /// api_key + base_url are read from settings.json["providers"]; models are per-operation.
    private static let defaultImageProviders: [PolymechProviderRow] = [
        .init(name: "google", apiKey: "", baseURL: "https://generativelanguage.googleapis.com/v1beta", defaultModel: "gemini-3-pro-image-preview", modelOptions: ["gemini-3-pro-image-preview", "gemini-3.1-flash-image-preview", "gemini-2.0-flash-exp"]),
        .init(name: "openai", apiKey: "", baseURL: "https://api.openai.com/v1", defaultModel: "gpt-image-1", modelOptions: ["gpt-image-1", "dall-e-3", "gpt-4o"]),
        .init(name: "openrouter", apiKey: "", baseURL: "https://openrouter.ai/api/v1", defaultModel: "openai/gpt-4o", modelOptions: ["openai/gpt-4o", "google/gemini-2-flash"]),
        // Replicate: `defaultModel` is catalog/UI metadata only; runtime must not use it as a fallback.
        .init(
            name: "replicate",
            apiKey: "",
            baseURL: "https://api.replicate.com/v1",
            defaultModel: "black-forest-labs/flux-schnell",
            modelOptions: [
                "black-forest-labs/flux-schnell",
                "black-forest-labs/flux-1.1-pro",
                "stability-ai/sdxl",
                "google/imagen-3",
                "recraft-ai/recraft-v3",
            ],
        ),
        // defaultModel is empty: runtime must not read it as a fallback.
        .init(
            name: "pixlwiz",
            apiKey: "",
            baseURL: "https://llm.polymech.info",
            defaultModel: "",
            modelOptions: ["image-generation-deep", "image-generation-fast", "video-fast"]
        ),
        // ElevenLabs: audio (TTS/STT) — API key stored in providers["elevenlabs"] same as Win32.
        // No image model options; present here so imageProviders() returns the saved api_key.
        .init(
            name: "elevenlabs",
            apiKey: "",
            baseURL: "https://api.elevenlabs.io/v1",
            defaultModel: "",
            modelOptions: []
        ),
    ]

    private static let defaultTextProviders: [PolymechProviderRow] = [
        .init(name: "openrouter", apiKey: "", baseURL: "https://openrouter.ai/api/v1", defaultModel: "openai/gpt-4o-mini", modelOptions: ["openai/gpt-4o-mini"]),
        .init(name: "openai", apiKey: "", baseURL: "https://api.openai.com/v1", defaultModel: "gpt-4o-mini", modelOptions: ["gpt-4o-mini"]),
        .init(name: "gemini", apiKey: "", baseURL: "https://generativelanguage.googleapis.com/v1beta", defaultModel: "gemini-3-pro-image-preview", modelOptions: ["gemini-3-pro-image-preview"]),
        .init(name: "deepseek", apiKey: "", baseURL: "https://api.deepseek.com/v1", defaultModel: "deepseek-chat", modelOptions: ["deepseek-chat"]),
        .init(name: "ollama", apiKey: "", baseURL: "http://localhost:11434/v1", defaultModel: "llama3.2", modelOptions: ["llama3.2"]),
        .init(name: "fireworks", apiKey: "", baseURL: "https://api.fireworks.ai/v1", defaultModel: "accounts/fireworks/models/llama-v3p1-8b-instruct", modelOptions: ["accounts/fireworks/models/llama-v3p1-8b-instruct"]),
        .init(name: "xai", apiKey: "", baseURL: "https://api.x.ai/v1", defaultModel: "grok-2", modelOptions: ["grok-2"]),
        .init(name: "huggingface", apiKey: "", baseURL: "https://api-inference.huggingface.co/v1", defaultModel: "meta-llama/Llama-3.1-8B-Instruct", modelOptions: ["meta-llama/Llama-3.1-8B-Instruct"]),
        // defaultModel is empty: runtime must not read it as a fallback.
        .init(name: "pixlwiz", apiKey: "", baseURL: "https://llm.polymech.info", defaultModel: "", modelOptions: ["text-fast", "text-deep"]),
        .init(name: "custom", apiKey: "", baseURL: "", defaultModel: "", modelOptions: []),
    ]

    private static func settingsPath() -> URL {
        return FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/PolyMech/pm-image/settings.json")
    }

    /// Shared `settings.json` path (Win32 / CLI / `portable_settings` on native).
    static var sharedSettingsFileURL: URL { settingsPath() }

    static func readSettingsRoot() -> [String: Any] {
        loadRootObject()
    }

    static func writeSettingsRoot(_ root: [String: Any]) throws {
        let path = settingsPath()
        let fm = FileManager.default
        try fm.createDirectory(
            at: path.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        let data = try JSONSerialization.data(
            withJSONObject: root,
            options: [.prettyPrinted, .sortedKeys]
        )
        try data.write(to: path, options: .atomic)
    }

    private static func loadRootObject() -> [String: Any] {
        let path = settingsPath()
        guard let d = try? Data(contentsOf: path), !d.isEmpty else { return [:] }
        guard let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return [:] }
        return o
    }

    static func imageProviders() -> [PolymechProviderRow] {
        let root = loadRootObject()
        guard let providers = root["providers"] as? [String: Any], !providers.isEmpty else {
            return defaultImageProviders
        }
        var rows = defaultImageProviders
        var byName: [String: Int] = [:]
        for (i, p) in rows.enumerated() { byName[p.name] = i }
        for (name, raw) in providers {
            let d = raw as? [String: Any] ?? [:]
            let defaults = defaultImageProviders.first(where: { $0.name == name })
            let merged = PolymechProviderRow(
                name: name,
                apiKey: d["api_key"] as? String ?? "",
                baseURL: d["base_url"] as? String ?? defaults?.baseURL ?? "",
                defaultModel: d["default_model"] as? String ?? defaults?.defaultModel ?? "",
                modelOptions: defaults?.modelOptions ?? []
            )
            if let i = byName[name] {
                rows[i] = merged
            } else {
                rows.append(merged)
                byName[name] = rows.count - 1
            }
        }
        return rows.sorted { $0.name < $1.name }
    }

    // Returns the saved image provider for UI/catalog display only.
    // Prefers chat.image_provider (explicit); falls back to legacy active_provider key.
    // Returns nil rather than falling back to the first provider row — callers must not use
    // this return value as a runtime fallback; C++ resolves credentials from settings.
    static func activeImageProvider() -> PolymechProviderRow? {
        let root = loadRootObject()
        let providers = imageProviders()
        let active = ((root["chat"] as? [String: Any])?["image_provider"] as? String)
                  ?? (root["active_provider"] as? String)
        guard let active, !active.isEmpty else { return nil }
        return providers.first(where: { $0.name == active })
    }

    static func imageProvider(named name: String) -> PolymechProviderRow? {
        imageProviders().first(where: { $0.name == name })
    }

    static func textProviders() -> [PolymechProviderRow] {
        let root = loadRootObject()
        if let providers = root["polymech_text_providers"] as? [String: Any], !providers.isEmpty {
            let out = providers.map { name, raw -> PolymechProviderRow in
                let d = raw as? [String: Any] ?? [:]
                let defaults = defaultTextProviders.first(where: { $0.name == name })
                return .init(
                    name: name,
                    apiKey: d["api_key"] as? String ?? "",
                    baseURL: d["base_url"] as? String ?? defaults?.baseURL ?? "",
                    defaultModel: d["default_model"] as? String ?? defaults?.defaultModel ?? "",
                    modelOptions: defaults?.modelOptions ?? []
                )
            }
            return out.sorted { $0.name < $1.name }
        }
        return defaultTextProviders
    }

    // Returns the saved text provider for UI/catalog display only.
    // Reads polymech_active_text_provider (legacy) or chat.router (canonical).
    // Returns nil rather than falling back to the first provider row — runtime resolution
    // belongs to C++ (media::runtime_settings); do not use this as a chat turn source.
    static func activeTextProvider() -> PolymechProviderRow? {
        let root = loadRootObject()
        let providers = textProviders()
        let active = (root["polymech_active_text_provider"] as? String)
            ?? ((root["chat"] as? [String: Any])?["router"] as? String)
        guard let active, !active.isEmpty else { return nil }
        return providers.first(where: { $0.name == active })
    }
}
