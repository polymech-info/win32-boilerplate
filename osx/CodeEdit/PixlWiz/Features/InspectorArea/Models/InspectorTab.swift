//
//  InspectorTab.swift
//  CodeEdit
//
//  Created by Wouter Hennen on 02/06/2023.
//

import SwiftUI
import CodeEditKit
import ExtensionFoundation

enum InspectorTab: WorkspacePanelTab {
    case agent
    case polymech
    case internalDevelopment
    case uiExtension(endpoint: AppExtensionIdentity, data: ResolvedSidebar.SidebarStore)

    var systemImage: String {
        switch self {
        case .agent:
            return "message"
        case .polymech:
            // `photo.badge.gearshape` is not in the SF Symbol set on all macOS 14.x builds (SwiftUI: Invalid Configuration).
            return "photo.on.rectangle.angled"
        case .internalDevelopment:
            return "hammer"
        case .uiExtension(_, let data):
            return data.icon ?? "e.square"
        }
    }

    var id: String {
        if case .uiExtension(let endpoint, let data) = self {
            return endpoint.bundleIdentifier + data.sceneID
        }
        return title
    }

    var title: String {
        switch self {
        case .agent:
            return "Agent"
        case .polymech:
            return "Polymech"
        case .internalDevelopment:
            return "Internal Development"
        case .uiExtension(_, let data):
            return data.help ?? data.sceneID
        }
    }

    var body: some View {
        switch self {
        case .agent:
            AgentInspectorView()
        case .polymech:
            PolymechImageInspectorView()
        case .internalDevelopment:
            InternalDevelopmentInspectorView()
        case let .uiExtension(endpoint, data):
            ExtensionSceneView(with: endpoint, sceneID: data.sceneID)
        }
    }
}
