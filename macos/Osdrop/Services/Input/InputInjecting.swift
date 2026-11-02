import CoreGraphics

/// Applies input to this Mac as if its own mouse and keyboard were used. Buttons carry the cursor
/// position they belong to (DEVPLAN, input semantics), so their order against pointer movement
/// never matters.
@MainActor
protocol InputInjecting: AnyObject {
    /// Where the last injected event left the cursor, in global display points.
    var cursorLocation: CGPoint { get }
    /// Keys whose HID usage has no key on this Mac, dropped since launch.
    var droppedKeyCount: Int { get }

    func moveCursor(to point: CGPoint)
    func button(_ button: MouseButton, isDown: Bool, at point: CGPoint)
    func scroll(_ delta: ScrollDelta)
    func key(_ key: HIDKey, _ action: KeyAction)
    /// Releases every key and button this injector pressed. Called on every way out of control
    /// (DEVPLAN, input safety invariants).
    func releaseAll()
}

/// Posts events into the system. Separate from building them so tests can observe the events.
protocol EventPosting: Sendable {
    func post(_ event: CGEvent)
}

struct HIDEventPoster: EventPosting {
    func post(_ event: CGEvent) {
        event.post(tap: .cghidEventTap)
    }
}
