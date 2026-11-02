import CoreGraphics

/// Builds the CoreGraphics events that injection posts. Building and posting are separate so the
/// events can be checked in tests without moving the real cursor.
struct EventFactory {
    let source: CGEventSource?

    func pointerMove(to point: CGPoint, delta: CGVector, heldButton: MouseButton?, flags: CGEventFlags) -> CGEvent? {
        let type: CGEventType
        switch heldButton {
        case nil: type = .mouseMoved
        case .left: type = .leftMouseDragged
        case .right: type = .rightMouseDragged
        case .middle, .other: type = .otherMouseDragged
        }
        let button = CGMouseButton(rawValue: UInt32(heldButton?.number ?? 0)) ?? .left
        guard
            let event = CGEvent(
                mouseEventSource: source, mouseType: type, mouseCursorPosition: point, mouseButton: button)
        else {
            return nil
        }
        // Games and 3D apps read the deltas rather than the position.
        event.setDoubleValueField(.mouseEventDeltaX, value: delta.dx)
        event.setDoubleValueField(.mouseEventDeltaY, value: delta.dy)
        event.flags = flags
        return event
    }

    func button(
        _ button: MouseButton, isDown: Bool, at point: CGPoint, clickCount: Int, flags: CGEventFlags
    ) -> CGEvent? {
        let type: CGEventType
        switch button {
        case .left: type = isDown ? .leftMouseDown : .leftMouseUp
        case .right: type = isDown ? .rightMouseDown : .rightMouseUp
        case .middle, .other: type = isDown ? .otherMouseDown : .otherMouseUp
        }
        let cgButton = CGMouseButton(rawValue: UInt32(button.number)) ?? .center
        guard
            let event = CGEvent(
                mouseEventSource: source, mouseType: type, mouseCursorPosition: point, mouseButton: cgButton)
        else {
            return nil
        }
        event.setIntegerValueField(.mouseEventButtonNumber, value: Int64(button.number))
        event.setIntegerValueField(.mouseEventClickState, value: Int64(clickCount))
        event.flags = flags
        return event
    }

    func scroll(_ delta: ScrollDelta, flags: CGEventFlags) -> CGEvent? {
        guard
            let event = CGEvent(
                scrollWheelEvent2Source: source, units: delta.unit == .pixel ? .pixel : .line, wheelCount: 2,
                wheel1: Int32(delta.y.rounded()), wheel2: Int32(delta.x.rounded()), wheel3: 0)
        else {
            return nil
        }
        // Point deltas are whole points, as trackpads report them; fractional lines from smooth
        // wheels travel in the fixed-point fields.
        switch delta.unit {
        case .pixel:
            event.setIntegerValueField(.scrollWheelEventIsContinuous, value: 1)
            event.setDoubleValueField(.scrollWheelEventPointDeltaAxis1, value: delta.y)
            event.setDoubleValueField(.scrollWheelEventPointDeltaAxis2, value: delta.x)
        case .line:
            event.setDoubleValueField(.scrollWheelEventFixedPtDeltaAxis1, value: delta.y)
            event.setDoubleValueField(.scrollWheelEventFixedPtDeltaAxis2, value: delta.x)
        }
        event.setIntegerValueField(.scrollWheelEventScrollPhase, value: delta.phase.cgValue)
        event.setIntegerValueField(.scrollWheelEventMomentumPhase, value: delta.momentum.cgValue)
        event.flags = flags
        return event
    }

    func key(keyCode: UInt16, isDown: Bool, isRepeat: Bool, flags: CGEventFlags) -> CGEvent? {
        guard let event = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: isDown) else {
            return nil
        }
        event.setIntegerValueField(.keyboardEventAutorepeat, value: isRepeat ? 1 : 0)
        event.flags = flags
        return event
    }

    /// A modifier key change. macOS reports modifier keys as flag changes, never as key presses.
    func modifierChange(keyCode: UInt16, flags: CGEventFlags) -> CGEvent? {
        guard let event = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: true) else {
            return nil
        }
        event.type = .flagsChanged
        event.flags = flags
        return event
    }
}
