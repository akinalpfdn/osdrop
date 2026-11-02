import AppKit
import Carbon.HIToolbox
import Testing

@testable import Osdrop

/// Turns Secure Event Input on in this process for a moment and checks that Osdrop detects it. It
/// never touches the cursor or the keyboard, but while it runs no event tap sees keystrokes.
@MainActor
@Suite("Secure Event Input on real hardware")
struct SecureInputHardwareTests {
    @Test("Secure Event Input is detected; an owner is named only when it is the frontmost app")
    func detectsAndNamesCarefully() async throws {
        #expect(!SystemSecureInput().current().isEnabled, "Another app already holds Secure Event Input")
        EnableSecureEventInput()
        defer { DisableSecureEventInput() }
        try await Task.sleep(for: .milliseconds(200))

        let status = SystemSecureInput().current()
        #expect(status.isEnabled)
        if let owner = status.ownerProcessID {
            #expect(owner == NSWorkspace.shared.frontmostApplication?.processIdentifier)
        }
    }
}
