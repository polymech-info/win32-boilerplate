import Foundation
import SwiftUI

/// Buffers recent Polymech CLI-style operations for the utility-area queue view.
@MainActor
final class PolymechImageJobCenter: ObservableObject {
    static let shared = PolymechImageJobCenter()

    enum JobStatus: String, Equatable, Sendable, Hashable {
        case running
        case queued
        case succeeded
        case failed
    }

    struct LogEntry: Identifiable, Hashable {
        let id: UUID
        let title: String
        var status: JobStatus
        /// Full job output (or error) for the detail sheet; may be empty while `status == .running` or for quiet tools.
        var fullOutput: String
        /// Input path or summary (e.g. find roots) when the job operates on the filesystem.
        let sourcePath: String?
        /// Output file path when the job produces a new file, or the same as ``sourcePath`` for in-place writes.
        let resultPath: String?
        let date: Date

        var isRunning: Bool { status == .running }
    }

    @Published private(set) var entries: [LogEntry] = []

    /// One step in ``runSequential(_:)`` (tuple cannot carry `@escaping` work closure).
    struct SequentialWorkItem: Sendable {
        let title: String
        let sourcePath: String?
        let resultPath: String?
        let work: @Sendable () throws -> String
    }

    private let maxEntries = 80
    private static let maxOutputChars = 120_000
    /// Single-job runs: one task per row id.
    private var inFlight: [UUID: Task<Void, Never>] = [:]
    /// A chained batch (e.g. multi-file **Transform**): one shared coordinator task, many row ids in order.
    private var batchTaskById: [UUID: Task<Void, Never>] = [:]
    private var batchMembers: [UUID: [UUID]] = [:] // batch id –> ordered job ids
    private var jobToBatch: [UUID: UUID] = [:] // job id –> batch id

    private init() {}

    static func localizedStatus(_ status: JobStatus) -> String {
        switch status {
        case .running:
            return NSLocalizedString("Polymech.queue.status.running", comment: "Polymech job queue")
        case .queued:
            return NSLocalizedString("Polymech.queue.status.queued", comment: "Polymech job queue")
        case .succeeded:
            return NSLocalizedString("Polymech.queue.status.succeeded", comment: "Polymech job queue")
        case .failed:
            return NSLocalizedString("Polymech.queue.status.failed", comment: "Polymech job queue")
        }
    }

    /// Runs work off the main thread; updates the log on completion or failure.
    /// - Parameters:
    ///   - sourcePath: Input path (or a short label such as a multi-root find) for the *Source* column and reveal.
    ///   - resultPath: Output path when the job creates a new file, or the same as `sourcePath` for in-place writes. Nil when there is no file to reveal.
    @discardableResult
    func run(
        title: String,
        sourcePath: String? = nil,
        resultPath: String? = nil,
        work: @escaping @Sendable () throws -> String,
        onComplete: (@MainActor () -> Void)? = nil
    ) -> UUID {
        let jobId = UUID()
        let entry = LogEntry(
            id: jobId,
            title: title,
            status: .running,
            fullOutput: "",
            sourcePath: sourcePath,
            resultPath: resultPath,
            date: Date()
        )
        entries.insert(entry, at: 0)
        if entries.count > maxEntries {
            entries.removeLast(entries.count - maxEntries)
        }
        let t = Task { @MainActor [weak self] in
            guard let self else { return }
            defer { self.inFlight.removeValue(forKey: jobId) }
            if Task.isCancelled {
                onComplete?()
                return
            }
            do {
                let out = try await Task.detached(priority: .userInitiated) {
                    try work()
                }.value
                self.handleCompletion(id: jobId, output: out, ok: true)
            } catch {
                let errText = Self.clip(error.localizedDescription, max: Self.maxOutputChars)
                self.handleCompletion(id: jobId, output: errText, ok: false)
            }
            onComplete?()
        }
        inFlight[jobId] = t
        return jobId
    }

    /// Enqueue several jobs **strictly in order** with **all rows visible at once** (first `running`, rest `queued` until their turn).
    func runSequential(_ items: [SequentialWorkItem]) {
        guard !items.isEmpty else { return }
        let batchId = UUID()
        let jobIds: [UUID] = (0..<items.count).map { _ in UUID() }
        for j in jobIds { jobToBatch[j] = batchId }
        batchMembers[batchId] = jobIds

        var newEntries: [LogEntry] = []
        for (i, it) in items.enumerated() {
            newEntries.append(
                LogEntry(
                    id: jobIds[i],
                    title: it.title,
                    status: i == 0 ? .running : .queued,
                    fullOutput: "",
                    sourcePath: it.sourcePath,
                    resultPath: it.resultPath,
                    date: Date()
                )
            )
        }
        entries.insert(contentsOf: newEntries, at: 0)
        if entries.count > maxEntries { entries.removeLast(entries.count - maxEntries) }

        let t = Task { @MainActor [weak self] in
            guard let self else { return }
            defer { self.tearDownBatch(id: batchId) }
            for (index, it) in items.enumerated() {
                let id = jobIds[index]
                if Task.isCancelled {
                    self.markBatchIncompleteCancelled(jobIds: jobIds, fromIndex: index)
                    return
                }
                if index > 0 {
                    if let idx = self.entries.firstIndex(where: { $0.id == id }) {
                        var e = self.entries[idx]
                        e.status = .running
                        self.entries[idx] = e
                    }
                }
                do {
                    let out = try await Task.detached(priority: .userInitiated) {
                        try it.work()
                    }.value
                    self.handleCompletion(id: id, output: out, ok: true)
                } catch {
                    let errText = Self.clip(error.localizedDescription, max: Self.maxOutputChars)
                    self.handleCompletion(id: id, output: errText, ok: false)
                }
            }
        }
        batchTaskById[batchId] = t
    }

    private func tearDownBatch(id batchId: UUID) {
        batchTaskById.removeValue(forKey: batchId)
        if let jids = batchMembers.removeValue(forKey: batchId) {
            for j in jids { jobToBatch.removeValue(forKey: j) }
        }
    }

    /// Marks rows from `fromIndex` on as cancelled (batch `Task` was cancelled or removed).
    private func markBatchIncompleteCancelled(jobIds: [UUID], fromIndex: Int) {
        let msg = NSLocalizedString("Polymech.queue.cancelled", comment: "Polymech job queue")
        for i in fromIndex..<jobIds.count {
            let jid = jobIds[i]
            guard let idx = entries.firstIndex(where: { $0.id == jid }) else { continue }
            var e = entries[idx]
            if e.status == .running || e.status == .queued {
                e.status = .failed
                e.fullOutput = msg
                entries[idx] = e
            }
        }
    }

    /// Stops a **single** in-flight job, or the **entire** chained transform batch that contains `id` (cancels all rows still running or queued in that batch).
    func cancel(id: UUID) {
        if let batch = jobToBatch[id] {
            if let t = batchTaskById[batch] {
                t.cancel()
            }
            batchTaskById.removeValue(forKey: batch)
            let jids = batchMembers.removeValue(forKey: batch) ?? []
            for j in jids { jobToBatch.removeValue(forKey: j) }
            let msg = NSLocalizedString("Polymech.queue.cancelled", comment: "Polymech job queue")
            for jid in jids {
                guard let idx = entries.firstIndex(where: { $0.id == jid }) else { continue }
                var e = entries[idx]
                if e.status == .running || e.status == .queued {
                    e.status = .failed
                    e.fullOutput = msg
                    entries[idx] = e
                }
            }
            return
        }
        inFlight[id]?.cancel()
        inFlight.removeValue(forKey: id)
        if let idx = entries.firstIndex(where: { $0.id == id && $0.isRunning }) {
            var e = entries[idx]
            e.status = .failed
            e.fullOutput = NSLocalizedString("Polymech.queue.cancelled", comment: "Polymech job queue")
            entries[idx] = e
        }
    }

    /// Stops all selected work; a chained **batch** is cancelled only once (first selected id in that batch).
    func cancelIfRunning(in ids: Set<UUID>) {
        var seenBatch: Set<UUID> = []
        for id in ids {
            if let b = jobToBatch[id] {
                if !seenBatch.insert(b).inserted { continue }
            }
            cancel(id: id)
        }
    }

    /// Removes rows; cancels a single in-flight or **entire** batch if any member is removed, then drops selected rows.
    func removeEntries(ids: Set<UUID>) {
        guard !ids.isEmpty else { return }
        for b in Set(ids.compactMap { jobToBatch[$0] }) {
            if let t = batchTaskById.removeValue(forKey: b) { t.cancel() }
            if let jids = batchMembers.removeValue(forKey: b) {
                for j in jids { jobToBatch.removeValue(forKey: j) }
                let msg = NSLocalizedString("Polymech.queue.cancelled", comment: "Polymech job queue")
                for jid in jids {
                    guard let idx = entries.firstIndex(where: { $0.id == jid }) else { continue }
                    var e = entries[idx]
                    if e.status == .running || e.status == .queued {
                        e.status = .failed
                        e.fullOutput = msg
                        entries[idx] = e
                    }
                }
            }
        }
        for id in ids {
            inFlight[id]?.cancel()
            inFlight.removeValue(forKey: id)
        }
        entries.removeAll { ids.contains($0.id) }
    }

    func clear() {
        for t in inFlight.values { t.cancel() }
        inFlight.removeAll()
        for t in batchTaskById.values { t.cancel() }
        batchTaskById.removeAll()
        batchMembers.removeAll()
        jobToBatch.removeAll()
        entries.removeAll()
    }

    private func handleCompletion(id: UUID, output: String, ok: Bool) {
        if let idx = entries.firstIndex(where: { $0.id == id }), !entries[idx].isRunning { return }
        let clipped = Self.clip(output, max: Self.maxOutputChars)
        finish(id: id, output: clipped, ok: ok)
    }

    private func finish(id: UUID, output: String, ok: Bool) {
        guard let idx = entries.firstIndex(where: { $0.id == id }) else { return }
        guard entries[idx].isRunning else { return }
        var e = entries[idx]
        e.status = ok ? .succeeded : .failed
        e.fullOutput = ok ? output : "Error: \(output)"
        entries[idx] = e
    }

    private static func clip(_ s: String, max: Int) -> String {
        if s.count <= max { return s }
        return String(s.prefix(max)) + "…"
    }
}
