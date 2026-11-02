import AppKit
import CoreGraphics
import os

/// Injects input with CoreGraphics events posted at the HID level. Every event carries Osdrop's
/// tag in its source user data, so Osdrop's own event tap can tell it from physical input.
@MainActor
final class CGEventInjector: InputInjecting {
    private let factory: EventFactory
    private let poster: any EventPosting
    private let layout: () -> KeyboardPhysicalLayout
    private let now: () -> TimeInterval

    private(set) var cursorLocation: CGPoint
    private(set) var droppedKeyCount = 0
    private var modifiers: ModifierState = []
    private var heldKeys: [HIDKey] = []
    private var heldButtons: [MouseButton] = []
    private var clickCounter: ClickCounter
    private var clickCounts: [MouseButton: Int] = [:]

    init(
        poster: any EventPosting = HIDEventPoster(),
        source: CGEventSource? = CGEventInjector.makeSource(),
        cursorLocation: CGPoint = CGEventInjector.currentCursorLocation(),
        doubleClickInterval: TimeInterval = NSEvent.doubleClickInterval,
        layout: @escaping () -> KeyboardPhysicalLayout = KeyboardPhysicalLayout.current,
        now: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime }
    ) {
        self.factory = EventFactory(source: source)
        self.poster = poster
        self.cursorLocation = cursorLocation
        self.clickCounter = ClickCounter(interval: doubleClickInterval, radius: Tunables.Input.clickRadius)
        self.layout = layout
        self.now = now
    }

    /// An event source tagged as Osdrop's, with no suppression of local events after injection.
    nonisolated static func makeSource() -> CGEventSource? {
        let source = CGEventSource(stateID: .hidSystemState)
        source?.userData = Tunables.Input.injectedEventTag
        source?.localEventsSuppressionInterval = 0
        return source
    }

    nonisolated static func currentCursorLocation() -> CGPoint {
        CGEvent(source: nil)?.location ?? .zero
    }

    func moveCursor(to point: CGPoint) {
        let delta = CGVector(dx: point.x - cursorLocation.x, dy: point.y - cursorLocation.y)
        cursorLocation = point
        post(factory.pointerMove(to: point, delta: delta, heldButton: dragButton, flags: flags))
    }

    func button(_ button: MouseButton, isDown: Bool, at point: CGPoint) {
        if point != cursorLocation {
            moveCursor(to: point)
        }
        let count: Int
        if isDown {
            count = clickCounter.press(button, at: point, time: now())
            clickCounts[button] = count
            if !heldButtons.contains(button) {
                heldButtons.append(button)
            }
        } else {
            guard heldButtons.contains(button) else { return }
            count = clickCounts[button] ?? 1
            heldButtons.removeAll { $0 == button }
        }
        post(factory.button(button, isDown: isDown, at: point, clickCount: count, flags: flags))
    }

    func scroll(_ delta: ScrollDelta) {
        post(factory.scroll(delta, flags: flags))
    }

    func key(_ key: HIDKey, _ action: KeyAction) {
        if key.page == HIDKey.consumerPage {
            mediaKey(key, action)
            return
        }
        guard let keyCode = MacKeyMap.keyCode(for: key, layout: layout()) else {
            droppedKeyCount += 1
            return
        }
        if let entry = ModifierKeys.entry(for: key) {
            modifier(entry, keyCode: keyCode, isDown: action != .up)
            return
        }
        switch action {
        case .down, .repeated:
            if !heldKeys.contains(key) {
                heldKeys.append(key)
            }
        case .up:
            guard heldKeys.contains(key) else { return }
            heldKeys.removeAll { $0 == key }
        }
        post(factory.key(keyCode: keyCode, isDown: action != .up, isRepeat: action == .repeated, flags: flags))
    }

    func releaseAll() {
        for key in heldKeys.reversed() {
            self.key(key, .up)
        }
        for entry in ModifierKeys.entries where modifiers.contains(entry.state) {
            self.key(.keyboard(entry.usage), .up)
        }
        for button in heldButtons.reversed() {
            self.button(button, isDown: false, at: cursorLocation)
        }
        clickCounter.reset()
    }

    private var flags: CGEventFlags {
        ModifierKeys.flags(for: modifiers)
    }

    /// The button a pointer movement drags with: left first, as macOS reports drags.
    private var dragButton: MouseButton? {
        heldButtons.contains(.left) ? .left : heldButtons.first
    }

    private func modifier(_ entry: ModifierKeys.Entry, keyCode: UInt16, isDown: Bool) {
        guard modifiers.contains(entry.state) != isDown else { return }
        if isDown {
            modifiers.insert(entry.state)
        } else {
            modifiers.remove(entry.state)
        }
        post(factory.modifierChange(keyCode: keyCode, flags: flags))
    }

    private func mediaKey(_ key: HIDKey, _ action: KeyAction) {
        guard let event = MediaKeys.makeEvent(for: key, isDown: action != .up) else {
            droppedKeyCount += 1
            return
        }
        event.setIntegerValueField(.eventSourceUserData, value: Tunables.Input.injectedEventTag)
        poster.post(event)
    }

    private func post(_ event: CGEvent?) {
        guard let event else {
            Log.input.error("Could not create an input event to inject")
            return
        }
        poster.post(event)
    }
}
