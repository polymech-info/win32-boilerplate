import SwiftUI

/// Collection + model pickers for Replicate: same native bridge as `pm-image` / Win32
/// `ReplicateSelectorController` + `replicate_provider_models_cli`.
///
/// When `command` is set, `collection` + `model` are also written to
/// `command_provider_overrides` in the shared `settings.json` (Win32 parity).
struct PolymechReplicateModelBrowser: View {
    @Binding var apiKey: String
    @Binding var baseURL: String
    @Binding var modelSlug: String
    var modelOptions: Binding<[String]>? = nil
    /// When non-nil, loads/saves `command_provider_overrides[command]`.
    var command: PolymechCommandOverrideKey? = nil

    @State private var collections: [(slug: String, title: String)] = []
    @State private var selectedCollectionSlug: String = "official"
    @State private var modelSlugs: [String] = []
    @State private var modelMeta: [String: (description: String, url: String, visibility: String, official: Bool)] = [:]
    @State private var metaLine: String = ""
    @State private var repError: String = ""
    @State private var busy = false

    var body: some View {
        Group {
            HStack(alignment: .firstTextBaseline) {
                Picker("Collection", selection: $selectedCollectionSlug) {
                    ForEach(collections, id: \.slug) { c in
                        Text(c.title).tag(c.slug)
                    }
                }
                .frame(minWidth: 220)
                .disabled(busy || collections.isEmpty)
                Spacer(minLength: 8)
                Button {
                    Task { await refreshAll(force: true) }
                } label: {
                    if busy { ProgressView().controlSize(.small) } else { Text("↻").font(.body) }
                }
                .buttonStyle(.bordered)
                .help("Re-fetch collections and models (same on-disk cache as `pm-image provider models list`).")
            }
            if !modelSlugs.isEmpty {
                Picker("Models in collection", selection: $modelSlug) {
                    ForEach(modelSlugs, id: \.self) { slug in
                        // Use the same string as the stored `owner/model` value (avoids a misleading
                        // `g/google/...` display that looked like a bad API slug to users and tools).
                        Text(slug).tag(slug)
                    }
                }
                .disabled(busy)
            }
            if !metaLine.isEmpty {
                Text(metaLine)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
            if !repError.isEmpty {
                Text(repError)
                    .font(.caption)
                    .foregroundStyle(.red)
            }
        }
        .onAppear {
            Task { await refreshAll(force: false) }
        }
        .onChange(of: selectedCollectionSlug) { _, _ in
            Task { await loadModelsOnly(force: false) }
        }
        .onChange(of: modelSlug) { _, new in
            updateMeta(for: new)
            persistCommandOverrideIfNeeded()
        }
    }

    private func updateMeta(for slug: String) {
        guard let m = modelMeta[slug] else {
            metaLine = ""
            return
        }
        var parts: [String] = [
            "visibility: \(m.visibility.isEmpty ? "unknown" : m.visibility)",
            "official: \(m.official ? "true" : "false")"
        ]
        if !m.description.isEmpty {
            let d = m.description.count > 220 ? String(m.description.prefix(220)) + "…" : m.description
            parts.append("description: \(d)")
        }
        if !m.url.isEmpty { parts.append("url: \(m.url)") }
        metaLine = parts.joined(separator: "  •  ")
    }

    private func persistCommandOverrideIfNeeded() {
        guard let command else { return }
        PolymechCommandOverridesStore.set(
            command,
            provider: "replicate",
            model: modelSlug,
            collection: selectedCollectionSlug
        )
    }

    private func refreshAll(force: Bool) async {
        await MainActor.run {
            busy = true
            repError = ""
        }
        do {
            let key = await MainActor.run { apiKey }
            let base = await MainActor.run { baseURL }
            let jCol = try await Task.detached {
                try PolymechImageEngine.runReplicateRequest([
                    "op": "collections",
                    "api_key": key,
                    "base_url": base,
                    "force_refresh": force
                ])
            }.value
            let parsed = try Self.parseCollectionsList(jCol)
            await MainActor.run {
                collections = parsed
                if !parsed.contains(where: { $0.slug == selectedCollectionSlug }) {
                    selectedCollectionSlug = parsed.first?.slug ?? "official"
                }
            }
            if let cmd = command,
               let c = PolymechCommandOverridesStore.get(cmd).collection,
               !c.isEmpty,
               parsed.contains(where: { $0.slug == c }) {
                await MainActor.run { selectedCollectionSlug = c }
            } else {
                let mslug = await MainActor.run { modelSlug }
                if !mslug.isEmpty {
                    do {
                        let jRes = try await Task.detached {
                            try PolymechImageEngine.runReplicateRequest([
                                "op": "resolve_collection",
                                "model_slug": mslug
                            ])
                        }.value
                        if let guessed = try Self.parseOptionalCollectionSlug(jRes) {
                            await MainActor.run {
                                if parsed.contains(where: { $0.slug == guessed }) {
                                    selectedCollectionSlug = guessed
                                }
                            }
                        }
                    } catch { /* best-effort */ }
                }
            }
            await loadModelsOnly(force: force)
        } catch {
            await MainActor.run { repError = error.localizedDescription }
        }
        await MainActor.run { busy = false }
    }

    private func loadModelsOnly(force: Bool) async {
        await MainActor.run { busy = true; repError = "" }
        let key = await MainActor.run { apiKey }
        let base = await MainActor.run { baseURL }
        let coll = await MainActor.run { selectedCollectionSlug }
        do {
            let j = try await Task.detached {
                try PolymechImageEngine.runReplicateRequest([
                    "op": "models",
                    "api_key": key,
                    "base_url": base,
                    "collection": coll,
                    "force_refresh": force
                ])
            }.value
            let (slugs, meta) = try Self.parseModelsList(j)
            await MainActor.run {
                modelSlugs = slugs
                modelMeta = meta
                if let b = modelOptions {
                    b.wrappedValue = slugs
                }
                if !slugs.isEmpty, !slugs.contains(modelSlug) {
                    modelSlug = slugs[0]
                }
                updateMeta(for: modelSlug)
            }
        } catch {
            await MainActor.run {
                repError = error.localizedDescription
                modelSlugs = []
                modelMeta = [:]
                metaLine = ""
            }
        }
        await MainActor.run {
            busy = false
            persistCommandOverrideIfNeeded()
        }
    }

    private static func parseCollectionsList(_ json: String) throws -> [(slug: String, title: String)] {
        guard let d = json.data(using: .utf8),
              let root = try JSONSerialization.jsonObject(with: d) as? [String: Any],
              let arr = root["collections"] as? [[String: Any]] else {
            throw PolymechEngineError.c("Invalid Replicate collections JSON")
        }
        var out: [(String, String)] = []
        for c in arr {
            guard let slug = c["slug"] as? String, !slug.isEmpty else { continue }
            let name = (c["name"] as? String) ?? slug
            out.append((slug, name))
        }
        if out.isEmpty { throw PolymechEngineError.c("Replicate returned no collections") }
        return out
    }

    private static func parseOptionalCollectionSlug(_ json: String) throws -> String? {
        guard let d = json.data(using: .utf8),
              let root = try JSONSerialization.jsonObject(with: d) as? [String: Any] else { return nil }
        if let s = root["collection"] as? String { return s }
        if root["collection"] is NSNull { return nil }
        return nil
    }

    private static func parseModelsList(_ json: String) throws -> (
        [String], [String: (description: String, url: String, visibility: String, official: Bool)]
    ) {
        guard let d = json.data(using: .utf8),
              let root = try JSONSerialization.jsonObject(with: d) as? [String: Any],
              let arr = root["models"] as? [[String: Any]] else {
            throw PolymechEngineError.c("Invalid Replicate models JSON")
        }
        var slugs: [String] = []
        var meta: [String: (String, String, String, Bool)] = [:]
        for m in arr {
            guard let slug = m["slug"] as? String, !slug.isEmpty else { continue }
            let desc = (m["description"] as? String) ?? ""
            let url = (m["url"] as? String) ?? ""
            let vis = (m["visibility"] as? String) ?? ""
            let off = (m["is_official"] as? Bool) ?? false
            slugs.append(slug)
            meta[slug] = (desc, url, vis, off)
        }
        if slugs.isEmpty { throw PolymechEngineError.c("Replicate returned no models in this collection") }
        return (slugs, meta)
    }
}
