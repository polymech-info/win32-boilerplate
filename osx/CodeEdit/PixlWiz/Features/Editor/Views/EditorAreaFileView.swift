//
//  EditorAreaFileView.swift
//  CodeEdit
//
//  Created by Pavel Kasila on 20.03.22.
//

import SwiftUI

struct EditorAreaFileView: View {

    @Environment(\.edgeInsets)
    private var edgeInsets

    var codeFile: CodeFileDocument

    @ViewBuilder var editorAreaFileView: some View {
        if let fileURL = codeFile.fileURL,
           codeFile.usesPolymechWebChatEmbed || PolymechWebChat.isPolymechChatFileURLForWebView(fileURL)
        {
            PolymechWebChatContainerView(fileURL: fileURL)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(.top, edgeInsets.top - 1.74)
                .padding(.bottom, StatusBarView.height + 1.26)
        } else if let fileURL = codeFile.fileURL {
            PolymechFileViewerView(url: fileURL)
                .padding(.top, edgeInsets.top - 1.74)
                .padding(.bottom, StatusBarView.height + 1.26)
        } else {
            CEContentUnavailableView("Cannot open file")
        }
    }

    var body: some View {
        editorAreaFileView
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
