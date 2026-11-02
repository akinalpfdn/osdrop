import AppKit
import ApplicationServices
import CoreGraphics
import Testing

@testable import Osdrop

/// These tests move the real cursor and take over input for a moment, so they run only through the
/// `OsdropHardware` scheme, by hand, with Accessibility access granted to Osdrop (docs/TESTING.md).
@MainActor
@Suite("Input on real hardware", .serialized, .enabled(if: AXIsProcessTrusted(), "Osdrop needs Accessibility access"))
struct InputHardwareTests {
    private func cursorLocation() -> CGPoint {
        CGEvent(source: nil)?.location ?? .zero
    }

    private func mainScreenCenter() -> CGPoint {
        let frame = CGDisplayBounds(CGMainDisplayID())
        return CGPoint(x: frame.midX, y: frame.midY)
    }

    @Test("Injected moves put the cursor where they say")
    func injectedMove() async throws {
        let original = cursorLocation()
        let injector = CGEventInjector()
        let target = mainScreenCenter()
        injector.moveCursor(to: target)
        try await Task.sleep(for: .milliseconds(100))
        #expect(abs(cursorLocation().x - target.x) <= 1)
        #expect(abs(cursorLocation().y - target.y) <= 1)
        injector.moveCursor(to: original)
    }

    @Test("Injected input still moves the cursor while physical input is captured")
    func injectionDuringCapture() async throws {
        let original = cursorLocation()
        let capture = EventTapCapture()
        try capture.start(layout: .current())
        defer { capture.stop() }
        capture.setMode(.capture)
        #expect(capture.mode == .capture)

        let injector = CGEventInjector()
        let target = CGPoint(x: mainScreenCenter().x + 40, y: mainScreenCenter().y + 40)
        injector.moveCursor(to: target)
        capture.heartbeat()
        try await Task.sleep(for: .milliseconds(100))
        #expect(abs(cursorLocation().x - target.x) <= 1)

        capture.setMode(.observe)
        #expect(capture.mode == .observe)
        injector.moveCursor(to: original)
    }

    @Test("Capture ends by itself when nobody confirms it")
    func watchdogReleases() async throws {
        let capture = EventTapCapture()
        try capture.start(layout: .current())
        defer { capture.stop() }
        capture.setMode(.capture)
        try await Task.sleep(for: Tunables.Input.captureHeartbeatTimeout + .milliseconds(500))
        #expect(capture.mode == .observe)
    }
}

@MainActor
@Suite("Displays on real hardware")
struct ScreenHardwareTests {
    @Test("Every display AppKit knows is described, the main one with its CoreGraphics frame")
    func describesDisplays() throws {
        let screens = CGScreenDescriber().screens()
        #expect(screens.count == NSScreen.screens.count)
        let main = try #require(screens.first(where: \.isMain))
        #expect(main.frame == CGDisplayBounds(CGMainDisplayID()))
        #expect(main.scale >= 1)
        #expect(Set(screens.map(\.id)).count == screens.count)
    }
}
