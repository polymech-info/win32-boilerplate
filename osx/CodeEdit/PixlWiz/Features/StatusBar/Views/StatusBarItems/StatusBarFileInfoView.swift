//
//  StatusBarFileInfoView.swift
//  CodeEdit
//
//  Created by Paul Ebose on 2024/5/12.
//

import SwiftUI

/// Shows media information about the currently opened file.
///
/// This currently shows the file size and image dimensions, if available.
struct StatusBarFileInfoView: View {

    @EnvironmentObject private var statusBarViewModel: StatusBarViewModel

    private let dimensionsNumberStyle = IntegerFormatStyle<Int>(locale: Locale(identifier: "en_US")).grouping(.never)

    var body: some View {

        HStack(spacing: 15) {

            if let dimensions = statusBarViewModel.dimensions {
                let width = dimensionsNumberStyle.format(dimensions.width)
                let height = dimensionsNumberStyle.format(dimensions.height)

                Text("\(width) × \(height)")
            }

            if let zoomPct = statusBarViewModel.imagePreviewMagnification {
                Text("\(Int(round(zoomPct)))%")
            }

            if let fileSize = statusBarViewModel.fileSize {
                Text(fileSize.formatted(.byteCount(style: .memory)))
            }

            if statusBarViewModel.imagePreviewMagnification != nil {
                Button(String(localized: "StatusBar.imageFitToViewport", defaultValue: "Fit")) {
                    statusBarViewModel.requestImageViewportFit()
                }
                .buttonStyle(.plain)
                .tint(Color.accentColor)
                .help(String(localized: "StatusBar.imageFitToViewport.help", defaultValue: "Reset zoom so the full image fits in the viewport (status shows 100% when fitted)."))
            }

        }
        .font(statusBarViewModel.statusBarFont)
        .foregroundStyle(statusBarViewModel.foregroundStyle)
    }

}
