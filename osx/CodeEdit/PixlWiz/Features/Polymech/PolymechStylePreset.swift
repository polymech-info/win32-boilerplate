import Foundation

/// A quick-action / style preset — same shape as `apps/chat` + `settings.json` → `chat_web.quick_actions`.
struct PolymechStylePreset: Hashable, Sendable, Identifiable {
    var id: String
    var name: String
    var prompt: String
    var icon: String
}

/// Payload for ``WorkspaceDocument.polymechStylePresetSheet`` (SwiftUI `.sheet` — see ``PolymechStylePresetRunCoordinator``).
struct PolymechStylePresetSheetContext: Identifiable, Equatable {
    let id = UUID()
    let preset: PolymechStylePreset
    let paths: [String]
    let imageURLs: [URL]

    static func == (lhs: PolymechStylePresetSheetContext, rhs: PolymechStylePresetSheetContext) -> Bool {
        lhs.id == rhs.id
    }
}

// MARK: - Load (parallels `main.js` + `i18n.js` `defaults`)

enum PolymechStylePresetLoader {
    private static let defaultPresets: [PolymechStylePreset] = [
        .init(id: "1", name: "Enhance", prompt: "Enhance and improve this image", icon: "✨"),
        .init(
            id: "2",
            name: "Make Artistic",
            prompt: "Transform this into a beautiful artistic painting",
            icon: "🎨"
        ),
        .init(
            id: "3",
            name: "Cyberpunk Style",
            prompt: "Transform this into cyberpunk style with neon colors",
            icon: "🌆"
        ),
        .init(
            id: "4",
            name: "Fantasy",
            prompt: "Transform this into a fantasy art style",
            icon: "🧙"
        ),
        .init(
            id: "5",
            name: "Portrait",
            prompt: "Transform this into a professional portrait",
            icon: "👤"
        )
    ]

    /// Reads `settings.json` → `chat_web.quick_actions` (Win32 and CodeEdit web chat both persist the subtree).
    /// if missing or empty, returns the same English defaults as `i18n.js` `getDefaultQuickActions` for `en`.
    static func all() -> [PolymechStylePreset] {
        let root = PolymechProviderSettingsStore.readSettingsRoot()
        guard
            let web = root["chat_web"] as? [String: Any],
            let raw = web["quick_actions"] as? [[String: Any]],
            !raw.isEmpty
        else {
            return defaultPresets
        }
        let parsed: [PolymechStylePreset] = raw.compactMap { o in
            let id = (o["id"] as? String) ?? ""
            let name = (o["name"] as? String) ?? ""
            let p = (o["prompt"] as? String) ?? ""
            if name.isEmpty, p.isEmpty { return nil }
            return PolymechStylePreset(
                id: id,
                name: name,
                prompt: p,
                icon: (o["icon"] as? String) ?? ""
            )
        }
        return parsed.isEmpty ? defaultPresets : parsed
    }
}
