//
//  CodeEditApp.swift
//  CodeEdit
//
//  Created by Wouter Hennen on 11/03/2023.
//

import SwiftUI
import WelcomeWindow
import AboutWindow

@main
struct CodeEditApp: App {
    @NSApplicationDelegateAdaptor var appdelegate: AppDelegate
    @ObservedObject private var settings: Settings

    let updater: SoftwareUpdater = SoftwareUpdater()

    init() {
        // C++ `logger` (spdlog): stderr + Desktop log + Console.app notice — same layout as `pm-image` CLI.
        PolymechImageEngine.bootstrapNativeLoggingAtLaunch()

        // Before Settings.shared loads bundles, match saved `appLanguage` to `AppleLanguages`.
        AppLanguageRuntime.applyFromSavedFileIfPresent()

        _settings = ObservedObject(wrappedValue: Settings.shared)

        // Register singleton services before anything else
        ServiceContainer.register(
            LSPService()
        )

        _ = CodeEditDocumentController.shared
        NSMenuItem.swizzle()
        NSSplitViewItem.swizzle()
    }

    var body: some Scene {
        Group {
            WelcomeWindow(
                subtitleView: { WelcomeSubtitleView() },
                actions: { dismissWindow in
                    NewFileButton(dismissWindow: dismissWindow)
                    GitCloneButton(dismissWindow: dismissWindow)
                    OpenFileOrFolderButton(dismissWindow: dismissWindow)
                },
                onDrop: { url, dismissWindow in
                    Task {
                        await CodeEditDocumentController.shared.openDocument(at: url, onCompletion: { dismissWindow() })
                    }
                }
            )

            ExtensionManagerWindow()

            AboutWindow(
                subtitleView: { AboutSubtitleView() },
                actions: {
                    AboutButton(title: "Contributors", destination: {
                        ContributorsView()
                    })
                    AboutButton(title: "Acknowledgements", destination: {
                        AcknowledgementsView()
                    })
                },
                footer: { AboutFooterView() }
            )

            SettingsWindow()
        }
        // Attach to the scene group so workspace windows get the same menu bar (View → Chat, etc.), not only Settings.
        .commands {
            CodeEditCommands()
        }
        .environment(\.settings, settings.preferences) // Add settings to each window environment
    }
}
