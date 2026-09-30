import SwiftUI
import Foundation

enum PolymechToolbarStyle: Equatable {
    case inline
    case titleBar
}

struct PolymechImageToolbarView: View {
    var style: PolymechToolbarStyle = .inline

    @EnvironmentObject private var polymechJobs: PolymechImageJobCenter
    @EnvironmentObject private var workflow: PolymechWorkflowState

    var body: some View {
        Group {
            if style == .titleBar {
                titleBarContent
            } else {
                inlineContent
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(NSLocalizedString("Polymech.a11y.imageTools", comment: "Polymech toolbar"))
        .help(NSLocalizedString("Polymech.toolbar.help.default", comment: "Polymech toolbar"))
    }

    // MARK: - Title bar (command → inspector → run)

    private var titleBarContent: some View {
        HStack(alignment: .center, spacing: 8) {
            Text(NSLocalizedString("Polymech.brand", comment: "Polymech toolbar"))
                .font(.system(size: 13, weight: .bold, design: .rounded))
                .foregroundStyle(
                    LinearGradient(
                        colors: [
                            Color(red: 0.35, green: 0.55, blue: 0.95),
                            Color(red: 0.65, green: 0.4, blue: 0.95)
                        ],
                        startPoint: .leading,
                        endPoint: .trailing
                    )
                )
            HStack(spacing: 5) {
                ForEach(PolymechCommand.allCases) { cmd in
                    commandChip(cmd)
                }
            }
            runButton
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 3)
        .background {
            RoundedRectangle(cornerRadius: 7, style: .continuous)
                .fill(.quaternary.opacity(0.35))
        }
        .overlay {
            RoundedRectangle(cornerRadius: 7, style: .continuous)
                .strokeBorder(.tertiary.opacity(0.45), lineWidth: 0.5)
        }
        .help(
            String(
                format: NSLocalizedString("Polymech.toolbar.help.titleBar", comment: "Polymech toolbar"),
                workflow.selectionDescription
            )
        )
    }

    private func commandChip(_ cmd: PolymechCommand) -> some View {
        let selected = workflow.command == cmd
        return Button {
            if workflow.command == cmd {
                workflow.clearCommand()
            } else {
                workflow.setCommand(cmd)
            }
        } label: {
            Text(cmd.displayName)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
        }
        .buttonStyle(.borderless)
        .padding(.horizontal, 7)
        .padding(.vertical, 3)
        .background {
            RoundedRectangle(cornerRadius: 5, style: .continuous)
                .fill(tintForCommand(cmd).opacity(selected ? 0.35 : 0.1))
        }
        .overlay {
            RoundedRectangle(cornerRadius: 5, style: .continuous)
                .strokeBorder(tintForCommand(cmd).opacity(selected ? 0.7 : 0.15), lineWidth: selected ? 1 : 0.5)
        }
        .foregroundStyle(selected ? .primary : .secondary)
        .help(
            String(
                format: NSLocalizedString("Polymech.toolbar.chipHelp", comment: "Polymech toolbar"),
                cmd.displayName
            )
        )
    }

    private var runButton: some View {
        let pending = workflow.command
        let ok = pending.map { workflow.canRun(pendingCommand: $0) } ?? false
        return Button {
            if let c = pending, ok {
                workflow.run(jobs: polymechJobs)
            }
        } label: {
            HStack(spacing: 3) {
                Image(systemName: "play.fill")
                Text(NSLocalizedString("Polymech.run", comment: "Polymech toolbar"))
            }
            .font(.system(size: 12, weight: .bold, design: .rounded))
        }
        .buttonStyle(.borderedProminent)
        .tint(.green)
        .controlSize(.small)
        .disabled(pending == nil || !ok)
        .opacity((pending != nil && ok) ? 1.0 : 0.5)
        .accessibilityLabel(NSLocalizedString("Polymech.a11y.run", comment: "Polymech toolbar"))
    }

    private func tintForCommand(_ cmd: PolymechCommand) -> Color {
        switch cmd {
        case .resize: .blue
        case .meta: .purple
        case .compress: .orange
        case .find: .green
        case .transform: .pink
        case .chat: .teal
        }
    }

    // MARK: - Inline (compact; if used, still workflow-driven + Run)

    private var inlineContent: some View {
        HStack(spacing: 4) {
            Group {
                if let c = workflow.command {
                    Text(
                        String(
                            format: NSLocalizedString("Polymech.inline.withCommand", comment: "Polymech toolbar"),
                            c.displayName
                        )
                    )
                } else {
                    Text(NSLocalizedString("Polymech.brand", comment: "Polymech toolbar"))
                }
            }
            .font(.system(size: 10, weight: .semibold))
            .foregroundStyle(.secondary)
            runButton
        }
    }
}
