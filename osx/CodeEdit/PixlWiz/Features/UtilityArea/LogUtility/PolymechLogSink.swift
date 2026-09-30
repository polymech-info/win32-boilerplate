import Foundation

// C bridge — declared in pm_polymech_bridge.h
@_silgen_name("pm_polymech_set_log_callback")
private func _pmSetLogCallback(
    _ callback: @convention(c) (UnsafePointer<CChar>?, UnsafeMutableRawPointer?) -> Void,
    _ context: UnsafeMutableRawPointer?
)

// Non-capturing @convention(c) trampoline — safe to pass to C.
private let _bridgeCB: @convention(c) (UnsafePointer<CChar>?, UnsafeMutableRawPointer?) -> Void = { line, _ in
    guard let line else { return }
    let s = String(cString: line)
    PolymechLogSink.shared.receiveLine(s)
}

// MARK: - Sink

/// Singleton that receives every spdlog line from the C++ bridge and publishes
/// parsed `PolymechLogEntry` values to the log panel.
final class PolymechLogSink: ObservableObject {
    static let shared = PolymechLogSink()

    private static let maxEntries = 12_000
    private static let trimTo     =  8_000

    @Published private(set) var entries: [PolymechLogEntry] = []
    @Published private(set) var knownChannels: [String] = []

    private var channelSet: Set<String> = []

    private init() {}

    // MARK: Public

    /// Call once after `PolymechImageEngine.bootstrapNativeLoggingAtLaunch()`.
    func install() {
        _pmSetLogCallback(_bridgeCB, nil)
    }

    func clear() {
        entries.removeAll(keepingCapacity: true)
    }

    // MARK: Internal

    fileprivate func receiveLine(_ raw: String) {
        let entry = PolymechLogEntry.parse(raw)
        DispatchQueue.main.async { [weak self] in
            self?.append(entry)
        }
    }

    private func append(_ entry: PolymechLogEntry) {
        if !channelSet.contains(entry.channel) {
            channelSet.insert(entry.channel)
            knownChannels = channelSet.sorted()
        }
        entries.append(entry)
        if entries.count > Self.maxEntries {
            entries.removeFirst(entries.count - Self.trimTo)
        }
    }
}
