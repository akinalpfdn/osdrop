import Carbon.HIToolbox
import CoreGraphics
import Testing

@testable import Osdrop

@Suite("Mac key map")
struct MacKeyMapTests {
    @Test("Key codes and usages are each used once")
    func entriesAreUnique() {
        let keyCodes = MacKeyMap.ansiPairs.map(\.keyCode)
        let usages = MacKeyMap.ansiPairs.map(\.usage)
        #expect(Set(keyCodes).count == keyCodes.count)
        #expect(Set(usages).count == usages.count)
    }

    @Test("Every ANSI pair converts both ways", arguments: [KeyboardPhysicalLayout.ansi, .jis])
    func roundTrip(layout: KeyboardPhysicalLayout) {
        for pair in MacKeyMap.ansiPairs {
            let key = MacKeyMap.hidKey(forKeyCode: UInt16(pair.keyCode), layout: layout)
            #expect(key == .keyboard(pair.usage))
            #expect(MacKeyMap.keyCode(for: .keyboard(pair.usage), layout: layout) == UInt16(pair.keyCode))
        }
    }

    @Test("ISO keyboards swap the key left of 1 and the key left of Z")
    func isoSwap() {
        #expect(MacKeyMap.hidKey(forKeyCode: UInt16(kVK_ISO_Section), layout: .iso) == .keyboard(0x35))
        #expect(MacKeyMap.hidKey(forKeyCode: UInt16(kVK_ANSI_Grave), layout: .iso) == .keyboard(0x64))
        #expect(MacKeyMap.keyCode(for: .keyboard(0x35), layout: .iso) == UInt16(kVK_ISO_Section))
        #expect(MacKeyMap.keyCode(for: .keyboard(0x64), layout: .iso) == UInt16(kVK_ANSI_Grave))
        #expect(MacKeyMap.hidKey(forKeyCode: UInt16(kVK_ANSI_A), layout: .iso) == .keyboard(0x04))
    }

    @Test("Fn stays local")
    func functionKeyHasNoUsage() {
        #expect(MacKeyMap.hidKey(forKeyCode: UInt16(kVK_Function), layout: .ansi) == nil)
    }

    @Test("The ISO hash key lands on the backslash position")
    func nonUSHash() {
        #expect(MacKeyMap.keyCode(for: .keyboard(0x32), layout: .ansi) == UInt16(kVK_ANSI_Backslash))
    }

    @Test("Usages without a Mac key and other pages give no key code")
    func unknownUsages() {
        #expect(MacKeyMap.keyCode(for: .keyboard(0x70), layout: .ansi) == nil)  // F21
        #expect(MacKeyMap.keyCode(for: .consumer(0xCD), layout: .ansi) == nil)
    }
}

@Suite("Modifier keys")
struct ModifierKeysTests {
    @Test("Every combination of held modifiers survives a round trip through event flags")
    func flagsRoundTrip() {
        for raw in UInt8.min...UInt8.max {
            let state = ModifierState(rawValue: raw)
            #expect(ModifierKeys.state(from: ModifierKeys.flags(for: state)) == state)
        }
    }

    @Test("Flags carry both the shared flag and the side")
    func flagsForSides() {
        let flags = ModifierKeys.flags(for: [.leftShift, .rightCommand])
        #expect(flags.contains(.maskShift))
        #expect(flags.contains(.maskCommand))
        #expect(flags.rawValue & 0x0002 != 0)
        #expect(flags.rawValue & 0x0010 != 0)
        #expect(!flags.contains(.maskControl))
    }

    @Test("A shared flag without a side counts as the left key")
    func sharedFlagOnly() {
        #expect(ModifierKeys.state(from: .maskShift) == .leftShift)
        #expect(ModifierKeys.state(from: [.maskControl, .maskAlternate]) == [.leftControl, .leftOption])
    }

    @Test("Each modifier usage has its entry")
    func entries() {
        #expect(ModifierKeys.entry(for: .keyboard(0xE5))?.state == .rightShift)
        #expect(ModifierKeys.entry(for: .keyboard(0x04)) == nil)
        #expect(ModifierKeys.entry(for: .consumer(0xE5)) == nil)
    }
}

@Suite("Media keys")
struct MediaKeysTests {
    @Test("Key types and usages are each used once and convert both ways")
    func pairs() {
        #expect(Set(MediaKeys.pairs.map(\.keyType)).count == MediaKeys.pairs.count)
        #expect(Set(MediaKeys.pairs.map(\.usage)).count == MediaKeys.pairs.count)
        for pair in MediaKeys.pairs {
            #expect(MediaKeys.keyType(for: .consumer(pair.usage)) == pair.keyType)
            #expect(MediaKeys.hidKey(forKeyType: pair.keyType) == .consumer(pair.usage))
        }
    }

    @Test("A built media key event decodes to the same key and action", arguments: [true, false])
    func eventRoundTrip(isDown: Bool) throws {
        let playPause = HIDKey.consumer(0xCD)
        let event = try #require(MediaKeys.makeEvent(for: playPause, isDown: isDown))
        let decoded = try #require(MediaKeys.decode(event))
        #expect(decoded.key == playPause)
        #expect(decoded.action == (isDown ? .down : .up))
    }

    @Test("Ordinary events are not media keys")
    func otherEvents() throws {
        let event = try #require(CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: true))
        #expect(MediaKeys.decode(event) == nil)
    }
}

@Suite("Click counter")
struct ClickCounterTests {
    @Test("Presses close in time and place count up")
    func doubleAndTripleClick() {
        var counter = ClickCounter(interval: 0.5, radius: 4)
        #expect(counter.press(.left, at: .zero, time: 1.0) == 1)
        #expect(counter.press(.left, at: CGPoint(x: 2, y: 2), time: 1.3) == 2)
        #expect(counter.press(.left, at: CGPoint(x: 2, y: 2), time: 1.6) == 3)
    }

    @Test("A slow, distant or different press starts over")
    func restarts() {
        var counter = ClickCounter(interval: 0.5, radius: 4)
        _ = counter.press(.left, at: .zero, time: 1.0)
        #expect(counter.press(.left, at: .zero, time: 1.6) == 1)
        #expect(counter.press(.left, at: CGPoint(x: 10, y: 0), time: 1.7) == 1)
        #expect(counter.press(.right, at: CGPoint(x: 10, y: 0), time: 1.8) == 1)
        counter.reset()
        #expect(counter.press(.right, at: CGPoint(x: 10, y: 0), time: 1.9) == 1)
    }
}
