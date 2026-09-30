//
//  PolymechFileViewerView.swift
//  PixlWiz
//
//  Type-dispatching read-only file viewer.  Mirrors the priority order of
//  CFileViewer::OpenFile in src/win/ui_next/FileViewer.cpp.
//
//  PolymechViewerPageHost is held as @StateObject so it survives kind-switches
//  (image → markdown, etc.) without deallocating the WKWebView or killing the
//  WebContent process. viewer.html is loaded ONCE; file changes call setStatus.
//
//  Appearance: @AppSettings(\.general.appAppearance) mirrors the three-way
//  Dark / Light / System setting in General Settings — the same toggle that
//  drives NSApp.appearance. An onChange also reacts to the system-level
//  colorScheme so the viewer tracks automatic OS switches when "System" is set.
//

import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct PolymechFileViewerView: View {

    let url: URL

    /// Persistent WKWebView host — outlives kind-switches inside this view.
    @StateObject private var viewerHost = PolymechViewerPageHost()

    /// CodeEdit's own three-way appearance setting (Dark / Light / System).
    @AppSettings(\.general.appAppearance) var appAppearance

    /// Tracks macOS-level dark / light so we react when "System" mode follows the OS.
    @Environment(\.colorScheme) private var colorScheme

    private enum FileKind {
        case image, pdf, viewer, other
    }

    private func kind() -> FileKind {
        let ext = url.pathExtension.lowercased()
        // CAD formats must go to the web viewer regardless of UTType conformance.
        // macOS registers .dxf as conforming to UTType.image, which would route it
        // to ImageFileView (showing "cannot preview image"). Force .viewer first.
        switch ext {
        case "dxf", "step", "stp", "stl", "obj", "gltf", "glb", "ply", "3ds", "dae":
            return .viewer
        default:
            break
        }
        if let ut = UTType(filenameExtension: url.pathExtension) {
            if ut.conforms(to: .image) { return .image }
            if ut.conforms(to: .pdf)   { return .pdf }
        }
        if ext.isEmpty { return .other }   // no extension → QuickLook
        return .viewer                      // everything else → viewer.html
    }

    var body: some View {
        Group {
            switch kind() {
            case .image:
                ImageFileView(url)
            case .pdf:
                PDFFileView(url)
            case .viewer:
                PolymechViewerWebViewRef(host: viewerHost, url: url)
            case .other:
                AnyFileView(url)
            }
        }
        // Re-push theme whenever the explicit app setting or OS colour-scheme changes.
        .onChange(of: appAppearance) { viewerHost.refreshTheme() }
        .onChange(of: colorScheme)   { viewerHost.refreshTheme() }
    }
}
