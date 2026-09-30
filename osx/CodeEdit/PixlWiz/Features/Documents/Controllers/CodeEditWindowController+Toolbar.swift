//
//  CodeEditWindowController+Toolbar.swift
//  CodeEdit
//
//  Created by Daniel Zhu on 5/10/24.
//

import AppKit
import SwiftUI
import Combine

extension CodeEditWindowController {
    internal func setupToolbar() {
        let toolbar = NSToolbar(identifier: UUID().uuidString)
        toolbar.delegate = self
        toolbar.showsBaselineSeparator = false
        self.window?.titleVisibility = toolbarCollapsed ? .visible : .hidden
        if #available(macOS 26, *) {
            self.window?.toolbarStyle = .automatic
            toolbar.centeredItemIdentifiers = [.activityViewer, .notificationItem]
            toolbar.displayMode = .iconOnly
            self.window?.titlebarAppearsTransparent = true
        } else {
            self.window?.toolbarStyle = .unifiedCompact
            toolbar.displayMode = .labelOnly
        }
        self.window?.titlebarSeparatorStyle = .automatic
        self.window?.toolbar = toolbar
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        var items: [NSToolbarItem.Identifier] = [
            .toggleFirstSidebarItem,
            .flexibleSpace,
        ]

        // polymech override: hide task start/stop toolbar controls (including dropdown group).

        items += [
            .sidebarTrackingSeparator,
            .flexibleSpace,
        ]

        if #available(macOS 26, *) {
            items += [
                .activityViewer,
                .space,
                .notificationItem,
            ]
        } else {
            items += [
                .activityViewer,
                .notificationItem,
                .flexibleSpace,
            ]
        }

        items += [
            .flexibleSpace,
            .itemListTrackingSeparator,
            .flexibleSpace,
            .toggleLastSidebarItem
        ]

        return items
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        var items: [NSToolbarItem.Identifier] = [
            .toggleFirstSidebarItem,
            .sidebarTrackingSeparator,
            .flexibleSpace,
            .itemListTrackingSeparator,
            .toggleLastSidebarItem,
            .activityViewer,
            .notificationItem,
        ]

        // polymech override: do not expose task toolbar items in customization palette.

        return items
    }

    func toggleToolbar() {
        toolbarCollapsed.toggle()
        workspace?.addToWorkspaceState(key: .toolbarCollapsed, value: toolbarCollapsed)
        updateToolbarVisibility()
    }

    func updateToolbarVisibility() {
        if toolbarCollapsed {
            window?.titleVisibility = .visible
            window?.title = workspace?.workspaceFileManager?.folderUrl.lastPathComponent ?? "Empty"
            window?.toolbar = nil
        } else {
            window?.titleVisibility = .hidden
            setupToolbar()
        }
    }

    // swiftlint:disable:next function_body_length cyclomatic_complexity
    func toolbar(
        _ toolbar: NSToolbar,
        itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier,
        willBeInsertedIntoToolbar flag: Bool
    ) -> NSToolbarItem? {
        switch itemIdentifier {
        case .itemListTrackingSeparator:
            guard let splitViewController else { return nil }

            return NSTrackingSeparatorToolbarItem(
                identifier: .itemListTrackingSeparator,
                splitView: splitViewController.splitView,
                dividerIndex: 1
            )
        case .toggleFirstSidebarItem:
            let toolbarItem = NSToolbarItem(itemIdentifier: NSToolbarItem.Identifier.toggleFirstSidebarItem)
            toolbarItem.paletteLabel = " Navigator Sidebar"
            toolbarItem.toolTip = "Hide or show the Navigator"
            toolbarItem.isBordered = true
            toolbarItem.target = self
            toolbarItem.action = #selector(self.objcToggleFirstPanel)
            toolbarItem.image = NSImage(
                systemSymbolName: "sidebar.leading",
                accessibilityDescription: nil
            )?.withSymbolConfiguration(.init(scale: .large))

            return toolbarItem
        case .toggleLastSidebarItem:
            let toolbarItem = NSToolbarItem(itemIdentifier: NSToolbarItem.Identifier.toggleLastSidebarItem)
            toolbarItem.paletteLabel = "Agent Panel"
            toolbarItem.toolTip = "Hide or show the Agent panel"
            toolbarItem.isBordered = true
            toolbarItem.target = self
            toolbarItem.action = #selector(self.objcToggleLastPanel)
            toolbarItem.image = NSImage(
                systemSymbolName: "sidebar.trailing",
                accessibilityDescription: nil
            )?.withSymbolConfiguration(.init(scale: .large))

            return toolbarItem
        case .stopTaskSidebarItem:
            return nil // polymech override: hide Stop task toolbar control.
        case .startTaskSidebarItem:
            return nil // polymech override: hide Start/Create task toolbar control.
        case .branchPicker:
            return nil // polymech override: hide branch picker toolbar control.
        case .activityViewer:
            return activityViewerItem()
        case .notificationItem:
            return notificationItem()
        case .taskSidebarItem:
            return nil // polymech override: hide unified task dropdown control.
        default:
            return NSToolbarItem(itemIdentifier: itemIdentifier)
        }
    }

    private func stopTaskSidebarItem() -> NSToolbarItem? {
        let toolbarItem = NSToolbarItem(itemIdentifier: NSToolbarItem.Identifier.stopTaskSidebarItem)

        guard let taskManager = workspace?.taskManager else { return nil }

        let view = NSHostingView(
            rootView: StopTaskToolbarButton(taskManager: taskManager)
        )
        toolbarItem.view = view

        return toolbarItem
    }

    private func startTaskSidebarItem() -> NSToolbarItem? {
        let toolbarItem = NSToolbarItem(itemIdentifier: NSToolbarItem.Identifier.startTaskSidebarItem)

        guard let taskManager = workspace?.taskManager else { return nil }
        guard let workspace = workspace else { return nil }

        let view = NSHostingView(
            rootView: StartTaskToolbarButton(taskManager: taskManager)
                .environmentObject(workspace)
        )
        toolbarItem.view = view

        return toolbarItem
    }

    private func notificationItem() -> NSToolbarItem? {
        let toolbarItem = NSToolbarItem(itemIdentifier: .notificationItem)
        guard let workspace = workspace else { return nil }
        let view = NSHostingView(rootView: NotificationToolbarItem().environmentObject(workspace))
        toolbarItem.view = view
        return toolbarItem
    }

    private func activityViewerItem() -> NSToolbarItem? {
        let toolbarItem = NSToolbarItem(itemIdentifier: NSToolbarItem.Identifier.activityViewer)
        toolbarItem.visibilityPriority = .user
        guard let workspace,
              let editorManager = workspace.editorManager,
              let workspaceSettingsManager = workspace.workspaceSettingsManager,
              let taskManager = workspace.taskManager
        else { return nil }

        let av = ActivityViewer(
            workspaceFileManager: workspace.workspaceFileManager,
            workspaceSettingsManager: workspaceSettingsManager,
            taskNotificationHandler: workspace.taskNotificationHandler,
            taskManager: taskManager
        )
        .environmentObject(workspace)
        .environmentObject(editorManager)
        .environmentObject(PolymechImageJobCenter.shared)
        .environmentObject(workspace.polymechWorkflow)

        let view = NSHostingView(rootView: av)

        let weakWidth = view.widthAnchor.constraint(equalToConstant: 700)
        weakWidth.priority = .defaultLow
        let strongWidth = view.widthAnchor.constraint(greaterThanOrEqualToConstant: 480)
        strongWidth.priority = .defaultHigh

        NSLayoutConstraint.activate([
            weakWidth,
            strongWidth
        ])

        toolbarItem.view = view
        return toolbarItem
    }
}
