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
    /// As `button(_:isDown:at:)`, with the click count the controller counted, so network delay
    /// never splits a double click (docs/PROTOCOL.md "0x24 ButtonEvent").
    func button(_ button: MouseButton, isDown: Bool, at point: CGPoint, clickCount: Int)
    func scroll(_ delta: ScrollDelta)
    func key(_ key: HIDKey, _ action: KeyAction)
    /// Releases every key and button this injector pressed. Called on every way out of control
    /// (DEVPLAN, input safety invariants).
    func releaseAll()
    /// As `releaseAll()`, but a left button still down stays down: core released everything it
    /// pressed before the control ended, so a left button it left down holds this Mac's drag,
    /// which waits at the edge for its outcome (DEVPLAN Phase 14) and is released there.
    func releaseAllButDrag()
    /// Releases keys that went down on this Mac before its input was captured, whether or not this
    /// injector pressed them: their physical release now goes to another computer.
    func releaseHere(_ keys: [HIDKey])
    /// Puts the cursor at `point` without an input event (the cursor came back from another
    /// computer).
    func placeCursor(at point: CGPoint)
    /// Ends a drag that waits at `point` with the left button down, whoever pressed it: a small
    /// dragged movement there, so the drop target under it is entered, then the release.
    func releaseDrag(at point: CGPoint)
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
