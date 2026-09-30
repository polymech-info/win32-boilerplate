//
//  OpenQuicklyPreviewView.swift
//  CodeEditModules/QuickOpen
//
//  Created by Pavel Kasila on 20.03.22.
//

import SwiftUI

struct OpenQuicklyPreviewView: View {

    private let item: CEWorkspaceFile

    init(item: CEWorkspaceFile) {
        self.item = item
    }

    var body: some View {
        PolymechFileViewerView(url: item.url)
    }
}
