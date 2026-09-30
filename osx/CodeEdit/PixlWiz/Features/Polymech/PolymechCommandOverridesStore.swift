import Foundation

/// Keys under `command_provider_overrides` in the shared `settings.json` (Win32
/// `LoadCommandProviderOverrides` / `SaveCommandProviderOverrides`).
enum PolymechCommandOverrideKey: String, CaseIterable {
    case meta
    case transform
    case find
}

/// Read/write the `command_provider_overrides` subtree; merges with the rest of
/// `settings.json` so we do not clobber `providers` / `chat` / other keys.
enum PolymechCommandOverridesStore {
    private static let subtreeKey = "command_provider_overrides"

    private static func settingsPath() -> URL {
        return FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/PolyMech/pm-image/settings.json")
    }

    private static func loadRoot() -> [String: Any] {
        let path = settingsPath()
        guard let d = try? Data(contentsOf: path), !d.isEmpty else { return [:] }
        guard let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return [:] }
        return o
    }

    private static func saveRoot(_ root: [String: Any]) throws {
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

    static func get(_ key: PolymechCommandOverrideKey) -> (provider: String?, model: String?, collection: String?) {
        guard let raw = loadRoot()[subtreeKey] as? [String: Any],
              let entry = raw[key.rawValue] as? [String: Any] else {
            return (nil, nil, nil)
        }
        return (
            entry["provider"] as? String,
            entry["model"] as? String,
            entry["collection"] as? String
        )
    }

    static func set(
        _ key: PolymechCommandOverrideKey,
        provider: String,
        model: String,
        collection: String?
    ) {
        do {
            var root = loadRoot()
            var subtree = (root[subtreeKey] as? [String: Any]) ?? [:]
            var entry: [String: Any] = [
                "provider": provider,
                "model": model
            ]
            if provider == "replicate",
               let c = collection,
               !c.trimmingCharacters(in: .whitespaces).isEmpty {
                entry["collection"] = c
            }
            subtree[key.rawValue] = entry
            root[subtreeKey] = subtree
            try saveRoot(root)
        } catch {
            // Best-effort persistence; do not break the form.
        }
    }
}
