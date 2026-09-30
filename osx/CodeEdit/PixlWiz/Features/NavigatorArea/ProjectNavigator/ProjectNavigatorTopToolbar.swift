//
//  ProjectNavigatorTopToolbar.swift
//  PixlWiz
//
//  Top toolbar for the Project Navigator.
//  • Left  : tree / thumb layout toggle (persisted to GeneralSettings).
//  • Right : Places dropdown — jump to Home, Documents, Pictures, etc.,
//            or reset to the workspace root.
//

import SwiftUI

struct ProjectNavigatorTopToolbar: View {

    @EnvironmentObject var workspace: WorkspaceDocument

    @AppSettings(\.general.navigatorLayout)
    private var navigatorLayout

    @Environment(\.controlActiveState)
    private var activeState

    // MARK: - Places

    private struct Place: Identifiable {
        let id: String
        let name: String
        let icon: String      // SF Symbol name
        let url: URL?

        static let all: [Place] = {
            let fm = FileManager.default
            func dir(_ d: FileManager.SearchPathDirectory) -> URL? {
                fm.urls(for: d, in: .userDomainMask).first
            }
            let home = URL(fileURLWithPath: NSHomeDirectory())
            return [
                Place(id: "home",      name: "Home",      icon: "house",                url: home),
                Place(id: "docs",      name: "Documents", icon: "doc",                  url: dir(.documentDirectory)),
                Place(id: "dl",        name: "Downloads", icon: "arrow.down.circle",    url: dir(.downloadsDirectory)),
                Place(id: "desktop",   name: "Desktop",   icon: "desktopcomputer",      url: dir(.desktopDirectory)),
                Place(id: "pictures",  name: "Pictures",  icon: "photo",                url: dir(.picturesDirectory)),
                Place(id: "movies",    name: "Movies",    icon: "film",                 url: dir(.moviesDirectory)),
                Place(id: "music",     name: "Music",     icon: "music.note",           url: dir(.musicDirectory)),
            ]
        }()
    }

    // MARK: - Body

    var body: some View {
        HStack(spacing: 6) {
            layoutToggle
            Spacer()
            placesMenu
        }
        .padding(.horizontal, 8)
        .frame(height: 28)
        .frame(maxWidth: .infinity)
        .overlay(alignment: .bottom) { Divider() }
        .opacity(activeState == .inactive ? 0.65 : 1)
    }

    // MARK: - Layout toggle

    private var layoutToggle: some View {
        Picker("Navigator layout", selection: $navigatorLayout) {
            Image(systemName: "list.bullet")
                .help("Tree view")
                .tag(SettingsData.NavigatorLayout.tree)
            Image(systemName: "square.grid.2x2")
                .help("Thumbnail grid")
                .tag(SettingsData.NavigatorLayout.thumb)
        }
        .pickerStyle(.segmented)
        .frame(width: 60)
        .labelsHidden()
        .accessibilityLabel("Navigator layout")
    }

    // MARK: - Places menu

    private var placesMenu: some View {
        Menu {
            // Workspace root — always available
            Button {
                workspace.setNavigatorWorkingDirectoryURL(nil)
            } label: {
                Label("Workspace Root", systemImage: "folder.badge.gearshape")
            }

            Divider()

            ForEach(Place.all) { place in
                if let url = place.url {
                    Button {
                        workspace.setNavigatorWorkingDirectoryURL(url)
                    } label: {
                        Label(place.name, systemImage: place.icon)
                    }
                }
            }
        } label: {
            Image(systemName: "sidebar.left")
                .imageScale(.medium)
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
        .frame(width: 22)
        .help("Go to place")
        .accessibilityLabel("Places")
    }
}
