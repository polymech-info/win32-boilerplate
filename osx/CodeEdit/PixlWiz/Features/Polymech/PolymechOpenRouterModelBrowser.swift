import SwiftUI

/// OpenRouter text LLM model picker — same layout as Win32 ``ChatProviderDlg`` when the router is OpenRouter:
/// **model** combo (ids from `GET /v1/models`) + **Refresh** + read-only **details** (`OpenRouterSelectorController::update_meta_text`).
/// Unlike Replicate rows in the same dialog, OpenRouter has **no** collection combo — only this list + meta.
struct PolymechOpenRouterModelBrowser: View {
    @Binding var apiKey: String
    @Binding var baseURL: String
    @Binding var modelId: String
    /// Optional: push fetched model ids into a parent row (e.g. AI Providers `default_model` presets list).
    var modelOptions: Binding<[String]>? = nil

    private struct ModelEntry: Equatable {
        let id: String
        let name: String
        let description: String
        let openURL: String
        let detailHead: String
    }

    @State private var models: [ModelEntry] = []
    @State private var indexById: [String: Int] = [:]
    @State private var orError: String = ""
    @State private var busy = false

    var body: some View {
        Group {
            HStack(alignment: .firstTextBaseline) {
                Spacer(minLength: 8)
                Button {
                    Task { await refreshModels(force: true) }
                } label: {
                    if busy { ProgressView().controlSize(.small) } else { Text("↻").font(.body) }
                }
                .buttonStyle(.bordered)
                .help("Refresh OpenRouter `/v1/models` (same on-disk cache as `pm-image provider models list --provider openrouter`).")
            }
            if !models.isEmpty {
                Picker("Model", selection: $modelId) {
                    ForEach(models, id: \.id) { m in
                        Text(m.id).tag(m.id)
                    }
                }
                .disabled(busy)
            } else if !busy, orError.isEmpty {
                Text("Tap ↻ to load models from OpenRouter (API key optional — public `/v1/models` works without one).")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            metaBlock
            if !orError.isEmpty {
                Text(orError)
                    .font(.caption)
                    .foregroundStyle(.red)
            }
        }
        .onAppear {
            Task { await refreshModels(force: false) }
        }
        .onChange(of: apiKey) { _, _ in
            Task { await refreshModels(force: false) }
        }
        .onChange(of: baseURL) { _, _ in
            Task { await refreshModels(force: false) }
        }
    }

    @ViewBuilder
    private var metaBlock: some View {
        let text = detailsText()
        if !text.isEmpty {
            ScrollView {
                Text(text)
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
            }
            .frame(minHeight: 44, maxHeight: 160)
        }
    }

    /// Same content as Win32 `OpenRouterSelectorController::update_meta_text` (newlines as `\n`).
    private func detailsText() -> String {
        let mid = modelId.trimmingCharacters(in: .whitespacesAndNewlines)
        if mid.isEmpty { return "" }
        guard let idx = indexById[mid], idx >= 0, idx < models.count else {
            return "id: \(mid)\n(Select ↻ to load the model list, or type a model id the provider returned.)"
        }
        let m = models[idx]
        var parts: [String] = []
        if !m.name.isEmpty, m.name != m.id {
            parts.append("name: \(m.name)")
        }
        parts.append("id: \(m.id)")
        if !m.detailHead.isEmpty {
            parts.append(m.detailHead.trimmingCharacters(in: .whitespacesAndNewlines))
        }
        parts.append("---")
        if !m.description.isEmpty {
            parts.append(m.description)
        }
        if !m.openURL.isEmpty {
            parts.append("Page: \(m.openURL)")
        }
        var out = parts.joined(separator: "\n")
        if out.count > 12_000 {
            out = String(out.prefix(12_000))
        }
        return out
    }

    private func refreshModels(force: Bool) async {
        await MainActor.run {
            busy = true
            orError = ""
        }
        let key = await MainActor.run { apiKey }
        let base = await MainActor.run { baseURL }
        let curModel = await MainActor.run { modelId }
        do {
            let j = try await Task.detached {
                try PolymechImageEngine.runOpenRouterRequest([
                    "op": "models",
                    "api_key": key,
                    "base_url": base.isEmpty ? "https://openrouter.ai/api/v1" : base,
                    "force_refresh": force,
                ])
            }.value
            let parsed = try Self.parseModelsJSON(j)
            await MainActor.run {
                models = parsed
                var map: [String: Int] = [:]
                for (i, e) in parsed.enumerated() { map[e.id] = i }
                indexById = map
                let ids = parsed.map(\.id)
                modelOptions?.wrappedValue = ids
                if !ids.isEmpty {
                    let want = curModel.trimmingCharacters(in: .whitespacesAndNewlines)
                    if want.isEmpty {
                        modelId = ids[0]
                    }
                } else {
                    modelOptions?.wrappedValue = []
                }
            }
        } catch {
            await MainActor.run {
                orError = error.localizedDescription
                models = []
                indexById = [:]
                modelOptions?.wrappedValue = []
            }
        }
        await MainActor.run { busy = false }
    }

    private static func parseModelsJSON(_ json: String) throws -> [ModelEntry] {
        guard let d = json.data(using: .utf8),
              let root = try JSONSerialization.jsonObject(with: d) as? [String: Any] else {
            throw PolymechEngineError.c("Invalid OpenRouter JSON")
        }
        if let ok = root["ok"] as? Bool, !ok {
            let err = (root["error"] as? String) ?? "OpenRouter request failed"
            throw PolymechEngineError.c(err)
        }
        guard let arr = root["models"] as? [[String: Any]] else {
            throw PolymechEngineError.c("OpenRouter JSON missing models array")
        }
        var out: [ModelEntry] = []
        for m in arr {
            guard let id = m["id"] as? String, !id.isEmpty else { continue }
            let name = (m["name"] as? String) ?? id
            let desc = (m["description"] as? String) ?? ""
            let url = (m["open_url"] as? String) ?? ""
            let head = (m["detail_head"] as? String) ?? ""
            out.append(ModelEntry(id: id, name: name, description: desc, openURL: url, detailHead: head))
        }
        if out.isEmpty { throw PolymechEngineError.c("OpenRouter returned no models") }
        return out
    }
}
