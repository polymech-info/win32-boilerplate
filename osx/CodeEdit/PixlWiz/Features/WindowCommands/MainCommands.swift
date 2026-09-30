//
//  MainCommands.swift
//  CodeEdit
//
//  Created by Wouter Hennen on 13/03/2023.
//

import SwiftUI
#if !APP_STORE
import Sparkle
#endif

struct MainCommands: Commands {
    @Environment(\.openWindow)
    var openWindow

    var body: some Commands {
        CommandGroup(replacing: .appInfo) {
            Button("About PixelWiz") { openWindow(sceneID: .about)}
            /*
            // polymech.override
            Button("Check for updates...") {
                NSApp.sendAction(#selector(SPUStandardUpdaterController.checkForUpdates(_:)), to: nil, from: nil)
            }
            */
        }

        CommandGroup(replacing: .appSettings) {
            Button("Settings...") {
                openWindow(sceneID: .settings)
            }
            .keyboardShortcut(",")
        }
    }
}
