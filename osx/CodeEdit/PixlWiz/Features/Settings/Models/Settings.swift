//
//  Settings.swift
//  CodeEditModules/Settings
//
//  Created by Lukas Pistrol on 01.04.22.
//

import Foundation
import SwiftUI
import Combine

/// The Preferences View Model. Accessible via the singleton "``SettingsModel/shared``".
///
/// **Usage:**
/// ```swift
/// @StateObject
/// private var prefs: SettingsModel = .shared
/// ```
final class Settings: ObservableObject {

    /// The publicly available singleton instance of ``SettingsModel``
    static let shared: Settings = .init()

    private var storeTask: AnyCancellable!

    private init() {
        self.preferences = .init()
        self.preferences = loadSettings()

        self.storeTask = self.$preferences.throttle(for: 2, scheduler: RunLoop.main, latest: true).sink {
            try? self.savePreferences($0)
        }
    }

    static subscript<T>(_ path: WritableKeyPath<SettingsData, T>, suite: Settings = .shared) -> T {
        get {
            suite.preferences[keyPath: path]
        }
        set {
            suite.preferences[keyPath: path] = newValue
        }
    }

    /// Published instance of the ``Settings`` model.
    ///
    /// Changes are saved automatically.
    @Published var preferences: SettingsData

    /// Load and construct ``Settings`` model from
    /// `~/Library/Application Support/CodeEdit/settings.json`
    private func loadSettings() -> SettingsData {
        if !filemanager.fileExists(atPath: settingsURL.path) {
            try? filemanager.createDirectory(at: baseURL, withIntermediateDirectories: false)
            return .init()
        }

        guard let json = try? Data(contentsOf: settingsURL),
              let prefs = try? JSONDecoder().decode(SettingsData.self, from: json)
        else {
            return .init()
        }
        return prefs
    }

    /// Save``Settings`` model to
    /// `~/Library/Application Support/CodeEdit/settings.json`
    ///
    /// Merges with any existing top-level keys (e.g. `chat`, `providers` mirrored from
    /// `pm-image settings import` / PolyMech portable `settings.json`) so CodeEdit prefs
    /// throttles do not strip keys outside ``SettingsData``.
    private func savePreferences(_ data: SettingsData) throws {
        var disk: [String: Any] = [:]
        if filemanager.fileExists(atPath: settingsURL.path),
           let existing = try? Data(contentsOf: settingsURL),
           let obj = try? JSONSerialization.jsonObject(with: existing) as? [String: Any] {
            disk = obj
        }
        let encoded = try JSONEncoder().encode(data)
        guard let prefsObj = try JSONSerialization.jsonObject(with: encoded) as? [String: Any] else {
            return
        }
        for (k, v) in prefsObj {
            disk[k] = v
        }
        let prettyJSON = try JSONSerialization.data(
            withJSONObject: disk,
            options: [.prettyPrinted, .sortedKeys]
        )
        try prettyJSON.write(to: settingsURL, options: .atomic)
    }

    /// Default instance of the `FileManager`
    private let filemanager = FileManager.default

    /// The base URL of settings.
    ///
    /// Points to `~/Library/Application Support/CodeEdit/`
    internal var baseURL: URL {
        filemanager
            .homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/CodeEdit", directoryHint: .isDirectory)
    }

    /// The URL of the `settings.json` settings file.
    ///
    /// Points to `~/Library/Application Support/CodeEdit/settings.json`
    private var settingsURL: URL {
        baseURL
            .appending(path: "settings")
            .appendingPathExtension("json")
    }
}
