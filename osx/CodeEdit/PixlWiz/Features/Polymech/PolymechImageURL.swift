import Foundation
import ImageIO
import UniformTypeIdentifiers

enum PolymechImageURL {
    private static let imageExtensions: Set<String> = [
        "jpg", "jpeg", "png", "gif", "webp", "tiff", "tif", "heic", "heif", "bmp", "icns", "avif", "jxl"
    ]

    static func isLikelyImageFile(_ url: URL) -> Bool {
        if let type = try? url.resourceValues(forKeys: [.contentTypeKey]).contentType,
           type.conforms(to: .image) {
            return true
        }
        return imageExtensions.contains(url.pathExtension.lowercased())
    }

    /// Image header only (no full decode) for inspector “Image size” UI.
    static func pixelSize(of url: URL) -> (width: Int, height: Int)? {
        // `nil` options is fine; we only read metadata for one file at a time in the inspector.
        guard let src = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
        guard let props = CGImageSourceCopyPropertiesAtIndex(src, 0, nil) as? [String: Any] else { return nil }
        guard let w = props[kCGImagePropertyPixelWidth as String] as? NSNumber,
              let h = props[kCGImagePropertyPixelHeight as String] as? NSNumber else { return nil }
        let wi = w.intValue
        let hi = h.intValue
        guard wi > 0, hi > 0 else { return nil }
        return (wi, hi)
    }

    /// Deterministic sibling path for a Polymech output (e.g. `photo_pm_resize.png`).
    static func siblingOutput(for input: URL, tag: String) -> URL {
        siblingOutput(for: input, basenameInfix: "pm_\(tag)")
    }

    /// Sibling in same folder: `base_<basenameInfix>.ext` (e.g. infix `pm_resize` → `shot_pm_resize.jpg`).
    static func siblingOutput(for input: URL, basenameInfix: String) -> URL {
        let dir = input.deletingLastPathComponent()
        let base = input.deletingPathExtension().lastPathComponent
        let ext = input.pathExtension.isEmpty ? "dat" : input.pathExtension
        return dir.appendingPathComponent("\(base)_\(basenameInfix).\(ext)")
    }

    /// `in_place` → same as `input` (C++ rewrites via temp+replace). Otherwise sibling with the given infix.
    static func imageCommandOutput(
        input: URL,
        outputMode: String,
        basenameInfix: String
    ) -> URL {
        if outputMode == "in_place" {
            return input.standardizedFileURL
        }
        return siblingOutput(for: input, basenameInfix: basenameInfix)
    }
}
