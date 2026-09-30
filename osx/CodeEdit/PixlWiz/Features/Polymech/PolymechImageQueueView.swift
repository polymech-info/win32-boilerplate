import Foundation
import SwiftUI

struct PolymechImageQueueView: View {
    @EnvironmentObject private var workspace: WorkspaceDocument
    @EnvironmentObject private var jobs: PolymechImageJobCenter
    @State private var detailSelection: PolymechQueueDetailSelection?
    @State private var selectedJobIDs: Set<UUID> = []

    private var selectedCancellableCount: Int {
        jobs.entries.filter { selectedJobIDs.contains($0.id) && ($0.isRunning || $0.status == .queued) }
            .count
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text(verbatim: NSLocalizedString("Polymech.brand", comment: "Polymech panel title"))
                    .font(.headline)
                Spacer()
                Button(NSLocalizedString("Polymech.queue.stopSelected", comment: "Polymech job queue")) {
                    jobs.cancelIfRunning(in: selectedJobIDs)
                }
                .disabled(selectedCancellableCount == 0)
                .help(NSLocalizedString("Polymech.queue.stopSelectedHelp", comment: "Polymech job queue"))
                Button(NSLocalizedString("Polymech.queue.removeSelected", comment: "Polymech job queue")) {
                    let toRemove = selectedJobIDs
                    if let d = detailSelection, toRemove.contains(d.id) { detailSelection = nil }
                    jobs.removeEntries(ids: toRemove)
                    selectedJobIDs = []
                }
                .disabled(selectedJobIDs.isEmpty)
                .help(NSLocalizedString("Polymech.queue.removeSelectedHelp", comment: "Polymech job queue"))
                Button(NSLocalizedString("Polymech.queue.clear", comment: "Polymech job queue")) {
                    detailSelection = nil
                    selectedJobIDs = []
                    jobs.clear()
                }
                .disabled(jobs.entries.isEmpty)
            }
            .padding(8)
            if jobs.entries.isEmpty {
                CEContentUnavailableView(
                    NSLocalizedString("Polymech.queue.emptyTitle", comment: "Polymech job queue"),
                    description: NSLocalizedString("Polymech.queue.emptyDescription", comment: "Polymech job queue"),
                    systemImage: "tray"
                )
            } else {
                Table(jobs.entries, selection: $selectedJobIDs) {
                    TableColumn(NSLocalizedString("Polymech.queue.column.name", comment: "Polymech job queue")) { (e: PolymechImageJobCenter.LogEntry) in
                        HStack(alignment: .center, spacing: 6) {
                            if e.isRunning {
                                ProgressView()
                                    .controlSize(.small)
                            } else if e.status == .queued {
                                Image(systemName: "clock")
                                    .font(.caption)
                                    .foregroundStyle(.tertiary)
                            }
                            Text(e.title)
                                .lineLimit(2)
                        }
                    }
                    .width(min: 120, ideal: 220, max: 400)

                    TableColumn(NSLocalizedString("Polymech.queue.column.status", comment: "Polymech job queue")) { (e: PolymechImageJobCenter.LogEntry) in
                        Text(PolymechImageJobCenter.localizedStatus(e.status))
                    }
                    .width(min: 64, ideal: 88, max: 120)

                    TableColumn(NSLocalizedString("Polymech.queue.column.source", comment: "Polymech job queue")) { (e: PolymechImageJobCenter.LogEntry) in
                        pathRevealButton(path: e.sourcePath)
                    }
                    .width(min: 80, ideal: 200)

                    TableColumn(NSLocalizedString("Polymech.queue.column.result", comment: "Polymech job queue")) { (e: PolymechImageJobCenter.LogEntry) in
                        pathRevealButton(path: e.resultPath)
                    }
                    .width(min: 80, ideal: 200)

                    TableColumn(NSLocalizedString("Polymech.queue.column.stop", comment: "Polymech job queue")) { (e: PolymechImageJobCenter.LogEntry) in
                        if e.isRunning || e.status == .queued {
                            Button {
                                jobs.cancel(id: e.id)
                            } label: {
                                Text(NSLocalizedString("Polymech.queue.stop", comment: "Polymech job queue"))
                            }
                            .buttonStyle(.borderless)
                            .help(NSLocalizedString("Polymech.queue.stopHelp", comment: "Polymech job queue"))
                        }
                    }
                    .width(64)

                    TableColumn(NSLocalizedString("Polymech.queue.column.detail", comment: "Polymech job queue")) { (e: PolymechImageJobCenter.LogEntry) in
                        Button {
                            detailSelection = PolymechQueueDetailSelection(id: e.id)
                        } label: {
                            Text(NSLocalizedString("Polymech.queue.detail", comment: "Polymech job queue"))
                        }
                        .buttonStyle(.borderless)
                        .buttonBorderShape(.roundedRectangle)
                        .help(
                            String(
                                format: NSLocalizedString("Polymech.queue.detailHelp", comment: "Polymech job queue"),
                                e.title
                            )
                        )
                    }
                    .width(72)
                }
            }
        }
        .sheet(item: $detailSelection) { selection in
            PolymechQueueJobDetailSheet(jobId: selection.id)
                .environmentObject(jobs)
                .environmentObject(workspace)
        }
        .onChange(of: jobs.entries.map(\.id)) { _, ids in
            let set = Set(ids)
            selectedJobIDs = selectedJobIDs.filter { set.contains($0) }
        }
    }

    private func revealInNavigator(path: String) {
        let p = URL(fileURLWithPath: path).standardizedFileURL.path
        NotificationCenter.default.post(
            name: .polymechRevealFileInNavigator,
            object: workspace,
            userInfo: ["path": p]
        )
    }

    @ViewBuilder
    private func pathRevealButton(path: String?) -> some View {
        if let p = path, !p.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            Button {
                revealInNavigator(path: p)
            } label: {
                Text(p)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .font(.caption)
            }
            .buttonStyle(.plain)
            .help(NSLocalizedString("Polymech.queue.revealInNavigator", comment: "Polymech job queue"))
        } else {
            Text(NSLocalizedString("Polymech.queue.pathNone", comment: "Polymech job queue"))
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
    }
}

// MARK: - Detail

private struct PolymechQueueDetailSelection: Identifiable, Equatable {
    let id: UUID
}

private struct PolymechQueueJobDetailSheet: View {
    let jobId: UUID

    @EnvironmentObject private var workspace: WorkspaceDocument
    @EnvironmentObject private var jobs: PolymechImageJobCenter
    @Environment(\.dismiss) private var dismiss

    private var entry: PolymechImageJobCenter.LogEntry? {
        jobs.entries.first { $0.id == jobId }
    }

    var body: some View {
        NavigationStack {
            Group {
                if let entry {
                    detailContent(entry)
                } else {
                    Text(NSLocalizedString("Polymech.queue.detail.removed", comment: "Polymech job queue"))
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
                }
            }
            .navigationTitle(NSLocalizedString("Polymech.queue.detailTitle", comment: "Polymech job queue"))
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(NSLocalizedString("Polymech.queue.detailClose", comment: "Polymech job queue")) {
                        dismiss()
                    }
                }
            }
        }
        .frame(minWidth: 480, minHeight: 360)
    }

    @ViewBuilder
    private func detailContent(_ entry: PolymechImageJobCenter.LogEntry) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text(entry.date.formatted(date: .abbreviated, time: .shortened))
                    .font(.caption)
                    .foregroundStyle(.tertiary)
                HStack(alignment: .top, spacing: 8) {
                    Text(entry.title)
                        .font(.headline)
                    Spacer(minLength: 0)
                    Text(PolymechImageJobCenter.localizedStatus(entry.status))
                        .font(.subheadline.weight(.semibold))
                        .padding(.horizontal, 8)
                        .padding(.vertical, 2)
                        .background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 4, style: .continuous))
                }
                let sNorm = (entry.sourcePath.map { URL(fileURLWithPath: $0).standardizedFileURL.path } ?? "")
                let rNorm = (entry.resultPath.map { URL(fileURLWithPath: $0).standardizedFileURL.path } ?? "")
                if !sNorm.isEmpty {
                    detailPathBlock(
                        label: NSLocalizedString("Polymech.queue.detail.sourceLabel", comment: "Polymech job queue"),
                        path: sNorm
                    )
                }
                if !rNorm.isEmpty, rNorm != sNorm, let r = entry.resultPath, !r.isEmpty {
                    detailPathBlock(
                        label: NSLocalizedString("Polymech.queue.detail.resultLabel", comment: "Polymech job queue"),
                        path: r
                    )
                }
                if entry.isRunning {
                    Text(NSLocalizedString("Polymech.queue.detail.runningHint", comment: "Polymech job queue"))
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                Text(NSLocalizedString("Polymech.queue.detail.outputLabel", comment: "Polymech job queue"))
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                let bodyText = entry.fullOutput.isEmpty && !entry.isRunning
                    ? NSLocalizedString("Polymech.queue.detail.noOutput", comment: "Polymech job queue")
                    : entry.fullOutput
                Text(bodyText)
                    .font(.system(.caption, design: .monospaced))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(16)
        }
    }

    @ViewBuilder
    private func detailPathBlock(label: String, path: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text(label)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                Spacer(minLength: 0)
                Button(NSLocalizedString("Polymech.queue.detail.revealButton", comment: "Polymech job queue")) {
                    let p = URL(fileURLWithPath: path).standardizedFileURL.path
                    NotificationCenter.default.post(
                        name: .polymechRevealFileInNavigator,
                        object: workspace,
                        userInfo: ["path": p]
                    )
                }
                .buttonStyle(.link)
            }
            Text(path)
                .font(.system(.body, design: .monospaced))
                .textSelection(.enabled)
        }
    }
}
