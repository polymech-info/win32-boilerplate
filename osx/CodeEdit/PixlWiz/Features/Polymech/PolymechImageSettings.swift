import Foundation

/// Per-operation JSON `options` objects, merged by `pm_polymech_*` with `apply_*_from_json` in C++.
/// Edit these strings in the Polymech settings UI (or paste JSON from `pm-image` / REST examples).
public struct PolymechImageSettings: Codable, Equatable, Sendable {
    // Single-line JSON for `JSONSerialization` / C++ `apply_*_from_json`.
    public static let defaultResizeJSON =
        "{\"constrain_proportions\":true,\"output_mode\":\"sibling\",\"output_basename_infix\":\"pm_resize\",\"max_width\":800,\"max_height\":800,\"format\":\"jpeg\",\"quality\":85,\"fit\":\"inside\",\"cache_enabled\":false}"
    public static let defaultMetaJSON =
        "{\"provider\":\"google\",\"model\":\"gemini-3-pro-image-preview\",\"resize_first\":true,\"resize_width\":512,\"out_md\":true,\"out_json\":true,\"dry_run\":false}"
    public static let defaultCompressJSON =
        "{\"output_mode\":\"sibling\",\"output_basename_infix\":\"pm_compress\",\"compressor\":\"auto\",\"jpeg_quality\":85,\"strip_metadata\":true}"
    public static let defaultFindJSON =
        "{\"mode\":\"name\",\"prompt\":\"\",\"case_insensitive\":true,\"match_folders\":true,\"recursive\":true,\"max_results\":0,\"dry_run\":false,\"meta\":{}}"
    public static let defaultTransformJSON = "{\"provider\":\"google\",\"model\":\"gemini-3-pro-image-preview\",\"prompt\":\"\",\"resize_first\":false}"

    public var resize: String
    public var meta: String
    public var compress: String
    public var find: String
    public var transform: String
    /// Optional: merged into `meta` / `find` / `transform` `options` when you set a single app-wide key.
    public var providerApiKey: String

    public static let `defaults` = PolymechImageSettings(
        resize: defaultResizeJSON,
        meta: defaultMetaJSON,
        compress: defaultCompressJSON,
        find: defaultFindJSON,
        transform: defaultTransformJSON,
        providerApiKey: ""
    )

    public init(
        resize: String = PolymechImageSettings.defaultResizeJSON,
        meta: String = PolymechImageSettings.defaultMetaJSON,
        compress: String = PolymechImageSettings.defaultCompressJSON,
        find: String = PolymechImageSettings.defaultFindJSON,
        transform: String = PolymechImageSettings.defaultTransformJSON,
        providerApiKey: String = ""
    ) {
        self.resize = resize
        self.meta = meta
        self.compress = compress
        self.find = find
        self.transform = transform
        self.providerApiKey = providerApiKey
    }
}

public enum PolymechImageSettingsStore {
    private static let key = "com.polymech.PolymechImageSettings.v1"

    public static func load() -> PolymechImageSettings {
        guard let d = UserDefaults.standard.data(forKey: key) else { return .defaults }
        return (try? JSONDecoder().decode(PolymechImageSettings.self, from: d)) ?? .defaults
    }

    public static func save(_ s: PolymechImageSettings) {
        if let d = try? JSONEncoder().encode(s) {
            UserDefaults.standard.set(d, forKey: key)
        }
    }
}
