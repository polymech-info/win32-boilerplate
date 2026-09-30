//
//  PMUITestSupport.swift
//  pm-imageUITests
//
//  Shared helpers: screenshot paths, drag, env line for `run_uitest_screenshots.sh`.
//

import Foundation
import XCTest

enum PMUITestSupport {
    /// Default under `pm-image/build/uitest-screenshots/`; override with `PM_UI_TEST_SCREENSHOT_DIR` when propagated.
    static func screenshotOutputDirectory() -> URL {
        if let env = ProcessInfo.processInfo.environment["PM_UI_TEST_SCREENSHOT_DIR"], !env.isEmpty {
            return URL(fileURLWithPath: env, isDirectory: true)
        }
        return URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("build/uitest-screenshots", isDirectory: true)
    }

    /// Write PNG; tries project `build/uitest-screenshots` first, then `TMPDIR` (sandbox). Prints `PM_UI_TEST_SCREENSHOT_FILE=`.
    static func writePNG(data: Data, baseName: String) throws -> URL {
        let preferred = screenshotOutputDirectory().appendingPathComponent("\(baseName).png", isDirectory: false)
        do {
            try FileManager.default.createDirectory(
                at: preferred.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try data.write(to: preferred, options: .atomic)
            print("PM_UI_TEST_SCREENSHOT_FILE=\(preferred.path)")
            return preferred
        } catch {
            let tempDir = FileManager.default.temporaryDirectory
                .appendingPathComponent("pm-image-uitest", isDirectory: true)
            try FileManager.default.createDirectory(at: tempDir, withIntermediateDirectories: true)
            let tempFile = tempDir.appendingPathComponent("\(baseName).png", isDirectory: false)
            try data.write(to: tempFile, options: .atomic)
            print("PM_UI_TEST_SCREENSHOT_FILE=\(tempFile.path)")
            return tempFile
        }
    }

    static func attachScreenshot(_ shot: XCUIScreenshot, name: String, to test: XCTestCase) {
        let a = XCTAttachment(screenshot: shot)
        a.name = name
        a.lifetime = .keepAlways
        test.add(a)
    }

    /// Drags from the center of `el` by the given offset in **points** (emulates titlebar undock / splitter drag).
    static func pressDrag(from el: XCUIElement, offset dx: CGFloat, _ dy: CGFloat) {
        let s = el.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        let e = s.withOffset(CGVector(dx: dx, dy: dy))
        s.press(forDuration: 0.12, thenDragTo: e)
    }

    /// **Not used for default UI tests:** XCUITest `Window` hit tests resolve to **child views**; the bottom-right
    /// “corner” usually lands on **split / panel** chrome, not `NSWindow`’s `NSThemeFrame` resize handle — drags don’t resize.
    /// Prefer `launchEnvironment["PM_UITEST_BUMP_SIZE"] = "1"` in the app for a real `setContentSize` bump.
    static func dragResizeMainWindowBottomRightGrow(_ main: XCUIElement) {
        let corner = main.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.98))
        let dest = corner.withOffset(CGVector(dx: 100, dy: -72))
        corner.press(forDuration: 0.15, thenDragTo: dest)
    }
}

enum PMUITestA11y {
    static let mainWindow = "pm-main-window"
    static func titlebar(_ id: String) -> String { "pm-titlebar-\(id)" }
    static let dockRoot = "pm-dock-root"
    static let dockCenter = "pm-dock-center"
}
