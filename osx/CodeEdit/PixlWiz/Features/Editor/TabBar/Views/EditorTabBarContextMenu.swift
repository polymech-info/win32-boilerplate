//
//  EditorTabBarContextMenu.swift
//  CodeEdit
//
//  Created by Khan Winter on 6/4/22.
//

import SwiftUI
import Foundation

extension View {
    func tabBarContextMenu(item: CEWorkspaceFile, isTemporary: Bool) -> some View {
        modifier(EditorTabBarContextMenu(item: item, isTemporary: isTemporary))
    }
}

struct EditorTabBarContextMenu: ViewModifier {
    init(
        item: CEWorkspaceFile,
        isTemporary: Bool
    ) {
        self.item = item
        self.isTemporary = isTemporary
    }

    @EnvironmentObject var workspace: WorkspaceDocument

    @EnvironmentObject var tabs: Editor

    @Environment(\.splitEditor)
    var splitEditor

    private var item: CEWorkspaceFile
    private var isTemporary: Bool

    // swiftlint:disable:next function_body_length
    func body(content: Content) -> some View {
        content.contextMenu(menuItems: {
            Group {
                Button {
                    withAnimation {
                        tabs.closeTab(file: item)
                    }
                } label: {
                    Text(LocalizedStringKey("Close Tab"))
                }
                .keyboardShortcut("w", modifiers: [.command])

                Button {
                    withAnimation {
                        tabs.tabs.map({ $0.file }).forEach { file in
                            if file != item {
                                tabs.closeTab(file: file)
                            }
                        }
                    }
                } label: {
                    Text(LocalizedStringKey("Close Other Tabs"))
                }

                Button {
                    withAnimation {
                        if let index = tabs.tabs.firstIndex(where: { $0.file == item }), index + 1 < tabs.tabs.count {
                            tabs.tabs[(index + 1)...].forEach {
                                tabs.closeTab(file: $0.file)
                            }
                        }
                    }
                } label: {
                    Text(LocalizedStringKey("Close Tabs to the Right"))
                }
                // Disable this option when current tab is the last one.
                .disabled(tabs.tabs.last?.file == item)

                Button {
                    withAnimation {
                        tabs.tabs.forEach {
                            tabs.closeTab(file: $0.file)
                        }
                    }
                } label: {
                    Text(LocalizedStringKey("Close All"))
                }

                if isTemporary {
                    Button {
                        tabs.temporaryTab = nil
                    } label: {
                        Text(LocalizedStringKey("Keep Open"))
                    }
                }
            }

            Divider()

            Group {
                Button {
                    copyPath(item: item)
                } label: {
                    Text(LocalizedStringKey("Copy Path"))
                }

                Button {
                    copyRelativePath(item: item)
                } label: {
                    Text(LocalizedStringKey("Copy Relative Path"))
                }
            }

            Divider()

            Group {
                Button {
                    item.showInFinder()
                } label: {
                    Text(LocalizedStringKey("Show in Finder"))
                }

                Button {
                    workspace.listenerModel.highlightedFileItem = item
                } label: {
                    Text(LocalizedStringKey("Reveal in Project Navigator"))
                }

                Button {
                } label: {
                    Text(LocalizedStringKey("Open in New Window"))
                }
                .disabled(true)
            }

            Divider()

            Button {
                moveToNewSplit(.top)
            } label: {
                Text(LocalizedStringKey("Split Up"))
            }
            Button {
                moveToNewSplit(.bottom)
            } label: {
                Text(LocalizedStringKey("Split Down"))
            }
            Button {
                moveToNewSplit(.leading)
            } label: {
                Text(LocalizedStringKey("Split Left"))
            }
            Button {
                moveToNewSplit(.trailing)
            } label: {
                Text(LocalizedStringKey("Split Right"))
            }
        })
    }

    // MARK: - Actions

    /// Copies the absolute path of the given `FileItem`
    /// - Parameter item: The `FileItem` to use.
    private func copyPath(item: CEWorkspaceFile) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(item.url.standardizedFileURL.path, forType: .string)
    }

    func moveToNewSplit(_ edge: Edge) {
        let newEditor = Editor(files: [item], workspace: workspace)
        splitEditor(edge, newEditor)
        tabs.closeTab(file: item)
        workspace.editorManager?.activeEditor = newEditor
    }

    /// Copies the relative path from the workspace folder to the given file item to the pasteboard.
    /// - Parameter item: The `FileItem` to use.
    private func copyRelativePath(item: CEWorkspaceFile) {
        guard let rootPath = workspace.workspaceFileManager?.folderUrl else {
            return
        }
        let destinationComponents = item.url.standardizedFileURL.pathComponents
        let baseComponents = rootPath.standardizedFileURL.pathComponents

        // Find common prefix length
        var prefixCount = 0
        while prefixCount < min(destinationComponents.count, baseComponents.count)
                && destinationComponents[prefixCount] == baseComponents[prefixCount] {
            prefixCount += 1
        }
        // Build the relative path
        let upPath = String(repeating: "../", count: baseComponents.count - prefixCount)
        let downPath = destinationComponents[prefixCount...].joined(separator: "/")

        // Copy it to the clipboard
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(upPath + downPath, forType: .string)
    }
}
