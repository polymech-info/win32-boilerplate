import SwiftUI

struct PolymechLogView: View {
    @ObservedObject private var sink = PolymechLogSink.shared
    @EnvironmentObject private var utilityAreaViewModel: UtilityAreaViewModel

    @State private var selectedLevel: PolymechLogLevel?  // nil = all
    @State private var selectedChannel: String = ""       // "" = all
    @State private var filterText: String = ""
    @State private var selection: Set<UUID> = []

    // MARK: - Derived

    private var filtered: [PolymechLogEntry] {
        sink.entries.filter { e in
            if let lv = selectedLevel, e.level != lv { return false }
            if !selectedChannel.isEmpty, e.channel != selectedChannel { return false }
            if !filterText.isEmpty,
               !e.message.localizedCaseInsensitiveContains(filterText),
               !e.channel.localizedCaseInsensitiveContains(filterText) { return false }
            return true
        }
    }

    private var selectedEntry: PolymechLogEntry? {
        guard selection.count == 1, let id = selection.first else { return nil }
        return sink.entries.first { $0.id == id }
    }

    // MARK: - Body

    var body: some View {
        UtilityAreaTabView(model: utilityAreaViewModel.tabViewModel) { _ in
            VStack(spacing: 0) {
                Table(filtered, selection: $selection) {
                    TableColumn("Time") { e in
                        Text(e.rawTime)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(.primary)
                    }
                    .width(ideal: 88, max: 110)

                    TableColumn("Level") { e in
                        LevelBadgeView(level: e.level)
                    }
                    .width(ideal: 62, max: 80)

                    TableColumn("Channel") { e in
                        Text(e.channel)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(.secondary)
                    }
                    .width(ideal: 90, max: 160)

                    TableColumn("Message") { e in
                        Text(e.message)
                            .font(.system(size: 11, design: .monospaced))
                            .lineLimit(1)
                    }
                }

                if let entry = selectedEntry {
                    Divider()
                    ScrollView(.vertical) {
                        Text(entry.rawLine)
                            .font(.system(size: 11, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(8)
                            .textSelection(.enabled)
                    }
                    .frame(height: 72)
                    .background(.background)
                }
            }
            .paneToolbar {
                levelPicker
                channelPicker
                Spacer()
                UtilityAreaFilterTextField(title: "Filter", text: $filterText)
                    .frame(maxWidth: 175)
                Button { sink.clear() } label: {
                    Image(systemName: "trash")
                }
                .help("Clear log")
                .disabled(sink.entries.isEmpty)
            }
        }
    }

    // MARK: - Toolbar helpers

    @ViewBuilder
    private var levelPicker: some View {
        Picker(
            selection: Binding(
                get: { selectedLevel?.rawValue ?? "" },
                set: { selectedLevel = $0.isEmpty ? nil : PolymechLogLevel(spdlog: $0) }
            )
        ) {
            Text("All Levels").tag("")
            Divider()
            ForEach(PolymechLogLevel.allCases) { lv in
                Text(lv.rawValue.capitalized).tag(lv.rawValue)
            }
        } label: { EmptyView() }
        .fixedSize()
        .help("Filter by level")
    }

    @ViewBuilder
    private var channelPicker: some View {
        Picker(selection: $selectedChannel) {
            Text("All Channels").tag("")
            if !sink.knownChannels.isEmpty {
                Divider()
                ForEach(sink.knownChannels, id: \.self) { ch in
                    Text(ch).tag(ch)
                }
            }
        } label: { EmptyView() }
        .fixedSize()
        .help("Filter by channel")
        .disabled(sink.knownChannels.isEmpty)
    }
}

// MARK: - Level badge

private struct LevelBadgeView: View {
    let level: PolymechLogLevel

    var body: some View {
        Text(level.rawValue)
            .font(.system(size: 9, weight: .semibold).monospaced())
            .padding(.horizontal, 4)
            .padding(.vertical, 2)
            .background(
                RoundedRectangle(cornerRadius: 3)
                    .fill(level.color.opacity(0.18))
            )
            .foregroundStyle(level.color)
    }
}
