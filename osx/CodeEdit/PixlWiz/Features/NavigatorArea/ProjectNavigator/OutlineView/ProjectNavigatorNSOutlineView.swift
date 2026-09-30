//
//  ProjectNavigatorNSOutlineView.swift
//  CodeEdit
//
//  Created by Khan Winter on 6/10/25.
//

import AppKit

final class ProjectNavigatorNSOutlineView: NSOutlineView, NSMenuItemValidation {
    weak var projectNavigatorViewController: ProjectNavigatorViewController?

    // Command+C / Command+V / Command+Delete are handled in
    // ``ProjectNavigatorViewController+KeyboardNavigation`` (works when a row, not the outline, is first responder).

    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        if menuItem.action == #selector(ProjectNavigatorMenu.newFileFromClipboard) {
            return !selectedRowIndexes.isEmpty
        }
        return false
    }
}
