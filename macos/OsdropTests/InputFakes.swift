import CoreBridge
import CoreGraphics
import Foundation

@testable import Osdrop

/// An event tap in memory: tests feed it events and read its mode and heartbeats.
final class FakeCapture: InputCapturing, @unchecked Sendable {
    let events: AsyncStream<CaptureEvent>
    private let continuation: AsyncStream<CaptureEvent>.Continuation
    private let lock = NSLock()
    private var currentMode: CaptureMode = .observe
    private var running = false
    private var beats = 0

    init() {
        (events, continuation) = AsyncStream.makeStream(of: CaptureEvent.self)
    }

    var mode: CaptureMode { lock.withLock { currentMode } }
    var isRunning: Bool { lock.withLock { running } }
    var heartbeats: Int { lock.withLock { beats } }

    func start(layout: KeyboardPhysicalLayout) throws(CaptureError) { lock.withLock { running = true } }
    func stop() { lock.withLock { running = false } }
    func setMode(_ mode: CaptureMode) { lock.withLock { currentMode = mode } }
    func heartbeat() { lock.withLock { beats += 1 } }

    func send(_ event: CaptureEvent) { continuation.yield(event) }
}

/// Records what would be injected.
@MainActor
final class FakeInjector: InputInjecting {
    enum Call: Equatable {
        case move(CGPoint)
        case button(MouseButton, isDown: Bool, at: CGPoint, clickCount: Int?)
        case scroll(ScrollDelta)
        case key(HIDKey, KeyAction)
        case releaseAll
        case releaseHere([HIDKey])
        case place(CGPoint)
    }

    private(set) var calls: [Call] = []
    private(set) var cursorLocation: CGPoint = .zero
    let droppedKeyCount = 0

    func moveCursor(to point: CGPoint) {
        cursorLocation = point
        calls.append(.move(point))
    }

    func button(_ button: MouseButton, isDown: Bool, at point: CGPoint) {
        calls.append(.button(button, isDown: isDown, at: point, clickCount: nil))
    }

    func button(_ button: MouseButton, isDown: Bool, at point: CGPoint, clickCount: Int) {
        calls.append(.button(button, isDown: isDown, at: point, clickCount: clickCount))
    }

    func scroll(_ delta: ScrollDelta) { calls.append(.scroll(delta)) }
    func key(_ key: HIDKey, _ action: KeyAction) { calls.append(.key(key, action)) }
    func releaseAll() { calls.append(.releaseAll) }
    func releaseHere(_ keys: [HIDKey]) { calls.append(.releaseHere(keys)) }

    func placeCursor(at point: CGPoint) {
        cursorLocation = point
        calls.append(.place(point))
    }
}

/// Records input control's commands; tests deliver core's events.
final class FakeControlMesh: ControlDriving, @unchecked Sendable {
    enum Call: Equatable {
        case input(ControlInput)
        case returnHome(inputLost: Bool)
        case lock(Bool)
        case unavailable(ControlUnavailability?)
        case cursorMoved(CoordPoint)
    }

    let control: AsyncStream<ControlEvent>
    private let continuation: AsyncStream<ControlEvent>.Continuation
    private let lock = NSLock()
    private var recorded: [Call] = []

    init() {
        (control, continuation) = AsyncStream.makeStream(of: ControlEvent.self)
    }

    var calls: [Call] { lock.withLock { recorded } }

    func send(_ event: ControlEvent) { continuation.yield(event) }

    func input(_ input: ControlInput, ageMicroseconds: UInt64) { record(.input(input)) }
    func returnHome(inputLost: Bool) { record(.returnHome(inputLost: inputLost)) }
    func lockInput(_ locked: Bool) { record(.lock(locked)) }
    func setUnavailable(_ reason: ControlUnavailability?) { record(.unavailable(reason)) }
    func cursorMoved(to position: CoordPoint) { record(.cursorMoved(position)) }

    private func record(_ call: Call) { lock.withLock { recorded.append(call) } }
}

/// Where the fake system cursor is; tests move it as an application would.
@MainActor
final class FakeCursor {
    var location: CGPoint = .zero
}

extension InputServices {
    @MainActor
    static func fake(
        capture: FakeCapture = FakeCapture(), injector: FakeInjector = FakeInjector(), trusted: Bool = false,
        cursor: FakeCursor = FakeCursor()
    ) -> InputServices {
        InputServices(
            makeCapture: { capture }, makeInjector: { injector }, isTrusted: { trusted },
            cursorLocation: { cursor.location }, doubleClickInterval: { 0.5 }, keyboardLayout: { .ansi })
    }
}
