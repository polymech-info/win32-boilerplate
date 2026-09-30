//
//  ViewCommands.swift
//  CodeEdit
//
//  Created by Wouter Hennen on 13/03/2023.
//

import AppKit
import Combine
import Foundation
import SwiftUI

struct ViewCommands: Commands {
    @AppSettings(\.textEditing.font.size)
    var editorFontSize
    @AppSettings(\.terminal.font.size)
    var terminalFontSize
    @AppSettings(\.general.showEditorJumpBar)
    var showEditorJumpBar
    @AppSettings(\.general.dimEditorsWithoutFocus)
    var dimEditorsWithoutFocus

    @FocusedBinding(\.navigationSplitViewVisibility)
    var navigationSplitViewVisibility

    @FocusedBinding(\.inspectorVisibility)
    var inspectorVisibility

    @UpdatingWindowController var windowController: CodeEditWindowController?

    var body: some Commands {
        CommandGroup(after: .toolbar) {
            Button {
                NSApp.sendAction(#selector(CodeEditWindowController.openCommandPalette(_:)), to: nil, from: nil)
            } label: {
                Text(LocalizedStringKey("Show Command Palette"))
            }
            .keyboardShortcut("p", modifiers: [.shift, .command])

            Button {
                NSApp.sendAction(#selector(CodeEditWindowController.openSearchNavigator(_:)), to: nil, from: nil)
            } label: {
                Text(LocalizedStringKey("Open Search Navigator"))
            }
            .keyboardShortcut("f", modifiers: [.shift, .command])

            Button {
                windowController?.openPolymechWebChatInInspector()
            } label: {
                Text(LocalizedStringKey("Chat"))
            }
            .help(LocalizedStringKey("Polymech.menu.viewChat.help"))
            .disabled(windowController?.workspace?.canOpenPolymechWebChatTab() != true)

            Button {
                windowController?.workspace?.openPolymechWebChatTab()
            } label: {
                Text(LocalizedStringKey("Polymech.menu.detachChatToEditor"))
            }
            .help(LocalizedStringKey("Polymech.menu.detachChatToEditor.help"))
            .disabled(windowController?.workspace?.canOpenPolymechWebChatTab() != true)

            Button {
                Self.presentPixlwizShareSheet(windowController: windowController)
            } label: {
                Text(LocalizedStringKey("Polymech.menu.shareToPixlwiz"))
            }
            .help(LocalizedStringKey("Polymech.menu.shareToPixlwiz.help"))
            .disabled(windowController?.workspace == nil)

            Menu {
                if PolymechStylePresetLoader.all().isEmpty {
                    Button { } label: {
                        Text(LocalizedStringKey("Polymech.navigator.presetEmpty"))
                    }
                    .disabled(true)
                } else {
                    ForEach(PolymechStylePresetLoader.all(), id: \.id) { preset in
                        Button {
                            runStylePresetFromMainMenu(preset)
                        } label: {
                            Text(PolymechStylePresetRunCoordinator.menuItemTitle(for: preset))
                        }
                    }
                }
            } label: {
                Text(LocalizedStringKey("Polymech.menu.stylePresets"))
            }
            .help(LocalizedStringKey("Polymech.menu.stylePresets.help"))
            .disabled(
                windowController?.window == nil
                    || !PolymechStylePresetRunCoordinator.canRunFromMainMenu(
                        workspace: windowController?.workspace
                    )
            )

            Menu {
                Button {
                    if editorFontSize < 288 {
                        editorFontSize += 1
                    }
                    if terminalFontSize < 288 {
                        terminalFontSize += 1
                    }
                } label: {
                    Text(LocalizedStringKey("Increase"))
                }
                .keyboardShortcut("+")

                Button {
                    if editorFontSize > 1 {
                        editorFontSize -= 1
                    }
                    if terminalFontSize > 1 {
                        terminalFontSize -= 1
                    }
                } label: {
                    Text(LocalizedStringKey("Decrease"))
                }
                .keyboardShortcut("-")

                Divider()

                Button {
                    editorFontSize = 12
                    terminalFontSize = 12
                } label: {
                    Text(LocalizedStringKey("Reset"))
                }
                .keyboardShortcut("0", modifiers: [.command, .control])
            } label: {
                Text(LocalizedStringKey("Font Size"))
            }
            .disabled(windowController == nil)

            Button {
            } label: {
                Text(LocalizedStringKey("Customize Toolbar…"))
            }
            .disabled(true)

            Divider()

            HideCommands()

            Divider()

            Button {
                showEditorJumpBar.toggle()
            } label: {
                Text(LocalizedStringKey(showEditorJumpBar ? "Hide Jump Bar" : "Show Jump Bar"))
            }

            Toggle(isOn: $dimEditorsWithoutFocus) {
                Text(LocalizedStringKey("Dim editors without focus"))
            }

            Divider()

            if let model = windowController?.navigatorSidebarViewModel {
                Divider()
                NavigatorCommands(model: model)
            }
        }
    }

    private func runStylePresetFromMainMenu(_ preset: PolymechStylePreset) {
        guard let wc = windowController, let workspace = wc.workspace, let window = wc.window else { return }
        let (paths, imageURLs) = PolymechStylePresetRunCoordinator.mainMenuContext(workspace: workspace)
        guard !paths.isEmpty else {
            NSSound.beep()
            return
        }
        PolymechStylePresetRunCoordinator.present(
            preset: preset,
            workspace: workspace,
            paths: paths,
            imageURLs: imageURLs
        )
    }

    /// View → Share to Pixlwiz… — uses navigator / active-tab image inputs (same as Polymech Run).
    private static func presentPixlwizShareSheet(windowController: CodeEditWindowController?) {
        guard let ws = windowController?.workspace else { return }
        let urls = ws.polymechWorkflow.effectiveImageInputURLs
        if urls.isEmpty {
            let alert = NSAlert()
            alert.messageText = NSLocalizedString("Polymech.share.noImages.title", bundle: .main, comment: "")
            alert.informativeText = NSLocalizedString("Polymech.share.noImages.message", bundle: .main, comment: "")
            alert.alertStyle = .informational
            alert.addButton(withTitle: NSLocalizedString("OK", bundle: .main, comment: ""))
            alert.runModal()
            return
        }
        ws.pixlwizSharePostSheet = PixlwizSharePostSheetContext(imageURLs: urls)
    }
}

extension ViewCommands {
    struct HideCommands: View {
        @UpdatingWindowController var windowController: CodeEditWindowController?

        var navigatorCollapsed: Bool {
            windowController?.navigatorCollapsed ?? true
        }

        var inspectorCollapsed: Bool {
            windowController?.inspectorCollapsed ?? true
        }

        var utilityAreaCollapsed: Bool {
            windowController?.workspace?.utilityAreaModel?.isCollapsed ?? true
        }

        var toolbarCollapsed: Bool {
            windowController?.toolbarCollapsed ?? true
        }

        var isInterfaceHidden: Bool {
            return windowController?.isInterfaceStillHidden() ?? false
        }

        var body: some View {
            Button {
                windowController?.toggleFirstPanel()
            } label: {
                Text(LocalizedStringKey(navigatorCollapsed ? "Show Navigator" : "Hide Navigator"))
            }
            .disabled(windowController == nil)
            .keyboardShortcut("0", modifiers: [.command])

            Button {
                windowController?.toggleLastPanel()
            } label: {
                Text(LocalizedStringKey(inspectorCollapsed ? "Show Agent Panel" : "Hide Agent Panel"))
            }
            .disabled(windowController == nil)
            .keyboardShortcut("i", modifiers: [.control, .command])

            Button {
                CommandManager.shared.executeCommand("open.drawer")
            } label: {
                Text(LocalizedStringKey(utilityAreaCollapsed ? "Show Utility Area" : "Hide Utility Area"))
            }
            .disabled(windowController == nil)
            .keyboardShortcut("y", modifiers: [.shift, .command])

            Button {
                windowController?.toggleToolbar()
            } label: {
                Text(LocalizedStringKey(toolbarCollapsed ? "Show Toolbar" : "Hide Toolbar"))
            }
            .disabled(windowController == nil)
            .keyboardShortcut("t", modifiers: [.option, .command])

            Button {
                windowController?.toggleInterface(shouldHide: !isInterfaceHidden)
            } label: {
                Text(LocalizedStringKey(isInterfaceHidden ? "Show Interface" : "Hide Interface"))
            }
            .disabled(windowController == nil)
            .keyboardShortcut("H", modifiers: [.shift, .command])
        }
    }
}

extension ViewCommands {
    struct NavigatorCommands: View {
        @ObservedObject var model: NavigatorAreaViewModel

        var body: some View {
            Menu {
                ForEach(Array(model.tabItems.prefix(9).enumerated()), id: \.element) { index, tab in
                    Button {
                        model.setNavigatorTab(tab: tab)
                    } label: {
                        Text(tab.title)
                    }
                    .keyboardShortcut(KeyEquivalent(Character(String(index + 1))))
                }
            } label: {
                Text(LocalizedStringKey("Navigators"))
            }
        }
    }
}
