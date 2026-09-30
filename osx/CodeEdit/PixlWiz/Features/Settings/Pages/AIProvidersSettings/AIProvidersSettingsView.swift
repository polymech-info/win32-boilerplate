import AppKit
import Foundation
import SwiftUI

// MARK: - Canonical provider definitions
// Mirrors Win32 media::settings::known_providers (settings_store.cpp) in the same order.
// Each entry stores api_key + base_url under settings.json["providers"][id].

private struct PMCanonicalDef: Identifiable {
    let id: String           // JSON key
    let displayName: String
    let defaultBaseURL: String
}

private let pmCanonicalProviders: [PMCanonicalDef] = [
    .init(id: "google",     displayName: "Google",     defaultBaseURL: "https://generativelanguage.googleapis.com/v1beta"),
    .init(id: "openai",     displayName: "OpenAI",     defaultBaseURL: "https://api.openai.com/v1"),
    .init(id: "replicate",  displayName: "Replicate",  defaultBaseURL: "https://api.replicate.com/v1"),
    .init(id: "openrouter", displayName: "OpenRouter", defaultBaseURL: "https://openrouter.ai/api/v1"),
    .init(id: "pixlwiz",    displayName: "PixlWiz",    defaultBaseURL: "https://llm.polymech.info"),
    .init(id: "elevenlabs", displayName: "ElevenLabs", defaultBaseURL: "https://api.elevenlabs.io/v1"),
]

// MARK: - Row models

private struct PMCanonicalRow: Identifiable {
    var id: String { name }
    let name: String
    let displayName: String
    let defaultBaseURL: String
    var apiKey: String  = ""
    var baseURL: String = ""
    var showKey: Bool   = false
}

private struct PMTextProviderRow: Identifiable {
    var id = UUID()
    var name: String
    var apiKey: String
    var baseURL: String
}

// MARK: - Default text-only providers (macOS-specific chat routing; not in Win32 ProviderDlg)

private let pmDefaultTextProviders: [PMTextProviderRow] = [
    .init(name: "openrouter",  apiKey: "", baseURL: "https://openrouter.ai/api/v1"),
    .init(name: "openai",      apiKey: "", baseURL: "https://api.openai.com/v1"),
    .init(name: "gemini",      apiKey: "", baseURL: "https://generativelanguage.googleapis.com/v1beta"),
    .init(name: "deepseek",    apiKey: "", baseURL: "https://api.deepseek.com/v1"),
    .init(name: "ollama",      apiKey: "", baseURL: "http://localhost:11434/v1"),
    .init(name: "fireworks",   apiKey: "", baseURL: "https://api.fireworks.ai/v1"),
    .init(name: "xai",         apiKey: "", baseURL: "https://api.x.ai/v1"),
    .init(name: "huggingface", apiKey: "", baseURL: "https://api-inference.huggingface.co/v1"),
    .init(name: "pixlwiz",     apiKey: "", baseURL: "https://llm.polymech.info"),
    .init(name: "custom",      apiKey: "", baseURL: ""),
]

// MARK: - Settings I/O

private enum PMSettingsPath {
    static func resolved() -> URL {
        return FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/PolyMech/pm-image/settings.json")
    }
}

private enum PMSettingsStore {
    static func readJSON(path: URL) -> [String: Any] {
        guard let d = try? Data(contentsOf: path), !d.isEmpty else { return [:] }
        guard let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return [:] }
        return o
    }

    static func writeJSON(path: URL, root: [String: Any]) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: path.deletingLastPathComponent(), withIntermediateDirectories: true)
        let d = try JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys])
        try d.write(to: path, options: .atomic)
    }
}

// MARK: - View

struct AIProvidersSettingsView: View {
    @State private var settingsPath: URL = PMSettingsPath.resolved()

    /// Fixed canonical rows — one per Win32 known_provider, order preserved.
    @State private var canonicalRows: [PMCanonicalRow] = pmCanonicalProviders.map {
        PMCanonicalRow(name: $0.id, displayName: $0.displayName,
                       defaultBaseURL: $0.defaultBaseURL, baseURL: $0.defaultBaseURL)
    }

    /// Dynamic text-only providers (macOS chat routing; stored in polymech_text_providers).
    @State private var textProviders: [PMTextProviderRow] = pmDefaultTextProviders

    @State private var statusMessage: String = ""

    var body: some View {
        SettingsForm {

            // ── Storage ────────────────────────────────────────────────────────
            Section {
                TextField(
                    NSLocalizedString("Shared settings path", comment: "AI providers"),
                    text: Binding(
                        get: { settingsPath.path },
                        set: { settingsPath = URL(fileURLWithPath: $0) }
                    )
                )
                .textFieldStyle(.roundedBorder)
                HStack {
                    Button(NSLocalizedString("Reload from disk", comment: "AI providers")) { loadFromDisk() }
                    Button(NSLocalizedString("Save to disk",     comment: "AI providers")) { saveToDisk() }
                }
            } header: {
                Text(NSLocalizedString("Storage", comment: "AI providers"))
            } footer: {
                Text(NSLocalizedString("AI providers footer storage", comment: "AI providers"))
            }

            // ── Canonical providers (par with Win32 ProviderDlg: key + base URL only) ──
            Section {
                ForEach(Array(canonicalRows.indices), id: \.self) { idx in
                    canonicalRow(index: idx)
                }
            } header: {
                Text(NSLocalizedString("AI Providers", comment: "AI providers"))
            } footer: {
                Text(NSLocalizedString("AI providers footer image", comment: "AI providers"))
                    .font(.caption)
            }

            // ── Text / Chat providers (macOS extension; not in Win32 ProviderDlg) ──
            Section {
                ForEach(Array(textProviders.indices), id: \.self) { idx in
                    textProviderRow(index: idx)
                }
                Button(NSLocalizedString("Add text provider", comment: "AI providers")) {
                    textProviders.append(
                        .init(name: "custom-\(textProviders.count + 1)", apiKey: "", baseURL: "")
                    )
                }
            } header: {
                Text(NSLocalizedString("Text providers", comment: "AI providers"))
            }

            // ── Import / Export ─────────────────────────────────────────────────
            Section {
                HStack {
                    Button(NSLocalizedString("Import settings.json…", comment: "AI providers")) { importFromFile() }
                    Button(NSLocalizedString("Export settings.json…", comment: "AI providers")) { exportToFile() }
                }
            } header: {
                Text(NSLocalizedString("Import / Export", comment: "AI providers"))
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
    }

    // MARK: - Row builders

    /// Fixed canonical provider row: display name label + API key (with show/hide toggle) + base URL.
    /// Mirrors Win32 ProviderDlg LayoutProviderRow — no model field.
    @ViewBuilder
    private func canonicalRow(index: Int) -> some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 6) {
                    Group {
                        if canonicalRows[index].showKey {
                            TextField(
                                NSLocalizedString("API key", comment: "AI providers"),
                                text: $canonicalRows[index].apiKey
                            )
                        } else {
                            SecureField(
                                NSLocalizedString("API key", comment: "AI providers"),
                                text: $canonicalRows[index].apiKey
                            )
                        }
                    }
                    .textFieldStyle(.roundedBorder)

                    // Show / Hide toggle — mirrors Win32 ProviderRow.hShowBtn
                    Button(canonicalRows[index].showKey
                           ? NSLocalizedString("Hide", comment: "AI providers")
                           : NSLocalizedString("Show", comment: "AI providers")) {
                        canonicalRows[index].showKey.toggle()
                    }
                    .controlSize(.small)
                    .fixedSize()
                }
                TextField(
                    NSLocalizedString("Base URL", comment: "AI providers"),
                    text: $canonicalRows[index].baseURL
                )
                .textFieldStyle(.roundedBorder)
            }
            .padding(10)
        } label: {
            Text(canonicalRows[index].displayName)
                .fontWeight(.medium)
        }
        .padding(.vertical, 4)
        .padding(.horizontal, 2)
    }

    /// Dynamic text-only provider row: name + API key + base URL, with remove button.
    @ViewBuilder
    private func textProviderRow(index: Int) -> some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 8) {
                TextField(
                    NSLocalizedString("Provider name", comment: "AI providers"),
                    text: $textProviders[index].name
                )
                .textFieldStyle(.roundedBorder)
                SecureField(
                    NSLocalizedString("API key", comment: "AI providers"),
                    text: $textProviders[index].apiKey
                )
                .textFieldStyle(.roundedBorder)
                TextField(
                    NSLocalizedString("Base URL", comment: "AI providers"),
                    text: $textProviders[index].baseURL
                )
                .textFieldStyle(.roundedBorder)
                if textProviders.count > 1 {
                    Button(
                        NSLocalizedString("Remove provider", comment: "AI providers"),
                        role: .destructive
                    ) {
                        textProviders.remove(at: index)
                    }
                }
            }
            .padding(10)
        } label: {
            Text(
                textProviders[index].name.isEmpty
                    ? NSLocalizedString("Unnamed provider", comment: "AI providers")
                    : textProviders[index].name
            )
        }
        .padding(.vertical, 4)
        .padding(.horizontal, 2)
    }

    // MARK: - Load from disk

    private func loadFromDisk() {
        let root = PMSettingsStore.readJSON(path: settingsPath)
        let savedProviders = root["providers"] as? [String: Any] ?? [:]

        // Canonical rows: merge saved api_key + base_url, keep definition order.
        canonicalRows = pmCanonicalProviders.map { def in
            let saved = savedProviders[def.id] as? [String: Any] ?? [:]
            return PMCanonicalRow(
                name:           def.id,
                displayName:    def.displayName,
                defaultBaseURL: def.defaultBaseURL,
                apiKey:         saved["api_key"]  as? String ?? "",
                baseURL:        saved["base_url"] as? String ?? def.defaultBaseURL
            )
        }

        // Text providers.
        if let textMap = root["polymech_text_providers"] as? [String: Any], !textMap.isEmpty {
            textProviders = textMap.map { name, raw -> PMTextProviderRow in
                let d = raw as? [String: Any] ?? [:]
                let def = pmDefaultTextProviders.first(where: { $0.name == name })
                return PMTextProviderRow(
                    name:   name,
                    apiKey: d["api_key"]  as? String ?? "",
                    baseURL:d["base_url"] as? String ?? def?.baseURL ?? ""
                )
            }.sorted { $0.name < $1.name }
        } else if let chat = root["chat"] as? [String: Any] {
            // Migrate from legacy single-router chat block.
            let router  = chat["router"]   as? String ?? "openrouter"
            let def     = pmDefaultTextProviders.first(where: { $0.name == router })
            textProviders = [PMTextProviderRow(
                name:   router,
                apiKey: chat["api_key"]  as? String ?? "",
                baseURL:chat["base_url"] as? String ?? def?.baseURL ?? ""
            )]
        } else {
            textProviders = pmDefaultTextProviders
        }

        statusMessage = root.isEmpty
            ? String(format: NSLocalizedString("AI status no file",  comment: "AI providers"), settingsPath.path)
            : NSLocalizedString("AI status loaded", comment: "AI providers")
    }

    // MARK: - Save to disk

    private func saveToDisk() {
        var root = PMSettingsStore.readJSON(path: settingsPath)

        // Canonical providers → settings.json["providers"]
        // Matches Win32 save_providers: writes api_key + base_url per provider.
        // Preserves existing default_model so operation forms keep their state.
        var providers = root["providers"] as? [String: Any] ?? [:]
        for row in canonicalRows where !row.name.isEmpty {
            var entry = providers[row.name] as? [String: Any] ?? [:]
            if row.apiKey.isEmpty {
                entry.removeValue(forKey: "api_key")
            } else {
                entry["api_key"] = row.apiKey
            }
            entry["base_url"] = row.baseURL
            providers[row.name] = entry
        }
        root["providers"] = providers

        // Text providers → settings.json["polymech_text_providers"]
        var textMap: [String: Any] = [:]
        for p in textProviders where !p.name.isEmpty {
            var entry: [String: Any] = ["base_url": p.baseURL]
            if !p.apiKey.isEmpty { entry["api_key"] = p.apiKey }
            textMap[p.name] = entry
        }
        root["polymech_text_providers"] = textMap

        // Remove legacy active-provider keys (no longer used per design).
        root.removeValue(forKey: "active_provider")
        root.removeValue(forKey: "polymech_active_text_provider")

        do {
            try PMSettingsStore.writeJSON(path: settingsPath, root: root)
            statusMessage = String(
                format: NSLocalizedString("AI status saved", comment: "AI providers"),
                settingsPath.path
            )
        } catch {
            statusMessage = String(
                format: NSLocalizedString("AI status save failed", comment: "AI providers"),
                error.localizedDescription
            )
        }
    }

    // MARK: - Import / Export

    private func importFromFile() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let src = panel.url {
            do {
                let data = try Data(contentsOf: src)
                let obj  = try JSONSerialization.jsonObject(with: data)
                guard let root = obj as? [String: Any] else {
                    statusMessage = NSLocalizedString("AI import not object", comment: "AI providers")
                    return
                }
                try PMSettingsStore.writeJSON(path: settingsPath, root: root)
                loadFromDisk()
                statusMessage = String(
                    format: NSLocalizedString("AI status imported", comment: "AI providers"),
                    src.path
                )
            } catch {
                statusMessage = String(
                    format: NSLocalizedString("AI status import failed", comment: "AI providers"),
                    error.localizedDescription
                )
            }
        }
    }

    private func exportToFile() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "settings.json"
        if panel.runModal() == .OK, let out = panel.url {
            do {
                let root = PMSettingsStore.readJSON(path: settingsPath)
                try PMSettingsStore.writeJSON(path: out, root: root)
                statusMessage = String(
                    format: NSLocalizedString("AI status exported", comment: "AI providers"),
                    out.path
                )
            } catch {
                statusMessage = String(
                    format: NSLocalizedString("AI status export failed", comment: "AI providers"),
                    error.localizedDescription
                )
            }
        }
    }
}
