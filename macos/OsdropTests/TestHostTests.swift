import Testing

@testable import Osdrop

/// The unit tests run inside the app; there it must not start Osdrop, which would join the user's
/// group with this Mac's identity and empty the running Osdrop's drag staging folder.
@MainActor
@Suite("Test host")
struct TestHostTests {
    @Test("The app knows it hosts the tests and starts no Osdrop: no menu bar icon")
    func startsNoOsdrop() {
        #expect(TestHost.isRunning)
        #expect(MenuBarIcon.statusItem() == nil)
    }
}
