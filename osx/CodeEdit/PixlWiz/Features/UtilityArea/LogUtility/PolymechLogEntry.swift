import Foundation
import SwiftUI

// MARK: - Level

enum PolymechLogLevel: String, CaseIterable, Identifiable {
    case trace, debug, info, warn, error, critical

    var id: String { rawValue }

    init?(spdlog raw: String) {
        switch raw.lowercased() {
        case "trace":             self = .trace
        case "debug":             self = .debug
        case "info":              self = .info
        case "warn", "warning":   self = .warn
        case "error", "err":      self = .error
        case "critical":          self = .critical
        default:                  return nil
        }
    }

    var color: Color {
        switch self {
        case .trace:    return .secondary
        case .debug:    return Color(nsColor: .systemGray)
        case .info:     return .cyan
        case .warn:     return Color(red: 1.0, green: 186.0/255.0, blue: 0)
        case .error:    return Color(red: 202.0/255.0, green: 27.0/255.0, blue: 0)
        case .critical: return .purple
        }
    }
}

// MARK: - Entry

struct PolymechLogEntry: Identifiable {
    let id: UUID
    let rawTime: String
    let level: PolymechLogLevel
    let channel: String
    let message: String
    let rawLine: String

    // MARK: Parse

    /// Parses a spdlog-formatted line produced by `pm::log::prefixed`:
    ///   `[HH:MM:SS.mmm] [level] [channel] message body`
    static func parse(_ raw: String) -> PolymechLogEntry {
        var rest = raw[raw.startIndex...]
        var rawTime = ""
        var level = PolymechLogLevel.info
        var channel = ""

        // [time]
        if let (tok, tail) = extractBracket(rest) {
            rawTime = tok
            rest = tail
        }

        // [level]
        if let (tok, tail) = extractBracket(rest) {
            level = PolymechLogLevel(spdlog: tok) ?? .info
            rest = tail
        }

        // [channel] — added by pm::log::prefixed
        if let (tok, tail) = extractBracket(rest) {
            channel = tok
            rest = tail
        }

        let message = rest.trimmingCharacters(in: .whitespaces)

        return PolymechLogEntry(
            id: UUID(),
            rawTime: rawTime,
            level: level,
            channel: channel.isEmpty ? "log" : channel,
            message: message,
            rawLine: raw
        )
    }

    // Returns (token-inside-brackets, remainder-after-close-bracket-trimmed)
    private static func extractBracket(_ s: Substring) -> (String, Substring)? {
        guard s.first == "[", let end = s.firstIndex(of: "]") else { return nil }
        let token = String(s[s.index(after: s.startIndex)..<end])
        let tail = s[s.index(after: end)...].drop(while: { $0 == " " })
        return (token, tail)
    }
}
