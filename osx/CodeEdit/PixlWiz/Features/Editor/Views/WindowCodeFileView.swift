//
//  WindowCodeFileView.swift
//  CodeEdit
//
//  Created by Khan Winter on 3/19/23.
//

import Foundation
import SwiftUI

/// View that fixes [#1158](https://github.com/CodeEditApp/CodeEdit/issues/1158)
/// # Should **not** be used other than in a single file window.
struct WindowCodeFileView: View {
    let fileURL: URL

    init(codeFile: CodeFileDocument) {
        self.fileURL = codeFile.fileURL ?? URL(fileURLWithPath: "")
    }

    var body: some View {
        PolymechFileViewerView(url: fileURL)
    }
}
