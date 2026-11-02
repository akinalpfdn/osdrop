import AppKit
import ApplicationServices
import Testing

@testable import Osdrop

/// Drives a window of the test's own with injected input and checks what AppKit received: clicks,
/// double clicks, typing with a modifier, scrolling, dragging a window and typing into a password
/// field. Runs only through the `OsdropHardware` scheme; nobody may touch the mouse meanwhile.
@MainActor
@Suite(
    "Injection into a real window", .serialized,
    .enabled(if: AXIsProcessTrusted(), "Osdrop needs Accessibility access"))
struct InjectionWindowHardwareTests {
    /// HID keyboard usages of the digit row and space, the same keys on every layout this Mac uses.
    private enum Usage {
        static let one: UInt16 = 0x1E
        static let digits: [UInt16] = [0x1E, 0x1F, 0x20, 0x21, 0x22]  // 1 2 3 4 5
        static let space: UInt16 = 0x2C
        static let leftShift: UInt16 = 0xE1
    }

    private let window = TestWindow()
    private let injector = CGEventInjector()
    private let originalCursor = CGEvent(source: nil)?.location ?? .zero

    init() {
        window.show()
    }

    private func settle() async throws {
        try await Task.sleep(for: .milliseconds(300))
    }

    /// Stops the test before any key is sent unless the test window has keyboard focus. Without
    /// focus, injected keys would be typed into whatever app the user is working in.
    private func requireFocus() async throws {
        try await settle()
        try #require(
            NSApp.isActive && window.window.isKeyWindow,
            "The test window has no keyboard focus; nothing was typed. \(window.deliveredEvents) events delivered")
    }

    /// Stops the test before any mouse event is sent unless the test window is the topmost window
    /// at that point. A window just ordered front can take a moment to appear; until it does, a
    /// click, scroll or drag would reach the app underneath.
    private func requireOwnWindow(at point: CGPoint) async throws {
        for _ in 0..<40 where !window.isTopmost(at: point) {
            try await Task.sleep(for: .milliseconds(50))
        }
        try #require(
            window.isTopmost(at: point),
            "The test window is not the topmost window at \(point); no mouse event was sent")
    }

    private func type(_ usages: [UInt16]) {
        for usage in usages {
            injector.key(.keyboard(usage), .down)
            injector.key(.keyboard(usage), .up)
        }
    }

    private func click(at point: CGPoint) {
        injector.button(.left, isDown: true, at: point)
        injector.button(.left, isDown: false, at: point)
    }

    private func finish() {
        injector.releaseAll()
        injector.moveCursor(to: originalCursor)
        window.close()
    }

    @Test("A click focuses the text view, typing reaches it, Shift changes the character")
    func clickAndType() async throws {
        defer { finish() }
        let textPoint = window.point(in: window.textView, at: NSPoint(x: 20, y: 20))
        try await requireOwnWindow(at: textPoint)
        click(at: textPoint)
        try await requireFocus()
        window.textView.string = ""
        type(Usage.digits)
        injector.key(.keyboard(Usage.leftShift), .down)
        type([Usage.one])
        injector.key(.keyboard(Usage.leftShift), .up)
        try await settle()
        #expect(window.textView.string == "12345!", "\(window.deliveredEvents) events delivered")
    }

    @Test("A double click selects the word under the cursor")
    func doubleClickSelectsWord() async throws {
        defer { finish() }
        let textPoint = window.point(in: window.textView, at: NSPoint(x: 20, y: 20))
        try await requireOwnWindow(at: textPoint)
        click(at: textPoint)
        try await requireFocus()
        window.textView.string = ""
        type(Usage.digits + [Usage.space] + Usage.digits)
        try await settle()
        let word = try #require(window.pointOfCharacters(NSRange(location: 1, length: 1)))
        try await requireOwnWindow(at: word)
        click(at: word)
        click(at: word)
        try await settle()
        #expect(
            window.textView.selectedRange() == NSRange(location: 0, length: 5),
            "\(window.deliveredEvents) events delivered")
    }

    @Test("Scrolling over the text view scrolls it")
    func scroll() async throws {
        defer { finish() }
        window.textView.string = (1...200).map { "line \($0)" }.joined(separator: "\n")
        let before = window.scrollView.contentView.bounds.origin.y
        let scrollPoint = window.point(in: window.textView, at: NSPoint(x: 40, y: 40))
        try await requireOwnWindow(at: scrollPoint)
        injector.moveCursor(to: scrollPoint)
        for _ in 0..<5 {
            injector.scroll(ScrollDelta(x: 0, y: -5, unit: .line))
        }
        try await settle()
        #expect(
            window.scrollView.contentView.bounds.origin.y != before, "\(window.deliveredEvents) events delivered")
    }

    @Test("Dragging the title bar moves the window")
    func dragWindow() async throws {
        defer { finish() }
        let before = window.window.frame.origin
        let titleBar = window.titleBarPoint()
        try await requireOwnWindow(at: titleBar)
        injector.button(.left, isDown: true, at: titleBar)
        for step in 1...6 {
            injector.moveCursor(to: CGPoint(x: titleBar.x + Double(step * 10), y: titleBar.y + Double(step * 5)))
            try await Task.sleep(for: .milliseconds(30))
        }
        injector.button(.left, isDown: false, at: injector.cursorLocation)
        try await settle()
        let after = window.window.frame.origin
        #expect(abs(after.x - before.x - 60) <= 2, "\(window.deliveredEvents) events delivered")
        #expect(abs(before.y - after.y - 30) <= 2)
    }

    @Test("Typing reaches a password field, where Secure Event Input is on")
    func passwordField() async throws {
        defer { finish() }
        let fieldPoint = window.point(in: window.passwordField, at: NSPoint(x: 10, y: 8))
        try await requireOwnWindow(at: fieldPoint)
        click(at: fieldPoint)
        try await requireFocus()
        type(Usage.digits)
        try await settle()
        #expect(window.passwordField.stringValue == "12345", "\(window.deliveredEvents) events delivered")
    }
}

/// A floating window with a scrolling text view and a password field, placed on the main display.
@MainActor
private final class TestWindow {
    let window: NSWindow
    let scrollView = NSTextView.scrollableTextView()
    let passwordField = NSSecureTextField(frame: NSRect(x: 20, y: 20, width: 360, height: 24))
    /// Events AppKit delivered to this app while the window was open, to tell "nothing arrived"
    /// apart from "arrived but did something else" when a test fails.
    private(set) var deliveredEvents = 0
    private var monitor: Any?

    var textView: NSTextView {
        // scrollableTextView() always creates its document view as a text view.
        scrollView.documentView as? NSTextView ?? NSTextView()
    }

    init() {
        let screen = NSScreen.main?.visibleFrame ?? NSRect(x: 0, y: 0, width: 800, height: 600)
        let frame = NSRect(x: screen.midX - 200, y: screen.midY - 150, width: 400, height: 300)
        window = NSWindow(contentRect: frame, styleMask: [.titled], backing: .buffered, defer: false)
        window.title = "Osdrop hardware test"
        window.level = .floating
        window.isReleasedWhenClosed = false
        scrollView.frame = NSRect(x: 20, y: 60, width: 360, height: 220)
        textView.font = NSFont.monospacedSystemFont(ofSize: 14, weight: .regular)
        window.contentView?.addSubview(scrollView)
        window.contentView?.addSubview(passwordField)
    }

    func show() {
        monitor = NSEvent.addLocalMonitorForEvents(matching: .any) { [weak self] event in
            self?.deliveredEvents += 1
            return event
        }
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
    }

    func close() {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        window.orderOut(nil)
    }

    /// A point inside a view, in global display coordinates (origin at the top left of the
    /// primary display), as injected events need.
    func point(in view: NSView, at local: NSPoint) -> CGPoint {
        let onScreen = window.convertPoint(toScreen: view.convert(local, to: nil))
        return globalPoint(onScreen)
    }

    func titleBarPoint() -> CGPoint {
        let frame = window.frame
        let contentTop = window.contentLayoutRect.maxY
        let titleBarHeight = frame.height - contentTop
        return globalPoint(NSPoint(x: frame.midX, y: frame.maxY - titleBarHeight / 2))
    }

    /// The centre of a range of characters in the text view.
    func pointOfCharacters(_ range: NSRange) -> CGPoint? {
        guard let layoutManager = textView.layoutManager, let container = textView.textContainer else {
            return nil
        }
        let glyphs = layoutManager.glyphRange(forCharacterRange: range, actualCharacterRange: nil)
        var rect = layoutManager.boundingRect(forGlyphRange: glyphs, in: container)
        rect.origin.x += textView.textContainerOrigin.x
        rect.origin.y += textView.textContainerOrigin.y
        return point(in: textView, at: NSPoint(x: rect.midX, y: rect.midY))
    }

    /// Whether this window is the frontmost window under a point in global display coordinates.
    func isTopmost(at point: CGPoint) -> Bool {
        let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
        let screenPoint = NSPoint(x: point.x, y: primaryHeight - point.y)
        return NSWindow.windowNumber(at: screenPoint, belowWindowWithWindowNumber: 0) == window.windowNumber
    }

    private func globalPoint(_ screenPoint: NSPoint) -> CGPoint {
        let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
        return CGPoint(x: screenPoint.x, y: primaryHeight - screenPoint.y)
    }
}
