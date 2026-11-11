import Carbon.HIToolbox
import CoreGraphics
import Foundation
import Testing

@testable import Osdrop

/// Records posted events instead of sending them to the system.
private final class RecordingPoster: EventPosting {
    private let posted = Locked<[CGEvent]>([])

    var events: [CGEvent] {
        posted.withLock { $0 }
    }

    func post(_ event: CGEvent) {
        posted.withLock { $0.append(event) }
    }

    func clear() {
        posted.withLock { $0.removeAll() }
    }
}

@MainActor
@Suite("Injector")
struct InjectorTests {
    private let poster = RecordingPoster()
    private let injector: CGEventInjector

    init() {
        let clock = Locked(10.0)
        injector = CGEventInjector(
            poster: poster, source: nil, cursorLocation: CGPoint(x: 100, y: 100), doubleClickInterval: 0.5,
            layout: { .ansi }, now: { clock.withLock { $0 } })
    }

    @Test("Moves carry the location and the delta from the previous position")
    func move() throws {
        injector.moveCursor(to: CGPoint(x: 110, y: 95))
        let event = try #require(poster.events.last)
        #expect(event.type == .mouseMoved)
        #expect(event.location == CGPoint(x: 110, y: 95))
        #expect(event.getDoubleValueField(.mouseEventDeltaX) == 10)
        #expect(event.getDoubleValueField(.mouseEventDeltaY) == -5)
        #expect(injector.cursorLocation == CGPoint(x: 110, y: 95))
    }

    @Test("A button at another point moves there first; a second quick click is a double click")
    func doubleClick() throws {
        let point = CGPoint(x: 200, y: 200)
        injector.button(.left, isDown: true, at: point)
        injector.button(.left, isDown: false, at: point)
        injector.button(.left, isDown: true, at: point)
        injector.button(.left, isDown: false, at: point)
        let types = poster.events.map(\.type)
        #expect(types == [.mouseMoved, .leftMouseDown, .leftMouseUp, .leftMouseDown, .leftMouseUp])
        let clickStates = poster.events.dropFirst().map { $0.getIntegerValueField(.mouseEventClickState) }
        #expect(clickStates == [1, 1, 2, 2])
    }

    @Test("Moving with a held button drags")
    func drag() throws {
        injector.button(.left, isDown: true, at: CGPoint(x: 100, y: 100))
        injector.moveCursor(to: CGPoint(x: 150, y: 100))
        #expect(poster.events.last?.type == .leftMouseDragged)
    }

    @Test("Held modifiers are in the flags of later events")
    func modifierFlags() throws {
        injector.key(.keyboard(0xE1), .down)  // left Shift
        injector.key(.keyboard(0x04), .down)  // A
        let events = poster.events
        #expect(events.first?.type == .flagsChanged)
        #expect(events.last?.type == .keyDown)
        #expect(events.last?.flags.contains(.maskShift) == true)
        #expect(events.last?.getIntegerValueField(.keyboardEventKeycode) == Int64(kVK_ANSI_A))
    }

    @Test("Release all lets go of every key, modifier and button exactly once")
    func releaseAll() throws {
        injector.key(.keyboard(0xE3), .down)  // left Command
        injector.key(.keyboard(0x04), .down)
        injector.button(.right, isDown: true, at: CGPoint(x: 100, y: 100))
        poster.clear()

        injector.releaseAll()
        let types = poster.events.map(\.type)
        #expect(types == [.keyUp, .flagsChanged, .rightMouseUp])
        #expect(poster.events.last?.flags.contains(.maskCommand) == false)

        poster.clear()
        injector.releaseAll()
        #expect(poster.events.isEmpty)
    }

    @Test("At the end of a control, a left button core left down holds a drag and stays down")
    func releaseAllButDrag() throws {
        injector.key(.keyboard(0x04), .down)
        injector.button(.left, isDown: true, at: CGPoint(x: 100, y: 100))
        injector.button(.right, isDown: true, at: CGPoint(x: 100, y: 100))
        poster.clear()

        injector.releaseAllButDrag()
        #expect(poster.events.map(\.type) == [.keyUp, .rightMouseUp])

        poster.clear()
        injector.releaseDrag(at: CGPoint(x: 100, y: 100))
        #expect(poster.events.last?.type == .leftMouseUp)
    }

    @Test("Releasing something not pressed posts nothing")
    func strayRelease() {
        injector.key(.keyboard(0x04), .up)
        injector.button(.left, isDown: false, at: CGPoint(x: 100, y: 100))
        injector.key(.keyboard(0xE1), .up)
        #expect(poster.events.isEmpty)
    }

    @Test("Usages with no Mac key are dropped and counted")
    func droppedKeys() {
        injector.key(.keyboard(0x70), .down)  // F21
        #expect(poster.events.isEmpty)
        #expect(injector.droppedKeyCount == 1)
    }

    @Test("Media keys are tagged system-defined events")
    func mediaKeys() throws {
        injector.key(.consumer(0xCD), .down)
        let event = try #require(poster.events.last)
        #expect(event.type.rawValue == MediaKeys.systemDefinedEventType)
        #expect(event.getIntegerValueField(.eventSourceUserData) == Tunables.Input.injectedEventTag)
    }
}
