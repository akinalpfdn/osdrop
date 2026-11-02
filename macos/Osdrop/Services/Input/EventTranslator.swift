import Carbon.HIToolbox
import CoreGraphics

/// Turns CoreGraphics events from the event tap into platform-neutral input events. It keeps the
/// last modifier flags, because a modifier change only says which key changed, not whether it went
/// down or up.
struct EventTranslator {
    var layout: KeyboardPhysicalLayout
    private var modifiers: ModifierState = []

    init(layout: KeyboardPhysicalLayout) {
        self.layout = layout
    }

    mutating func translate(_ event: CGEvent) -> [InputEvent] {
        switch event.type {
        case .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged:
            return pointerMovement(event)
        case .leftMouseDown, .rightMouseDown, .otherMouseDown:
            return [.button(button(of: event), isDown: true)]
        case .leftMouseUp, .rightMouseUp, .otherMouseUp:
            return [.button(button(of: event), isDown: false)]
        case .scrollWheel:
            return scroll(event)
        case .keyDown, .keyUp:
            return key(event)
        case .flagsChanged:
            return modifierChange(event)
        default:
            return MediaKeys.decode(event).map { [.key($0.key, $0.action)] } ?? []
        }
    }

    private func pointerMovement(_ event: CGEvent) -> [InputEvent] {
        let dx = event.getDoubleValueField(.mouseEventDeltaX)
        let dy = event.getDoubleValueField(.mouseEventDeltaY)
        return dx == 0 && dy == 0 ? [] : [.pointerMoved(dx: dx, dy: dy)]
    }

    private func button(of event: CGEvent) -> MouseButton {
        switch event.type {
        case .leftMouseDown, .leftMouseUp: .left
        case .rightMouseDown, .rightMouseUp: .right
        default: MouseButton(number: Int(event.getIntegerValueField(.mouseEventButtonNumber)))
        }
    }

    private func scroll(_ event: CGEvent) -> [InputEvent] {
        let isContinuous = event.getIntegerValueField(.scrollWheelEventIsContinuous) != 0
        let delta = ScrollDelta(
            x: event.getDoubleValueField(
                isContinuous ? .scrollWheelEventPointDeltaAxis2 : .scrollWheelEventFixedPtDeltaAxis2),
            y: event.getDoubleValueField(
                isContinuous ? .scrollWheelEventPointDeltaAxis1 : .scrollWheelEventFixedPtDeltaAxis1),
            unit: isContinuous ? .pixel : .line,
            phase: ScrollPhase(cgValue: event.getIntegerValueField(.scrollWheelEventScrollPhase)),
            momentum: MomentumPhase(cgValue: event.getIntegerValueField(.scrollWheelEventMomentumPhase)))
        let isEmpty = delta.x == 0 && delta.y == 0 && delta.phase == .none && delta.momentum == .none
        return isEmpty ? [] : [.scroll(delta)]
    }

    private func key(_ event: CGEvent) -> [InputEvent] {
        let keyCode = UInt16(truncatingIfNeeded: event.getIntegerValueField(.keyboardEventKeycode))
        guard let key = MacKeyMap.hidKey(forKeyCode: keyCode, layout: layout) else { return [] }
        if event.type == .keyUp {
            return [.key(key, .up)]
        }
        let isRepeat = event.getIntegerValueField(.keyboardEventAutorepeat) != 0
        return [.key(key, isRepeat ? .repeated : .down)]
    }

    private mutating func modifierChange(_ event: CGEvent) -> [InputEvent] {
        let previous = modifiers
        modifiers = ModifierKeys.state(from: event.flags)
        let keyCode = UInt16(truncatingIfNeeded: event.getIntegerValueField(.keyboardEventKeycode))
        guard let key = MacKeyMap.hidKey(forKeyCode: keyCode, layout: layout) else { return [] }
        if key == .keyboard(ModifierKeys.capsLockUsage) {
            // macOS reports one change per Caps Lock press, when the lock toggles.
            return [.key(key, .down), .key(key, .up)]
        }
        guard let entry = ModifierKeys.entry(for: key) else { return [] }
        let wasDown = previous.contains(entry.state)
        let isDown = modifiers.contains(entry.state)
        return wasDown == isDown ? [] : [.key(key, isDown ? .down : .up)]
    }
}

extension ScrollPhase {
    init(cgValue: Int64) {
        switch cgValue {
        case Int64(CGScrollPhase.began.rawValue): self = .began
        case Int64(CGScrollPhase.changed.rawValue): self = .changed
        case Int64(CGScrollPhase.ended.rawValue): self = .ended
        case Int64(CGScrollPhase.cancelled.rawValue): self = .cancelled
        case Int64(CGScrollPhase.mayBegin.rawValue): self = .mayBegin
        default: self = .none
        }
    }

    var cgValue: Int64 {
        switch self {
        case .none: 0
        case .began: Int64(CGScrollPhase.began.rawValue)
        case .changed: Int64(CGScrollPhase.changed.rawValue)
        case .ended: Int64(CGScrollPhase.ended.rawValue)
        case .cancelled: Int64(CGScrollPhase.cancelled.rawValue)
        case .mayBegin: Int64(CGScrollPhase.mayBegin.rawValue)
        }
    }
}

extension MomentumPhase {
    init(cgValue: Int64) {
        switch cgValue {
        case Int64(CGMomentumScrollPhase.begin.rawValue): self = .began
        case Int64(CGMomentumScrollPhase.continuous.rawValue): self = .changed
        case Int64(CGMomentumScrollPhase.end.rawValue): self = .ended
        default: self = .none
        }
    }

    var cgValue: Int64 {
        switch self {
        case .none: Int64(CGMomentumScrollPhase.none.rawValue)
        case .began: Int64(CGMomentumScrollPhase.begin.rawValue)
        case .changed: Int64(CGMomentumScrollPhase.continuous.rawValue)
        case .ended: Int64(CGMomentumScrollPhase.end.rawValue)
        }
    }
}
