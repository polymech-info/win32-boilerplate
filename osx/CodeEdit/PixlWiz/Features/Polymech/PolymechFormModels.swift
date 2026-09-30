import Foundation

// MARK: - JSON helpers (align with `apply_*_options_from_json` keys in C++)

enum PolymechFormJSON {
    static func readInt(_ d: [String: Any], _ key: String, _ default: Int) -> Int {
        if let v = d[key] as? Int { return v }
        if let n = d[key] as? NSNumber { return n.intValue }
        return `default`
    }

    static func readString(_ d: [String: Any], _ key: String, _ default: String) -> String {
        (d[key] as? String) ?? `default`
    }

    static func readBool(_ d: [String: Any], _ key: String, _ default: Bool) -> Bool {
        if let b = d[key] as? Bool { return b }
        if let n = d[key] as? NSNumber { return n.boolValue }
        return `default`
    }

    static func parseObject(_ s: String) -> [String: Any] {
        guard let data = s.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return [:] }
        return obj
    }

    static func encodeSorted(_ o: [String: Any]) -> String {
        if JSONSerialization.isValidJSONObject(o),
           let d = try? JSONSerialization.data(withJSONObject: o, options: [.sortedKeys]) {
            return String(data: d, encoding: .utf8) ?? "{}"
        }
        return "{}"
    }
}

// MARK: - Output filename infix (sibling `name_<infix>.ext`)

enum PolymechOutputInfix {
    static func sanitize(_ raw: String, fallback: String) -> String {
        let t = String(raw.filter { $0.isLetter || $0.isNumber || $0 == "_" }).lowercased()
        if t.isEmpty { return fallback }
        return String(t.prefix(64))
    }
}

// MARK: - Resize (media::ResizeOptions + JSON)

struct PolymechResizeFormData: Equatable {
    /// `sibling` = new file next to the source; `in_place` = overwrite (engine uses a temp+replace on disk).
    var output_mode: String
    /// Inserted in `name_<infix>.ext` for sibling output (e.g. `pm_resize` → `photo_pm_resize.jpg`). See ``effectiveBasenameInfix()``.
    var output_basename_infix: String
    var max_width: Int
    var max_height: Int
    var quality: Int
    var format: String
    var fit: String
    var position: String
    var kernel: String
    var without_enlargement: Bool
    var autorotate: Bool
    var strip_metadata: Bool
    var cache_enabled: Bool
    var rotate: Int
    /// When true, changing width (or height) updates the other from the first image’s aspect, or the current w:h ratio.
    var constrain_proportions: Bool

    static let defaults = PolymechResizeFormData(
        output_mode: "sibling",
        output_basename_infix: "pm_resize",
        max_width: 800,
        max_height: 800,
        quality: 85,
        format: "jpeg",
        fit: "inside",
        position: "centre",
        kernel: "lanczos3",
        without_enlargement: true,
        autorotate: true,
        strip_metadata: true,
        cache_enabled: false,
        rotate: 0,
        constrain_proportions: true
    )

    static func from(json: String) -> PolymechResizeFormData {
        let d = PolymechFormJSON.parseObject(json)
        var t = PolymechResizeFormData.defaults
        t.output_mode = PolymechFormJSON.readString(d, "output_mode", t.output_mode)
        if t.output_mode != "in_place" { t.output_mode = "sibling" }
        t.output_basename_infix = PolymechFormJSON.readString(d, "output_basename_infix", t.output_basename_infix)
        if t.output_basename_infix.isEmpty, let legacy = d["output_suffix"] as? String, !legacy.isEmpty {
            t.output_basename_infix = legacy
        }
        t.max_width = PolymechFormJSON.readInt(d, "max_width", t.max_width)
        t.max_height = PolymechFormJSON.readInt(d, "max_height", t.max_height)
        t.quality = PolymechFormJSON.readInt(d, "quality", t.quality)
        t.format = PolymechFormJSON.readString(d, "format", t.format)
        t.fit = PolymechFormJSON.readString(d, "fit", t.fit)
        t.position = PolymechFormJSON.readString(d, "position", t.position)
        t.kernel = PolymechFormJSON.readString(d, "kernel", t.kernel)
        t.without_enlargement = PolymechFormJSON.readBool(d, "without_enlargement", t.without_enlargement)
        t.autorotate = PolymechFormJSON.readBool(d, "autorotate", t.autorotate)
        t.strip_metadata = PolymechFormJSON.readBool(d, "strip_metadata", t.strip_metadata)
        t.cache_enabled = PolymechFormJSON.readBool(d, "cache_enabled", t.cache_enabled)
        t.rotate = PolymechFormJSON.readInt(d, "rotate", t.rotate)
        t.constrain_proportions = PolymechFormJSON.readBool(d, "constrain_proportions", t.constrain_proportions)
        return t
    }

    func toJSON() -> String {
        var o: [String: Any] = [
            "output_mode": output_mode,
            "output_basename_infix": output_basename_infix,
            "max_width": max_width,
            "max_height": max_height,
            "quality": quality,
            "format": format,
            "fit": fit,
            "position": position,
            "kernel": kernel,
            "without_enlargement": without_enlargement,
            "autorotate": autorotate,
            "strip_metadata": strip_metadata,
            "cache_enabled": cache_enabled,
            "rotate": rotate,
            "constrain_proportions": constrain_proportions
        ]
        return PolymechFormJSON.encodeSorted(o)
    }

    /// Sibling filename infix, `[a-z0-9_]+`, lowercased; empty / invalid input falls back to `pm_resize`.
    func effectiveBasenameInfix() -> String { PolymechOutputInfix.sanitize(output_basename_infix, fallback: "pm_resize") }

    /// vips/Sharp: `0` = unconstrained on that axis. `fill` / `cover` / `outside` need both W and H.
    var isValidForEngineRun: Bool {
        if max_width <= 0 && max_height <= 0 { return false }
        switch fit {
        case "fill", "cover", "outside":
            return max_width > 0 && max_height > 0
        default:
            return max_width > 0 || max_height > 0
        }
    }
}

// MARK: - Meta (MetaOptions)

struct PolymechMetaFormData: Equatable {
    var provider: String
    var model: String
    var prompt: String
    var user_query: String
    var base_url: String
    var resize_first: Bool
    var resize_width: Int
    var out_md: Bool
    var out_json: Bool
    var update_exif: Bool
    var out_dir: String
    var dry_run: Bool

    static let defaults = PolymechMetaFormData(
        provider: "google",
        model: "gemini-3-pro-image-preview",
        prompt: "",
        user_query: "",
        base_url: "",
        resize_first: true,
        resize_width: 512,
        out_md: true,
        out_json: true,
        update_exif: false,
        out_dir: "",
        dry_run: false
    )

    static func from(json: String) -> PolymechMetaFormData {
        let d = PolymechFormJSON.parseObject(json)
        var t = PolymechMetaFormData.defaults
        t.provider = PolymechFormJSON.readString(d, "provider", t.provider)
        t.model = PolymechFormJSON.readString(d, "model", t.model)
        t.prompt = PolymechFormJSON.readString(d, "prompt", t.prompt)
        t.user_query = PolymechFormJSON.readString(d, "user_query", t.user_query)
        t.base_url = PolymechFormJSON.readString(d, "base_url", t.base_url)
        t.resize_first = PolymechFormJSON.readBool(d, "resize_first", t.resize_first)
        t.resize_width = PolymechFormJSON.readInt(d, "resize_width", t.resize_width)
        t.out_md = PolymechFormJSON.readBool(d, "out_md", t.out_md)
        t.out_json = PolymechFormJSON.readBool(d, "out_json", t.out_json)
        t.update_exif = PolymechFormJSON.readBool(d, "update_exif", t.update_exif)
        t.out_dir = PolymechFormJSON.readString(d, "out_dir", t.out_dir)
        t.dry_run = PolymechFormJSON.readBool(d, "dry_run", t.dry_run)
        return t
    }

    func toJSON() -> String {
        let o: [String: Any] = [
            "provider": provider,
            "model": model,
            "prompt": prompt,
            "user_query": user_query,
            "base_url": base_url,
            "resize_first": resize_first,
            "resize_width": resize_width,
            "out_md": out_md,
            "out_json": out_json,
            "update_exif": update_exif,
            "out_dir": out_dir,
            "dry_run": dry_run
        ]
        return PolymechFormJSON.encodeSorted(o)
    }

    /// One `base_url` field is shared with Gemini: switching to Replicate with a Google `generativelanguage` url
    /// made `pm_polymech_meta` hit the wrong host. Bare Gemini model ids (no `owner/`) are invalid as Replicate `version`.
    mutating func applyProviderSwitchToReplicate() {
        if !base_url.isEmpty && !base_url.contains("replicate.com") {
            base_url = ""
        }
        if !model.contains("/") || model == "gemini-3-pro-image-preview" {
            model = "google/gemini-2.5-flash"
        }
    }

    mutating func applyProviderSwitchFromReplicate() {
        if base_url.contains("replicate.com") { base_url = "" }
    }
}

// MARK: - Compress (CompressOptions)

struct PolymechCompressFormData: Equatable {
    var output_mode: String
    var output_basename_infix: String
    var compressor: String
    var strip_metadata: Bool
    var jpeg_quality: Int
    var jpeg_progressive: Bool
    var png_level: Int
    var png_quantize: Bool
    var png_zopfli: Bool
    var jpeg_trellis_quant: Bool
    var jpeg_overshoot_deringing: Bool

    static let defaults = PolymechCompressFormData(
        output_mode: "sibling",
        output_basename_infix: "pm_compress",
        compressor: "auto",
        strip_metadata: true,
        jpeg_quality: 85,
        jpeg_progressive: true,
        png_level: 9,
        png_quantize: false,
        png_zopfli: false,
        jpeg_trellis_quant: false,
        jpeg_overshoot_deringing: true
    )

    static func from(json: String) -> PolymechCompressFormData {
        let d = PolymechFormJSON.parseObject(json)
        var t = PolymechCompressFormData.defaults
        t.output_mode = PolymechFormJSON.readString(d, "output_mode", t.output_mode)
        if t.output_mode != "in_place" { t.output_mode = "sibling" }
        t.output_basename_infix = PolymechFormJSON.readString(d, "output_basename_infix", t.output_basename_infix)
        if t.output_basename_infix.isEmpty, let legacy = d["output_suffix"] as? String, !legacy.isEmpty {
            t.output_basename_infix = legacy
        }
        t.compressor = PolymechFormJSON.readString(d, "compressor", t.compressor)
        t.strip_metadata = PolymechFormJSON.readBool(d, "strip_metadata", t.strip_metadata)
        t.jpeg_quality = PolymechFormJSON.readInt(d, "jpeg_quality", t.jpeg_quality)
        if d["quality"] != nil { t.jpeg_quality = PolymechFormJSON.readInt(d, "quality", t.jpeg_quality) }
        t.jpeg_progressive = PolymechFormJSON.readBool(d, "jpeg_progressive", t.jpeg_progressive)
        if d["progressive"] != nil { t.jpeg_progressive = PolymechFormJSON.readBool(d, "progressive", t.jpeg_progressive) }
        t.png_level = PolymechFormJSON.readInt(d, "png_level", t.png_level)
        if d["level"] != nil { t.png_level = PolymechFormJSON.readInt(d, "level", t.png_level) }
        t.png_quantize = PolymechFormJSON.readBool(d, "png_quantize", t.png_quantize)
        t.png_zopfli = PolymechFormJSON.readBool(d, "png_zopfli", t.png_zopfli)
        t.jpeg_trellis_quant = PolymechFormJSON.readBool(d, "jpeg_trellis_quant", t.jpeg_trellis_quant)
        t.jpeg_overshoot_deringing = PolymechFormJSON.readBool(
            d,
            "jpeg_overshoot_deringing",
            t.jpeg_overshoot_deringing
        )
        return t
    }

    func toJSON() -> String {
        let o: [String: Any] = [
            "output_mode": output_mode,
            "output_basename_infix": output_basename_infix,
            "compressor": compressor,
            "strip_metadata": strip_metadata,
            "jpeg_quality": jpeg_quality,
            "jpeg_progressive": jpeg_progressive,
            "png_level": png_level,
            "png_quantize": png_quantize,
            "png_zopfli": png_zopfli,
            "jpeg_trellis_quant": jpeg_trellis_quant,
            "jpeg_overshoot_deringing": jpeg_overshoot_deringing
        ]
        return PolymechFormJSON.encodeSorted(o)
    }

    func effectiveBasenameInfix() -> String { PolymechOutputInfix.sanitize(output_basename_infix, fallback: "pm_compress") }
}

// MARK: - Transform (TransformOptions)

struct PolymechTransformFormData: Equatable {
    var provider: String
    var model: String
    var prompt: String
    var base_url: String
    var aspect_ratio: String
    var image_size: String
    var resize_first: Bool
    var resize_width: Int
    var preresize_raw_only: Bool

    static let defaults = PolymechTransformFormData(
        provider: "google",
        model: "gemini-3-pro-image-preview",
        prompt: "",
        base_url: "",
        aspect_ratio: "",
        image_size: "",
        resize_first: false,
        resize_width: 0,
        preresize_raw_only: false
    )

    static func from(json: String) -> PolymechTransformFormData {
        let d = PolymechFormJSON.parseObject(json)
        var t = PolymechTransformFormData.defaults
        t.provider = PolymechFormJSON.readString(d, "provider", t.provider)
        t.model = PolymechFormJSON.readString(d, "model", t.model)
        t.prompt = PolymechFormJSON.readString(d, "prompt", t.prompt)
        t.base_url = PolymechFormJSON.readString(d, "base_url", t.base_url)
        t.aspect_ratio = PolymechFormJSON.readString(d, "aspect_ratio", t.aspect_ratio)
        t.image_size = PolymechFormJSON.readString(d, "image_size", t.image_size)
        t.resize_first = PolymechFormJSON.readBool(d, "resize_first", t.resize_first)
        t.resize_width = PolymechFormJSON.readInt(d, "resize_width", t.resize_width)
        t.preresize_raw_only = PolymechFormJSON.readBool(d, "preresize_raw_only", t.preresize_raw_only)
        return t
    }

    func toJSON() -> String {
        let o: [String: Any] = [
            "provider": provider,
            "model": model,
            "prompt": prompt,
            "base_url": base_url,
            "aspect_ratio": aspect_ratio,
            "image_size": image_size,
            "resize_first": resize_first,
            "resize_width": resize_width,
            "preresize_raw_only": preresize_raw_only
        ]
        return PolymechFormJSON.encodeSorted(o)
    }
}

// MARK: - Find (FindOptions) — `meta` nested

struct PolymechFindFormData: Equatable {
    var mode: String
    var prompt: String
    var case_insensitive: Bool
    var match_folders: Bool
    var recursive: Bool
    var max_results: Int
    var dry_run: Bool
    var bypass_cache: Bool
    var generate: Bool
    var use_md: Bool
    var use_json: Bool
    var use_exif: Bool
    var find_semantic_judge: Bool
    var judge_prompt: String
    var meta: PolymechMetaFormData

    static let defaults = PolymechFindFormData(
        mode: "name",
        prompt: "",
        case_insensitive: true,
        match_folders: true,
        recursive: true,
        max_results: 0,
        dry_run: false,
        bypass_cache: false,
        generate: true,
        use_md: true,
        use_json: true,
        use_exif: true,
        find_semantic_judge: true,
        judge_prompt: "",
        meta: .defaults
    )

    static func from(json: String) -> PolymechFindFormData {
        let d = PolymechFormJSON.parseObject(json)
        var t = PolymechFindFormData.defaults
        t.mode = PolymechFormJSON.readString(d, "mode", t.mode)
        t.prompt = PolymechFormJSON.readString(d, "prompt", t.prompt)
        t.case_insensitive = PolymechFormJSON.readBool(d, "case_insensitive", t.case_insensitive)
        t.match_folders = PolymechFormJSON.readBool(d, "match_folders", t.match_folders)
        t.recursive = PolymechFormJSON.readBool(d, "recursive", t.recursive)
        t.max_results = PolymechFormJSON.readInt(d, "max_results", t.max_results)
        t.dry_run = PolymechFormJSON.readBool(d, "dry_run", t.dry_run)
        t.bypass_cache = PolymechFormJSON.readBool(d, "bypass_cache", t.bypass_cache)
        t.generate = PolymechFormJSON.readBool(d, "generate", t.generate)
        t.use_md = PolymechFormJSON.readBool(d, "use_md", t.use_md)
        t.use_json = PolymechFormJSON.readBool(d, "use_json", t.use_json)
        t.use_exif = PolymechFormJSON.readBool(d, "use_exif", t.use_exif)
        t.find_semantic_judge = PolymechFormJSON.readBool(
            d,
            "find_semantic_judge",
            t.find_semantic_judge
        )
        t.judge_prompt = PolymechFormJSON.readString(d, "judge_prompt", t.judge_prompt)
        if let metaObj = d["meta"] as? [String: Any] {
            let s = PolymechFormJSON.encodeSorted(metaObj)
            t.meta = PolymechMetaFormData.from(json: s)
        } else {
            t.meta = PolymechMetaFormData.defaults
        }
        return t
    }

    func toJSON() -> String {
        var o: [String: Any] = [
            "mode": mode,
            "prompt": prompt,
            "case_insensitive": case_insensitive,
            "match_folders": match_folders,
            "recursive": recursive,
            "max_results": max_results,
            "dry_run": dry_run,
            "bypass_cache": bypass_cache,
            "generate": generate,
            "use_md": use_md,
            "use_json": use_json,
            "use_exif": use_exif,
            "find_semantic_judge": find_semantic_judge,
            "judge_prompt": judge_prompt,
            "meta": PolymechFormJSON.parseObject(meta.toJSON())
        ]
        return PolymechFormJSON.encodeSorted(o)
    }
}
