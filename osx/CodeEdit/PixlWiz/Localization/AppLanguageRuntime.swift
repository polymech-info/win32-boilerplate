import Foundation

/// Applies `UserDefaults` `AppleLanguages` from saved settings *before* `Settings.shared` runs,
/// so `Bundle` picks the correct `Localizable.strings` on first layout.
enum AppLanguageRuntime {
    private static var settingsFileURL: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/CodeEdit", directoryHint: .isDirectory)
            .appending(path: "settings")
            .appendingPathExtension("json")
    }

    /// Read `settings.json` once (no `Settings` init) and sync `AppleLanguages` for the next
    /// localized lookup. Call from `CodeEditApp.init` before `Settings.shared`.
    static func applyFromSavedFileIfPresent() {
        let url = settingsFileURL
        guard let data = try? Data(contentsOf: url) else { return }
        let decoded = try? JSONDecoder().decode(SettingsData.self, from: data)
        if let value = decoded?.general.appLanguage {
            applyUserDefaults(value)
        }
    }

    static func applyUserDefaults(_ language: SettingsData.AppLanguage) {
        switch language {
        case .system:
            UserDefaults.standard.removeObject(forKey: "AppleLanguages")
        case .en:
            UserDefaults.standard.set(["en"], forKey: "AppleLanguages")
        case .es:
            UserDefaults.standard.set(["es"], forKey: "AppleLanguages")
        case .it:
            UserDefaults.standard.set(["it"], forKey: "AppleLanguages")
        case .fr:
            UserDefaults.standard.set(["fr"], forKey: "AppleLanguages")
        case .de:
            UserDefaults.standard.set(["de"], forKey: "AppleLanguages")
        case .ptBR:
            UserDefaults.standard.set(["pt-BR"], forKey: "AppleLanguages")
        }
    }
}
