//
//  NonTextFileView.swift
//  CodeEdit
//
//  Created by Paul Ebose on 2024/5/10.
//

import SwiftUI

/// Determines what type of file is passed in, and previews it accordingly.
///
/// ```swift
/// NonTextFileView(fileDocument)
/// ```
struct NonTextFileView: View {

    /// The file document you wish to open.
    let fileDocument: CodeFileDocument
    /// When set (main editor), image zoom / Fit in the status bar stay in sync with ``ImageFileView``.
    var statusBarForImagePreview: StatusBarViewModel? = nil

    var body: some View {
        Group {
            if let fileURL = fileDocument.fileURL {

                if let utType = fileDocument.utType {
                    if utType.conforms(to: .image) {
                        ImageFileView(fileURL, statusBarForImagePreview: statusBarForImagePreview)
                    } else if utType.conforms(to: .pdf) {
                        PDFFileView(fileURL)
                    } else {
                        AnyFileView(fileURL)
                    }
                } else {
                    AnyFileView(fileURL)
                }
            } else {
                ZStack {
                    Text("Cannot retrieve URL to the file you opened.")
                }
            }
        }
    }
}
