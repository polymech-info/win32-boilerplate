//
//  SettingsView.swift
//  CodeEdit
//
//  Created by Raymond Vleeshouwer on 26/03/23.
//

import SwiftUI

/// A struct for settings
struct SettingsView: View {
    @StateObject var model = SettingsViewModel()
    @Environment(\.colorScheme)
    private var colorScheme

    /// Variables for the selected Page, the current search text and software updater
    @State private var selectedPage: SettingsPage = Self.pages[0].page
    @State private var searchText: String = ""
    @State private var showDeveloperSettings: Bool = false

    @Environment(\.presentationMode)
    var presentationMode

    static var pages: [PageAndSettings] = [
        .init(
            SettingsPage(
                .general,
                baseColor: .gray,
                icon: .system("gear")
            )
        ),
        .init(
            // polymech override: dedicated provider settings page (text + image, import/export)
            SettingsPage(
                .aiProviders,
                baseColor: .purple,
                icon: .system("sparkles")
            )
        ),
        .init(
            SettingsPage(
                .polymechChat,
                baseColor: .indigo,
                icon: .system("message.badge.filled.fill")
            )
        ),
        // polymech override: hide Accounts for now (will be reintroduced later).
        .init(
            SettingsPage(
                .navigation,
                baseColor: .green,
                icon: .system("arrow.triangle.turn.up.right.diamond.fill")
            )
        ),
        .init(
            SettingsPage(
                .theme,
                baseColor: .pink,
                icon: .system("paintbrush.fill")
            )
        ),
        // polymech override: hide Text Editing and Terminal pages.
        .init(
            SettingsPage(
                .search,
                baseColor: .blue,
                icon: .system("magnifyingglass")
            )
        ),
        // polymech override: hide Source Control page.
        .init(
            SettingsPage(
                .location,
                baseColor: .green,
                icon: .system("externaldrive.fill")
            )
        ),
        // polymech override: hide Language Servers page.
        .init(
            SettingsPage(
                .developer,
                baseColor: .pink,
                icon: .system("bolt")
            )
        ),
    ]

    @ObservedObject private var settings: Settings = .shared

    let updater: SoftwareUpdater

    /// Searches through an array of pages to check if a page name exists in the array
    private func resultFound(_ page: SettingsPage, pages: [SettingsPage]) -> SettingsSearchResult {
        let lowercasedSearchText = searchText.lowercased()
        var returnedPages: [SettingsPage] = []
        var foundPage = false

        for item in pages where item.name == page.name {
            if item.isSetting && item.settingName.lowercased().contains(lowercasedSearchText) {
                returnedPages.append(item)
            } else if item.name.rawValue.contains(lowercasedSearchText) && !item.isSetting {
                foundPage = true
            }
        }

        return SettingsSearchResult(pageFound: foundPage, pages: returnedPages)
    }

    /// Gets search results from a settings page and an array of settings
    @ViewBuilder
    private func results(_ page: SettingsPage, _ settings: [SettingsPage]) -> some View {
        if !searchText.isEmpty {
            let results: SettingsSearchResult = resultFound(page, pages: settings)

            if !results.pages.isEmpty && !page.isSetting {
                SettingsPageView(page, searchText: searchText)

                ForEach(results.pages, id: \.settingName) { setting in
                    NavigationLink(value: setting) {
                        setting.settingName.highlightOccurrences(searchText)
                            .padding(.leading, 22)
                    }
                }
            } else if page.name.rawValue.lowercased().contains(searchText.lowercased()) && !page.isSetting {
                SettingsPageView(page, searchText: searchText)
            }
        } else if !page.isSetting {
            if page.name == .developer && !showDeveloperSettings {
                EmptyView()
            } else {
                SettingsPageView(page, searchText: searchText)
            }
        }
    }

    var body: some View {
        NavigationSplitView {
            /// Remove the extra List workaround; macOS 26's sidebar .searchable now matches System Settings
            if #unavailable(macOS 26.0) {
                List { }
                    .searchable(text: $searchText, placement: .sidebar, prompt: "Search")
                    .scrollDisabled(true)
                    .frame(height: 30)
                List(selection: $selectedPage) {
                    Section {
                        ForEach(Self.pages) { pageAndSettings in
                            results(pageAndSettings.page, pageAndSettings.settings)
                        }
                    }
                }
                .navigationSplitViewColumnWidth(215)
            } else {
                List(selection: $selectedPage) {
                    Section {
                        ForEach(Self.pages) { pageAndSettings in
                            results(pageAndSettings.page, pageAndSettings.settings)
                        }
                    }
                }
                .toolbar(removing: .sidebarToggle)
                .searchable(text: $searchText, placement: .sidebar, prompt: "Search")
                .navigationSplitViewColumnWidth(215)
            }
        } detail: {
            Group {
                switch selectedPage.name {
                case .general:
                    GeneralSettingsView().environmentObject(updater)
                case .aiProviders:
                    AIProvidersSettingsView()
                case .polymechChat:
                    PolymechChatSettingsView()
                case .navigation:
                    NavigationSettingsView()
                case .theme:
                    ThemeSettingsView()
                case .search:
                    SearchSettingsView()
                case .location:
                    LocationsSettingsView()
                case .developer:
                    DeveloperSettingsView()
                default:
                    Text("Implementation Needed").frame(alignment: .center)
                }
            }
            .navigationSplitViewColumnWidth(min: 500, ideal: 750, max: .infinity)
            .onAppear {
                model.backButtonVisible = false
            }
        }
        .hideSidebarToggle()
        .navigationTitle(selectedPage.name.rawValue)
        .toolbar {
            /// macOS 26 automatically adjusts the leading padding for navigationTitle
            if #unavailable(macOS 26.0) {
                ToolbarItem(placement: .navigation) {
                    if !model.backButtonVisible {
                        Rectangle()
                            .frame(width: 10)
                            .opacity(0)
                    } else {
                        EmptyView()
                    }
                }
            }
        }
        .environmentObject(model)
        .onAppear {
            // Monitor for the F12 key down event to toggle the developer settings
            model.setKeyDownMonitor { event in
                if event.keyCode == 111 {
                    showDeveloperSettings.toggle()

                    // If the developer menu is hidden and is selected, go back to default page
                    if !showDeveloperSettings && selectedPage.name == .developer {
                        selectedPage = Self.pages[0].page
                    }
                    return nil
                }
                return event
            }
        }
        .onDisappear {
            model.removeKeyDownMonitor()
        }
    }
}

class SettingsViewModel: ObservableObject {
    @Published var backButtonVisible: Bool = false
    @Published var scrolledToTop: Bool = false

    /// Holds a monitor closure for the `keyDown` event
    private var keyDownEventMonitor: Any?

    func setKeyDownMonitor(monitor: @escaping (NSEvent) -> NSEvent?) {
        keyDownEventMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown, handler: monitor)
    }

    func removeKeyDownMonitor() {
        if let eventMonitor = keyDownEventMonitor {
            NSEvent.removeMonitor(eventMonitor)
            self.keyDownEventMonitor = nil
        }
    }

    deinit {
        removeKeyDownMonitor()
    }
}
