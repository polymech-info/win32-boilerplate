//
//  OutlineMenu.swift
//  CodeEdit
//
//  Created by Lukas Pistrol on 07.04.22.
//

import Foundation
import SwiftUI
import UniformTypeIdentifiers

/// A subclass of `NSMenu` implementing the contextual menu for the project navigator
final class ProjectNavigatorMenu: NSMenu {

    /// The item to show the contextual menu for
    var item: CEWorkspaceFile?

    /// The workspace, for opening the item
    var workspace: WorkspaceDocument?

    /// The  `ProjectNavigatorViewController` is being called from.
    /// By sending it, we can access it's variables and functions.
    var sender: ProjectNavigatorViewController

    init(_ sender: ProjectNavigatorViewController) {
        self.sender = sender
        super.init(title: NSLocalizedString("Navigator.menu.options", comment: "Context menu title"))
    }

    @available(*, unavailable)
    required init(coder _: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    /// Creates a `NSMenuItem` depending on the given arguments
    /// - Parameters:
    ///   - title: The title of the menu item
    ///   - action: A `Selector` or `nil` of the action to perform.
    ///   - key: A `keyEquivalent` of the menu item. Defaults to an empty `String`
    /// - Returns: A `NSMenuItem` which has the target `self`
    private func menuItem(_ title: String, action: Selector?, key: String = "") -> NSMenuItem {
        let mItem = NSMenuItem(title: title, action: action, keyEquivalent: key)
        mItem.target = self

        return mItem
    }

    /// Configures the menu based on the current selection in the outline view.
    /// - Menu items get added depending on the amount of selected items.
    private func setupMenu() { // swiftlint:disable:this function_body_length
        guard let item else { return }
        if item.isProjectNavigatorUpRow {
            let showInFinder = menuItem(
                NSLocalizedString("Show in Finder", comment: "Navigator context"),
                action: #selector(showInFinder)
            )
            let copyPath = menuItem(
                NSLocalizedString("Copy Path", comment: "Navigator context"),
                action: #selector(copyPath)
            )
            let copyRelativePath = menuItem(
                NSLocalizedString("Copy Relative Path", comment: "Navigator context"),
                action: #selector(copyRelativePath)
            )
            items = [
                showInFinder,
                NSMenuItem.separator(),
                copyPath,
                copyRelativePath
            ]
            return
        }
        let showInFinder = menuItem(
            NSLocalizedString("Show in Finder", comment: "Navigator context"),
            action: #selector(showInFinder)
        )

        let openInTab = menuItem(
            NSLocalizedString("Open in Tab", comment: "Navigator context"),
            action: #selector(openInTab)
        )
        let openInNewWindow = menuItem(
            NSLocalizedString("Open in New Window", comment: "Navigator context"),
            action: nil
        )
        let openExternalEditor = menuItem(
            NSLocalizedString("Open with External Editor", comment: "Navigator context"),
            action: #selector(openWithExternalEditor)
        )
        let openAs = menuItem(NSLocalizedString("Open As", comment: "Navigator context"), action: nil)

        let copyPath = menuItem(
            NSLocalizedString("Copy Path", comment: "Navigator context"),
            action: #selector(copyPath)
        )
        let copyRelativePath = menuItem(
            NSLocalizedString("Copy Relative Path", comment: "Navigator context"),
            action: #selector(copyRelativePath)
        )

        let transform = NSMenuItem(
            title: NSLocalizedString("Polymech.navigator.transform", comment: "Navigator context"),
            action: nil,
            keyEquivalent: ""
        )
        transform.submenu = transformSubmenu()
        transform.isEnabled = !selectedItems().isEmpty

        let showFileInspector = menuItem(
            NSLocalizedString("Show File Inspector", comment: "Navigator context"),
            action: nil
        )

        let newFile = menuItem(
            NSLocalizedString("New File…", comment: "Navigator context"),
            action: #selector(newFile)
        )
        let newFileFromClipboard = menuItem(
            NSLocalizedString("New File from Clipboard", comment: "Navigator context"),
            action: #selector(newFileFromClipboard),
            key: "v"
        )
        newFileFromClipboard.keyEquivalentModifierMask = [.command]
        let newFolder = menuItem(
            NSLocalizedString("New Folder", comment: "Navigator context"),
            action: #selector(newFolder)
        )

        let rename = menuItem(
            NSLocalizedString("Rename", comment: "Navigator context"),
            action: #selector(renameFile)
        )

        let trash = menuItem(
            NSLocalizedString("Move to Trash", comment: "Navigator context"),
            action: item.url != workspace?.workspaceFileManager?.folderUrl
                ? #selector(trash) : nil
        )

        // trash has to be the previous menu item for delete.isAlternate to work correctly
        let delete = menuItem(
            NSLocalizedString("Delete Immediately…", comment: "Navigator context"),
            action: item.url != workspace?.workspaceFileManager?.folderUrl
                ? #selector(delete) : nil
        )
        delete.keyEquivalentModifierMask = .option
        delete.isAlternate = true

        let dupTitle = item.isFolder
            ? NSLocalizedString("Duplicate Folder", comment: "Navigator context")
            : NSLocalizedString("Duplicate File", comment: "Navigator context")
        let duplicate = menuItem(dupTitle, action: #selector(duplicate))

        let sortByName = menuItem(
            NSLocalizedString("Sort by Name", comment: "Navigator context"),
            action: nil
        )
        sortByName.isEnabled = item.isFolder

        let sortByType = menuItem(
            NSLocalizedString("Sort by Type", comment: "Navigator context"),
            action: nil
        )
        sortByType.isEnabled = item.isFolder

        //let sourceControl = menuItem("Source Control", action: nil)

        items = [
            showInFinder,
            NSMenuItem.separator(),
            openInTab,
            openInNewWindow,
            openExternalEditor,
            openAs,
            NSMenuItem.separator(),
            copyPath,
            copyRelativePath,
            NSMenuItem.separator(),
            transform,
            NSMenuItem.separator(),
            showFileInspector,
            NSMenuItem.separator(),
            newFile,
            newFileFromClipboard,
            newFolder
        ]

        if canCreateFolderFromSelection() {
            items.append(
                menuItem(
                    NSLocalizedString("New Folder from Selection", comment: "Navigator context"),
                    action: #selector(newFolderFromSelection)
                )
            )
        }
        items.append(NSMenuItem.separator())
        if selectedItems().count == 1 {
            items.append(rename)
        }

        // polymech override: Source Control item removed (menu not wired; see commented `sourceControl` above).
        items.append(
            contentsOf: [
                trash,
                delete,
                duplicate,
                NSMenuItem.separator(),
                sortByName,
                sortByType,
            ]
        )

        setSubmenu(openAsMenu(item: item), for: openAs)
        //setSubmenu(sourceControlMenu(item: item), for: sourceControl)
    }

    /// Submenu for **Open As** menu item.
    private func openAsMenu(item: CEWorkspaceFile) -> NSMenu {
        let openAsMenu = NSMenu(
            title: NSLocalizedString("Open As", comment: "Navigator submenu title")
        )
        func getMenusItems() -> ([NSMenuItem], [NSMenuItem]) {
            // Use UTType to distinguish between bundle file and user-browsable directory
            // The isDirectory property is not accurate on this.
            guard let type = item.contentType else { return ([.none()], []) }
            if type.conforms(to: .folder) {
                return ([.none()], [])
            }
            var primaryItems = [NSMenuItem]()
            if type.conforms(to: .sourceCode) {
                primaryItems.append(.sourceCode())
            }
            if type.conforms(to: .propertyList) {
                primaryItems.append(.propertyList())
            }
            if type.conforms(to: UTType(filenameExtension: "xcassets")!) {
                primaryItems.append(
                    NSMenuItem(
                        title: NSLocalizedString("Asset Catalog Document", comment: "Open As"),
                        action: nil,
                        keyEquivalent: ""
                    )
                )
            }
            if type.conforms(to: UTType(filenameExtension: "xib")!) {
                primaryItems.append(
                    NSMenuItem(
                        title: NSLocalizedString("Interface Builder XIB Document", comment: "Open As"),
                        action: nil,
                        keyEquivalent: ""
                    )
                )
            }
            if type.conforms(to: UTType(filenameExtension: "xcodeproj")!) {
                primaryItems.append(
                    NSMenuItem(
                        title: NSLocalizedString("Xcode Project", comment: "Open As"),
                        action: nil,
                        keyEquivalent: ""
                    )
                )
            }
            var secondaryItems = [NSMenuItem]()
            if type.conforms(to: .text) {
                secondaryItems.append(.asciiPropertyList())
                secondaryItems.append(.hex())
            }

            // FIXME: Update the quickLook condition
            if type.conforms(to: .data) {
                secondaryItems.append(.quickLook())
            }

            return (primaryItems, secondaryItems)
        }
        let (primaryItems, secondaryItems) = getMenusItems()
        for item in primaryItems {
            openAsMenu.addItem(item)
        }
        if !secondaryItems.isEmpty {
            openAsMenu.addItem(.separator())
        }
        for item in secondaryItems {
            openAsMenu.addItem(item)
        }
        return openAsMenu
    }

    /// `Transform` → style presets from `settings.json` / `chat_web.quick_actions` (see `PolymechStylePresetLoader` + `apps/chat` quick actions).
    private func transformSubmenu() -> NSMenu {
        let m = NSMenu(
            title: NSLocalizedString("Polymech.navigator.transform", comment: "Navigator context")
        )
        for p in PolymechStylePresetLoader.all() {
            let mi = NSMenuItem(
                title: PolymechStylePresetRunCoordinator.menuItemTitle(for: p),
                action: #selector(runPolymechTransformPreset(_:)),
                keyEquivalent: ""
            )
            mi.target = self
            let dict: [String: String] = [
                "id": p.id,
                "name": p.name,
                "prompt": p.prompt,
                "icon": p.icon
            ]
            mi.representedObject = dict as NSDictionary
            m.addItem(mi)
        }
        if m.items.isEmpty {
            let empty = NSMenuItem(
                title: NSLocalizedString("Polymech.navigator.presetEmpty", comment: "Navigator context"),
                action: nil,
                keyEquivalent: ""
            )
            empty.isEnabled = false
            m.addItem(empty)
        }
        return m
    }

    /// Submenu for **Source Control** menu item.
    private func sourceControlMenu(item: CEWorkspaceFile) -> NSMenu {
        let sourceControlMenu = NSMenu(title: "Source Control")
        sourceControlMenu.addItem(
            withTitle: "Commit \"\(String(describing: item.fileName()))\"...",
            action: nil,
            keyEquivalent: ""
        )
        sourceControlMenu.addItem(.separator())
        sourceControlMenu.addItem(withTitle: "Discard Changes...", action: nil, keyEquivalent: "")
        sourceControlMenu.addItem(.separator())
        sourceControlMenu.addItem(withTitle: "Add Selected Files", action: nil, keyEquivalent: "")
        sourceControlMenu.addItem(withTitle: "Mark Selected Files as Resolved", action: nil, keyEquivalent: "")

        return sourceControlMenu
    }

    /// Updates the menu for the selected item and hides it if no item is provided.
    override func update() {
        removeAllItems()
        setupMenu()
    }
}

extension NSMenuItem {
    fileprivate static func none() -> NSMenuItem {
        let item = NSMenuItem(
            title: NSLocalizedString("Navigator.openAs.none", comment: "Open As"),
            action: nil,
            keyEquivalent: ""
        )
        item.isEnabled = false
        return item
    }

    fileprivate static func sourceCode() -> NSMenuItem {
        NSMenuItem(
            title: NSLocalizedString("Source Code", comment: "Open As"),
            action: nil,
            keyEquivalent: ""
        )
    }

    fileprivate static func propertyList() -> NSMenuItem {
        NSMenuItem(
            title: NSLocalizedString("Property List", comment: "Open As"),
            action: nil,
            keyEquivalent: ""
        )
    }

    fileprivate static func asciiPropertyList() -> NSMenuItem {
        NSMenuItem(
            title: NSLocalizedString("ASCII Property List", comment: "Open As"),
            action: nil,
            keyEquivalent: ""
        )
    }

    fileprivate static func hex() -> NSMenuItem {
        NSMenuItem(
            title: NSLocalizedString("Hex", comment: "Open As"),
            action: nil,
            keyEquivalent: ""
        )
    }

    fileprivate static func quickLook() -> NSMenuItem {
        NSMenuItem(
            title: NSLocalizedString("Quick Look", comment: "Open As"),
            action: nil,
            keyEquivalent: ""
        )
    }
}
