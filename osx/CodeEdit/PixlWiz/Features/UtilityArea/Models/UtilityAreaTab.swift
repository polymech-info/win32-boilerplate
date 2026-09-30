//
//  UtilityAreaTab.swift
//  CodeEdit
//
//  Created by Wouter Hennen on 02/06/2023.
//

import SwiftUI

enum UtilityAreaTab: WorkspacePanelTab, CaseIterable {
    var id: Self { self }

    case terminal
    case debugConsole
    case output
    case polymech
    case log

    var title: String {
        switch self {
        case .terminal:
            return "Terminal"
        case .debugConsole:
            return "Debug Console"
        case .output:
            return "Output"
        case .polymech:
            return "Polymech"
        case .log:
            return "Log"
        }
    }

    var systemImage: String {
        switch self {
        case .terminal:
            return "terminal"
        case .debugConsole:
            return "ladybug"
        case .output:
            return "list.bullet.indent"
        case .polymech:
            return "photo.stack"
        case .log:
            return "doc.text.magnifyingglass"
        }
    }

    var body: some View {
        switch self {
        case .terminal:
            UtilityAreaTerminalView()
        case .debugConsole:
            UtilityAreaDebugView()
        case .output:
            UtilityAreaOutputView()
        case .polymech:
            PolymechImageQueueView()
        case .log:
            PolymechLogView()
        }
    }
}
