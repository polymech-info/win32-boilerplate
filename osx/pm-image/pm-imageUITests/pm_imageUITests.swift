//
//  pm_imageUITests.swift
//  pm-imageUITests
//
//  Created by mc007 on 24/4/26.
//

import XCTest

final class pm_imageUITests: XCTestCase {

    override func setUpWithError() throws {
        // Put setup code here. This method is called before the invocation of each test method in the class.

        // In UI tests it is usually best to stop immediately when a failure occurs.
        continueAfterFailure = false

        // In UI tests it’s important to set the initial state - such as interface orientation - required for your tests before they run. The setUp method is a good place to do this.
    }

    override func tearDownWithError() throws {
        // Put teardown code here. This method is called after the invocation of each test method in the class.
    }

    @MainActor
    func testExample() throws {
        let app = XCUIApplication()
        app.launch()
        _ = app.windows.firstMatch.waitForExistence(timeout: 8)
    }

    private static let launchScreenshotName = "pm-image-launch"

    /// Launches the app, waits for a window, captures a screenshot, saves `pm-image-launch.png` and an XCTAttachment, then quits the app.
    @MainActor
    func testLaunchSaveScreenshotAndClose() throws {
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(
            app.windows.firstMatch.waitForExistence(timeout: 12),
            "Expected at least one window (needs GUI session; headless ssh often fails here)"
        )
        let shot = app.screenshot()
        PMUITestSupport.attachScreenshot(shot, name: Self.launchScreenshotName, to: self)
        _ = try PMUITestSupport.writePNG(data: shot.pngRepresentation, baseName: Self.launchScreenshotName)

        app.terminate()
    }

    /// Titlebar drag to undock a docked panel (`PanelTitlebarDragView`), then full-app screenshot for review.
    @MainActor
    func testTitlebarDragUndockScreenshot() throws {
        let app = XCUIApplication()
        app.launch()
        let main = app.windows[PMUITestA11y.mainWindow]
        XCTAssertTrue(main.waitForExistence(timeout: 12), "Main window; needs GUI (pm-main-window)")

        // Query from `app` — window-scoped `descendants` can miss deep split content on macOS.
        let tbar = app.descendants(matching: .any).matching(
            NSPredicate(format: "identifier == %@", PMUITestA11y.titlebar("files"))
        ).firstMatch
        XCTAssertTrue(tbar.waitForExistence(timeout: 8), "pm-titlebar-files (PanelTitlebarDragView; label must not be a separate a11y leaf)")

        PMUITestSupport.pressDrag(from: tbar, offset: 220, 100)

        let name = "pm-image-after-titlebar-undock"
        let shot = app.screenshot()
        PMUITestSupport.attachScreenshot(shot, name: name, to: self)
        _ = try PMUITestSupport.writePNG(data: shot.pngRepresentation, baseName: name)
        app.terminate()
    }

    /// Larger main **frame** after a real `setContentSize` bump — **not** a fake corner drag: XCUITest hits **panel/split** views, not the window resize edge.
    /// The app checks `PM_UITEST_BUMP_SIZE` (see `WxxMainDockWindow.applyUITestContentSizeBumpIfNeeded()`).
    @MainActor
    func testResizeMainWindowDiagonalScreenshot() throws {
        let app = XCUIApplication()
        app.launchEnvironment["PM_UITEST_BUMP_SIZE"] = "1"
        app.launch()
        let main = app.windows[PMUITestA11y.mainWindow]
        XCTAssertTrue(main.waitForExistence(timeout: 12), "pm-main-window")
        // Bump runs `async` in the app; give split layout time to settle.
        RunLoop.current.run(until: Date().addingTimeInterval(0.75))

        let name = "pm-image-after-frame-diagonal-resize"
        let shot = app.screenshot()
        PMUITestSupport.attachScreenshot(shot, name: name, to: self)
        _ = try PMUITestSupport.writePNG(data: shot.pngRepresentation, baseName: name)
        app.terminate()
    }

    @MainActor
    func testLaunchPerformance() throws {
        // This measures how long it takes to launch your application.
        measure(metrics: [XCTApplicationLaunchMetric()]) {
            XCUIApplication().launch()
        }
    }
}
