//
//  StatusBarViewModel.swift
//  CodeEdit
//
//  Created by Paul Ebose on 2024/5/12.
//

import AppKit
import SwiftUI

final class StatusBarViewModel: ObservableObject {

    /// The file size of the currently opened file.
    @Published var fileSize: Int?

    /// The dimensions (width x height) of the currently opened image.
    @Published var dimensions: ImageDimensions?

    /// Zoom for the image editor as **percent of “fit entire image in viewport”** (100 = fitted, 200 = 2× closer than fit). `nil` when not previewing an image in the main editor.
    @Published var imagePreviewMagnification: CGFloat?

    /// Strong while the image tab is visible; cleared in ``clearImagePreviewZoomState`` so **Fit** always targets the live scroll view (a `weak` ref was often nil).
    var imagePreviewZoomHost: PolymechZoomableImageScrollView?

    /// Indicates whether the breakpoint is enabled or not.
    @Published var isBreakpointEnabled = true

    /// The font style of items shown in the status bar.
    private(set) var statusBarFont = Font.system(size: 11, weight: .medium)

    /// The color of the text shown in the status bar.
    private(set) var foregroundStyle = Color.secondary

    func requestImageViewportFit() {
        imagePreviewZoomHost?.applyViewportFit()
    }

    func clearImagePreviewZoomState() {
        imagePreviewZoomHost = nil
        imagePreviewMagnification = nil
    }
}
