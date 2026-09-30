import SwiftUI

/// **This** is the one `@main` for the `pm-image` target.
///
/// If you still have the old Xcode template with `WindowGroup { ContentView() }` and *no* adapter,
/// that SwiftUI app was the real entry: our `AppDelegate` in `WxxDockerKit.swift` never ran, so
/// you saw “nada” despite “Running pm-image.” Remove `ContentView.swift` from the target if it is
/// still listed in **Build Phases → Compile Sources**.
///
/// `NSApplicationDelegateAdaptor` hands lifecycle to `AppDelegate`. The dock UI is created by
/// `WxxDockAppLauncher.show()` in `WxxDockerKit.swift` (see `docs/osx-dockers.md`; same entry point
/// if you drop this `@main` and call the launcher from another `AppDelegate`).
///
/// **Debug:** In the Run scheme, set `PM_HELLO=1` or `PM_SMOKE=1` to force a plain test window. The
/// normal path prints a line from `applicationDidFinishLaunching` in `WxxDockerKit`.
@main
struct PmImageApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        // No `WindowGroup` — all UI is AppKit. `Settings` is a minimal valid scene on macOS.
        Settings {
            EmptyView()
        }
    }
}
