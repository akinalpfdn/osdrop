import Carbon.HIToolbox
import CoreGraphics
import Testing

@testable import Osdrop

@Suite("Event translator")
struct EventTranslatorTests {
    private func mouseEvent(_ type: CGEventType, button: CGMouseButton = .left) throws -> CGEvent {
        try #require(CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: .zero, mouseButton: button))
    }

    private func keyEvent(_ keyCode: Int, down: Bool = true) throws -> CGEvent {
        try #require(CGEvent(keyboardEventSource: nil, virtualKey: CGKeyCode(keyCode), keyDown: down))
    }

    private func flagsChanged(_ keyCode: Int, flags: CGEventFlags) throws -> CGEvent {
        let event = try keyEvent(keyCode)
        event.type = .flagsChanged
        event.flags = flags
        return event
    }

    @Test("Pointer movement reports its deltas, also while dragging")
    func pointerMovement() throws {
        var translator = EventTranslator(layout: .ansi)
        for type in [CGEventType.mouseMoved, .leftMouseDragged] {
            let event = try mouseEvent(type)
            event.setDoubleValueField(.mouseEventDeltaX, value: 3)
            event.setDoubleValueField(.mouseEventDeltaY, value: -2)
            #expect(translator.translate(event) == [.pointerMoved(dx: 3, dy: -2)])
        }
        #expect(translator.translate(try mouseEvent(.mouseMoved)).isEmpty)
    }

    @Test("Buttons report which button and direction")
    func buttons() throws {
        var translator = EventTranslator(layout: .ansi)
        #expect(translator.translate(try mouseEvent(.leftMouseDown)) == [.button(.left, isDown: true)])
        #expect(translator.translate(try mouseEvent(.rightMouseUp, button: .right)) == [.button(.right, isDown: false)])
        let other = try mouseEvent(.otherMouseDown, button: .center)
        other.setIntegerValueField(.mouseEventButtonNumber, value: 3)
        #expect(translator.translate(other) == [.button(.other(3), isDown: true)])
    }

    @Test("Trackpad scrolling reports points and phases; wheels report lines")
    func scrolling() throws {
        var translator = EventTranslator(layout: .ansi)
        let trackpad = try #require(
            CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 2, wheel1: 0, wheel2: 0, wheel3: 0))
        trackpad.setIntegerValueField(.scrollWheelEventIsContinuous, value: 1)
        trackpad.setDoubleValueField(.scrollWheelEventPointDeltaAxis1, value: 12)
        trackpad.setDoubleValueField(.scrollWheelEventPointDeltaAxis2, value: -4)
        trackpad.setIntegerValueField(.scrollWheelEventScrollPhase, value: Int64(CGScrollPhase.began.rawValue))
        #expect(
            translator.translate(trackpad) == [.scroll(ScrollDelta(x: -4, y: 12, unit: .pixel, phase: .began))])

        let wheel = try #require(
            CGEvent(scrollWheelEvent2Source: nil, units: .line, wheelCount: 1, wheel1: 1, wheel2: 0, wheel3: 0))
        #expect(translator.translate(wheel) == [.scroll(ScrollDelta(x: 0, y: 1, unit: .line))])
    }

    @Test("Keys report down, repeat and up as HID usages")
    func keys() throws {
        var translator = EventTranslator(layout: .ansi)
        #expect(translator.translate(try keyEvent(kVK_ANSI_A)) == [.key(.keyboard(0x04), .down)])
        let repeated = try keyEvent(kVK_ANSI_A)
        repeated.setIntegerValueField(.keyboardEventAutorepeat, value: 1)
        #expect(translator.translate(repeated) == [.key(.keyboard(0x04), .repeated)])
        #expect(translator.translate(try keyEvent(kVK_ANSI_A, down: false)) == [.key(.keyboard(0x04), .up)])
    }

    @Test("ISO keyboards report the key left of 1 as the grave usage")
    func isoKeys() throws {
        var translator = EventTranslator(layout: .iso)
        #expect(translator.translate(try keyEvent(kVK_ISO_Section)) == [.key(.keyboard(0x35), .down)])
    }

    @Test("Modifier changes become key presses of the side that changed")
    func modifiers() throws {
        var translator = EventTranslator(layout: .ansi)
        let rightShift = ModifierKeys.flags(for: .rightShift)
        #expect(
            translator.translate(try flagsChanged(kVK_RightShift, flags: rightShift)) == [.key(.keyboard(0xE5), .down)])
        let both = ModifierKeys.flags(for: [.rightShift, .leftCommand])
        #expect(translator.translate(try flagsChanged(kVK_Command, flags: both)) == [.key(.keyboard(0xE3), .down)])
        #expect(
            translator.translate(try flagsChanged(kVK_RightShift, flags: ModifierKeys.flags(for: .leftCommand)))
                == [.key(.keyboard(0xE5), .up)])
        #expect(translator.translate(try flagsChanged(kVK_Command, flags: [])) == [.key(.keyboard(0xE3), .up)])
    }

    @Test("Caps Lock reports a full press; Fn stays local")
    func lockAndFunctionKeys() throws {
        var translator = EventTranslator(layout: .ansi)
        #expect(
            translator.translate(try flagsChanged(kVK_CapsLock, flags: .maskAlphaShift)) == [
                .key(.keyboard(0x39), .down), .key(.keyboard(0x39), .up),
            ])
        #expect(translator.translate(try flagsChanged(kVK_Function, flags: .maskSecondaryFn)).isEmpty)
    }
}

@Suite("Event factory")
struct EventFactoryTests {
    private let factory = EventFactory(source: nil)

    @Test("A move with a held button is a drag of that button and carries its delta")
    func drag() throws {
        let event = try #require(
            factory.pointerMove(
                to: CGPoint(x: 10, y: 20), delta: CGVector(dx: 2, dy: 3), heldButton: .left, flags: .maskShift))
        #expect(event.type == .leftMouseDragged)
        #expect(event.location == CGPoint(x: 10, y: 20))
        #expect(event.getDoubleValueField(.mouseEventDeltaX) == 2)
        #expect(event.getDoubleValueField(.mouseEventDeltaY) == 3)
        #expect(event.flags.contains(.maskShift))
        #expect(
            try #require(factory.pointerMove(to: .zero, delta: .zero, heldButton: nil, flags: [])).type == .mouseMoved)
    }

    @Test("Buttons carry their number and click count")
    func buttons() throws {
        let event = try #require(factory.button(.other(4), isDown: true, at: .zero, clickCount: 2, flags: []))
        #expect(event.type == .otherMouseDown)
        #expect(event.getIntegerValueField(.mouseEventButtonNumber) == 4)
        #expect(event.getIntegerValueField(.mouseEventClickState) == 2)
        #expect(
            try #require(factory.button(.right, isDown: false, at: .zero, clickCount: 1, flags: [])).type
                == .rightMouseUp)
    }

    @Test("Pixel scrolling is continuous and keeps its phases")
    func scroll() throws {
        let delta = ScrollDelta(x: 3, y: -7, unit: .pixel, phase: .changed, momentum: .began)
        let event = try #require(factory.scroll(delta, flags: []))
        #expect(event.getIntegerValueField(.scrollWheelEventIsContinuous) == 1)
        #expect(event.getDoubleValueField(.scrollWheelEventPointDeltaAxis1) == -7)
        #expect(event.getDoubleValueField(.scrollWheelEventPointDeltaAxis2) == 3)
        #expect(event.getIntegerValueField(.scrollWheelEventScrollPhase) == ScrollPhase.changed.cgValue)
        #expect(event.getIntegerValueField(.scrollWheelEventMomentumPhase) == MomentumPhase.began.cgValue)
    }

    @Test("Keys mark repeats; modifiers are flag changes")
    func keys() throws {
        let repeated = try #require(factory.key(keyCode: 0, isDown: true, isRepeat: true, flags: []))
        #expect(repeated.type == .keyDown)
        #expect(repeated.getIntegerValueField(.keyboardEventAutorepeat) == 1)
        let shift = try #require(factory.modifierChange(keyCode: UInt16(kVK_Shift), flags: .maskShift))
        #expect(shift.type == .flagsChanged)
        #expect(shift.flags.contains(.maskShift))
    }
}

@Suite("Tap policy")
struct TapPolicyTests {
    @Test("Captured input is swallowed; observed and injected input passes")
    func decisions() {
        #expect(TapPolicy.decide(mode: .capture, isInjected: false) == .swallow)
        #expect(TapPolicy.decide(mode: .capture, isInjected: true) == .pass)
        #expect(TapPolicy.decide(mode: .observe, isInjected: false) == .pass)
    }

    @Test("The release chord needs Control, Option, Command and Escape")
    func releaseChord() {
        let all: CGEventFlags = [.maskControl, .maskAlternate, .maskCommand]
        let escape = Int64(kVK_Escape)
        #expect(TapPolicy.isReleaseChord(type: .keyDown, keyCode: escape, flags: all))
        #expect(TapPolicy.isReleaseChord(type: .keyDown, keyCode: escape, flags: all.union(.maskShift)))
        #expect(!TapPolicy.isReleaseChord(type: .keyUp, keyCode: escape, flags: all))
        #expect(!TapPolicy.isReleaseChord(type: .keyDown, keyCode: escape, flags: [.maskControl, .maskCommand]))
        #expect(!TapPolicy.isReleaseChord(type: .keyDown, keyCode: Int64(kVK_ANSI_A), flags: all))
    }
}

@Suite("Line scrolling")
struct LineScrollTests {
    @Test("Smooth wheels keep fractional lines")
    func fractionalLines() throws {
        let factory = EventFactory(source: nil)
        let event = try #require(factory.scroll(ScrollDelta(x: 0.5, y: -1.25, unit: .line), flags: []))
        #expect(event.getIntegerValueField(.scrollWheelEventIsContinuous) == 0)
        #expect(event.getDoubleValueField(.scrollWheelEventFixedPtDeltaAxis1) == -1.25)
        #expect(event.getDoubleValueField(.scrollWheelEventFixedPtDeltaAxis2) == 0.5)
    }
}
