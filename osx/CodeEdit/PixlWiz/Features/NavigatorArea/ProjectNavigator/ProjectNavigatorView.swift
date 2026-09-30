//
//  ProjectNavigatorView.swift
//  CodeEdit
//
//  Created by Lukas Pistrol on 25.03.22.
//

import SwiftUI

/// # Project Navigator - Sidebar
///
/// A list that functions as a project navigator, showing collapsible folders
/// and files.
///
/// When selecting a file it will open in the editor.
///
struct ProjectNavigatorView: View {

    @AppSettings(\.general.navigatorLayout)
    private var navigatorLayout

    var body: some View {
        VStack(spacing: 0) {
            // Top toolbar: tree/thumb toggle + places dropdown
            ProjectNavigatorTopToolbar()

            // Content: switches between outline tree and thumbnail grid
            switch navigatorLayout {
            case .tree:
                ProjectNavigatorOutlineView()
                    .safeAreaInset(edge: .bottom, spacing: 0) {
                        ProjectNavigatorToolbarBottom()
                    }
            case .thumb:
                ProjectNavigatorThumbView()
                    .safeAreaInset(edge: .bottom, spacing: 0) {
                        ProjectNavigatorToolbarBottom()
                    }
            }
        }
    }
}
