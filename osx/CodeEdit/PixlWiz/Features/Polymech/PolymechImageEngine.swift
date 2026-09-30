import Foundation

// C API from `pm_polymech_bridge.h` — add `dist-osx/polymech-cmake-libs/include` to Header Search + link `libpolymech-bridge.a`
// and the same static stack as `pm-codeedit` (or call `pm-image` as subprocess for a host-only path).

@_silgen_name("pm_polymech_free")
private func _pmFree(_: UnsafeMutableRawPointer?)

@_silgen_name("pm_polymech_resize")
private func _pmResize(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_meta")
private func _pmMeta(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_compress")
private func _pmCompress(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_find")
private func _pmFind(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_transform")
private func _pmTransform(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_chat_turn")
private func _pmChatTurn(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_replicate")
private func _pmReplicate(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_openrouter")
private func _pmOpenRouter(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_service_create_post")
private func _pmServiceCreatePost(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_mcp_probe")
private func _pmMcpProbe(_ json: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, _ err: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32

@_silgen_name("pm_polymech_init_process_logging")
private func _pmInitProcessLogging()

public enum PolymechImageEngine {
    /// Registers spdlog stderr + `~/Desktop/pm-image.log` (same as `pm-image` CLI), posts the path
    /// to unified logging for **Console.app**, and installs the in-process log panel sink.
    public static func bootstrapNativeLoggingAtLaunch() {
        _pmInitProcessLogging()
        PolymechLogSink.shared.install()
    }

    /// - Returns: (ok JSON string, error string). Frees C buffers from the bridge.
    public static func runResize(input: String, output: String, settings: PolymechImageSettings) throws -> String {
        let opt = try Self.mergeApiKey(into: settings.resize, key: settings.providerApiKey)
        let body: [String: Any] = [
            "input": input,
            "output": output,
            "options": (try? JSONSerialization.jsonObject(with: Data(opt.utf8))) as? [String: Any] ?? [:]
        ]
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        let json = String(data: data, encoding: .utf8)!
        return try invoke(_pmResize, json: json)
    }

    public static func runMeta(input: String, settings: PolymechImageSettings) throws -> String {
        let withProviderDefaults = Self.mergeImageProviderDefaults(into: settings.meta)
        let opt = try Self.mergeApiKey(into: withProviderDefaults, key: settings.providerApiKey)
        let body: [String: Any] = [
            "input": input,
            "options": (try? JSONSerialization.jsonObject(with: Data(opt.utf8))) as? [String: Any] ?? [:]
        ]
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        return try invoke(_pmMeta, json: String(data: data, encoding: .utf8)!)
    }

    public static func runCompress(input: String, output: String, settings: PolymechImageSettings) throws -> String {
        let opt = settings.compress
        let body: [String: Any] = [
            "input": input,
            "output": output,
            "options": (try? JSONSerialization.jsonObject(with: Data(opt.utf8))) as? [String: Any] ?? [:]
        ]
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        return try invoke(_pmCompress, json: String(data: data, encoding: .utf8)!)
    }

    public static func runFind(inputs: [String], settings: PolymechImageSettings) throws -> String {
        let opt = try Self.mergeApiKey(into: settings.find, key: settings.providerApiKey)
        var body: [String: Any] = ["inputs": inputs]
        if let o = try? JSONSerialization.jsonObject(with: Data(opt.utf8)) as? [String: Any] {
            body["options"] = o
        } else {
            body["options"] = [:]
        }
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        return try invoke(_pmFind, json: String(data: data, encoding: .utf8)!)
    }

    public static func runTransform(input: String, output: String?, settings: PolymechImageSettings) throws -> String {
        let withProviderDefaults = Self.mergeImageProviderDefaults(into: settings.transform)
        let opt = try Self.mergeApiKey(into: withProviderDefaults, key: settings.providerApiKey)
        var body: [String: Any] = [
            "input": input,
            "options": (try? JSONSerialization.jsonObject(with: Data(opt.utf8))) as? [String: Any] ?? [:]
        ]
        if let output { body["output"] = output } else { body["output"] = NSNull() }
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        return try invoke(_pmTransform, json: String(data: data, encoding: .utf8)!)
    }

    // Passes only explicit overrides; C++ runtime_settings resolves provider, model, and
    // credentials from the saved profile. Callers must not inject active/default provider rows.
    static func runChatTurn(
        prompt: String,
        selection: [String],
        folderHint: String,
        systemExtra: String? = nil,
        routerOverride: String? = nil,
        modelOverride: String? = nil,
        disableTools: [String]? = nil,
        mcpToolsEnabled: Bool = true,
        disabledMcpServers: [String]? = nil
    ) throws -> String {
        var body: [String: Any] = [
            "prompt": prompt,
            "selection": selection,
            "folder": folderHint
        ]
        if let systemExtra, !systemExtra.isEmpty { body["system_extra"] = systemExtra }
        if let r = routerOverride, !r.isEmpty { body["router"] = r }
        if let m = modelOverride, !m.isEmpty { body["model"] = m }
        if let disableTools, !disableTools.isEmpty { body["disable_tools"] = disableTools }
        if !mcpToolsEnabled { body["mcp_tools_enabled"] = false }
        if let disabledMcpServers, !disabledMcpServers.isEmpty { body["disabled_mcp_servers"] = disabledMcpServers }
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        return try invoke(_pmChatTurn, json: String(data: data, encoding: .utf8)!)
    }

    /// Probe mcp.json: handshake + tools/list for each configured server.
    /// Returns a slim catalog `[String: Any]` matching Win32 `chat_web_start_mcp_catalog_probe`.
    /// Throws on JSON serialisation failure; never throws on probe/network errors (they appear in `probe_error`).
    static func runMcpProbe() throws -> [String: Any] {
        let data = try JSONSerialization.data(withJSONObject: [:], options: [])
        let raw = try invoke(_pmMcpProbe, json: String(data: data, encoding: .utf8)!)
        guard let d = raw.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: d) as? [String: Any]
        else { throw PolymechEngineError.c("mcp_probe: invalid JSON response") }
        return obj
    }

    /// `op`: `collections` | `models` | `resolve_collection` — @see `pm_polymech_replicate` / `replicate_provider_models_cli.hpp`.
    static func runReplicateRequest(_ body: [String: Any]) throws -> String {
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        guard let json = String(data: data, encoding: .utf8) else {
            throw PolymechEngineError.c("UTF-8 encode (replicate)")
        }
        return try invoke(_pmReplicate, json: json)
    }

    /// `op`: `models` — @see `pm_polymech_openrouter` / Win32 `OpenRouterSelectorController`.
    static func runOpenRouterRequest(_ body: [String: Any]) throws -> String {
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        guard let json = String(data: data, encoding: .utf8) else {
            throw PolymechEngineError.c("UTF-8 encode (openrouter)")
        }
        return try invoke(_pmOpenRouter, json: json)
    }

    /// Same HTTP flow as `pm-image service posts create` (ZITADEL bearer beside PixelWiz `settings.json` on macOS).
    public static func runServiceCreatePost(
        imagePaths: [String],
        title: String?,
        description: String?,
        visibility: String,
        serverURL: String?
    ) throws -> PolymechServiceCreatePostResult {
        var body: [String: Any] = ["images": imagePaths, "visibility": visibility]
        if let title, !title.isEmpty { body["title"] = title }
        if let description, !description.isEmpty { body["description"] = description }
        if let serverURL, !serverURL.isEmpty { body["server_url"] = serverURL }
        let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
        guard let json = String(data: data, encoding: .utf8) else {
            throw PolymechEngineError.c("UTF-8 encode (service create post)")
        }
        let out = try invoke(_pmServiceCreatePost, json: json)
        guard let d = out.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: d) as? [String: Any],
              let ok = obj["ok"] as? Bool, ok,
              let postId = obj["post_id"] as? String,
              let viewUrl = obj["view_url"] as? String
        else {
            throw PolymechEngineError.c("unexpected service create post response")
        }
        let pics = (obj["picture_ids"] as? [String]) ?? []
        return PolymechServiceCreatePostResult(postId: postId, pictureIds: pics, viewURL: viewUrl)
    }

    // MARK: - Private

    private static func mergeApiKey(into optionsJson: String, key: String) throws -> String {
        if key.isEmpty { return optionsJson }
        guard
            var obj = try? JSONSerialization.jsonObject(with: Data(optionsJson.utf8)) as? [String: Any]
        else {
            throw PolymechEngineError.c("Polymech options must be a JSON object string.")
        }
        if obj["api_key"] == nil { obj["api_key"] = key }
        let d = try JSONSerialization.data(withJSONObject: obj, options: [.sortedKeys])
        guard let s = String(data: d, encoding: .utf8) else { throw PolymechEngineError.c("UTF-8 encode") }
        return s
    }

    // Fills api_key and base_url from the saved provider row — only when an explicit provider is
    // already present in the options JSON. Does not inject active/default provider or defaultModel.
    private static func mergeImageProviderDefaults(into optionsJson: String) -> String {
        guard var obj = try? JSONSerialization.jsonObject(with: Data(optionsJson.utf8)) as? [String: Any] else {
            return optionsJson
        }
        guard let providerName = obj["provider"] as? String, !providerName.isEmpty else {
            return optionsJson
        }
        if let row = PolymechProviderSettingsStore.imageProvider(named: providerName) {
            if (obj["base_url"] as? String ?? "").isEmpty, !row.baseURL.isEmpty {
                obj["base_url"] = row.baseURL
            }
            if obj["api_key"] == nil, !row.apiKey.isEmpty {
                obj["api_key"] = row.apiKey
            }
        }
        guard let data = try? JSONSerialization.data(withJSONObject: obj, options: [.sortedKeys]),
              let out = String(data: data, encoding: .utf8) else {
            return optionsJson
        }
        return out
    }

    private static func invoke(
        _ fn: (UnsafePointer<CChar>?, UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32,
        json: String
    ) throws -> String {
        var out: UnsafeMutablePointer<CChar>?
        var err: UnsafeMutablePointer<CChar>?
        let code = json.withCString { c in
            fn(c, &out, &err)
        }
        defer {
            if let p = out { _pmFree(p) }
            if let e = err { _pmFree(e) }
        }
        if code != 0 {
            if let e = err { throw PolymechEngineError.c(String(cString: e)) }
            throw PolymechEngineError.c("unknown (code \(code))")
        }
        guard let o = out else { throw PolymechEngineError.c("no output") }
        return String(cString: o)
    }
}

public struct PolymechServiceCreatePostResult: Sendable {
    public let postId: String
    public let pictureIds: [String]
    public let viewURL: String
}

public enum PolymechEngineError: Error, LocalizedError {
    case c(String)
    public var errorDescription: String? {
        switch self {
        case .c(let s): return s
        }
    }
}
