import SwiftUI

/// Starts Osdrop, or an empty app while Xcode runs the unit tests inside it (`TestHost`). Osdrop
/// itself would join the user's group with this Mac's identity, taking the PC's session from the
/// Osdrop already running, and empty that Osdrop's drag staging folder.
@main
@MainActor
enum OsdropMain {
    static func main() {
        if TestHost.isRunning {
            TestHostApp.main()
        } else {
            OsdropApp.main()
        }
    }
}

/// Hosts the unit tests: no menu bar icon, no group, no window.
private struct TestHostApp: App {
    var body: some Scene {
        Settings { EmptyView() }
    }
}
