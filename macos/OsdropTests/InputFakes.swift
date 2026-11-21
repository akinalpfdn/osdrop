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
    private var chords = CaptureHotkeys()

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
    func setHotkeys(_ hotkeys: CaptureHotkeys) { lock.withLock { chords = hotkeys } }
    var hotkeys: CaptureHotkeys { lock.withLock { chords } }

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
        case releaseAllButDrag
        case releaseHere([HIDKey])
        case place(CGPoint)
        case releaseDrag(CGPoint)
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
    func releaseAllButDrag() { calls.append(.releaseAllButDrag) }
    func releaseHere(_ keys: [HIDKey]) { calls.append(.releaseHere(keys)) }

    func placeCursor(at point: CGPoint) {
        cursorLocation = point
        calls.append(.place(point))
    }

    func releaseDrag(at point: CGPoint) {
        cursorLocation = point
        calls.append(.releaseDrag(point))
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
        case settings(InputSettings)
        case speed(UInt32, DeviceID)
        case capsLock(Bool)
        case dragStarted([DraggedFile])
        case dragPromised
        case dragStopped
        case dropFinished(UInt32, DropOutcome)
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
    func setInputSettings(_ settings: InputSettings) { record(.settings(settings)) }
    func setPointerSpeed(_ permille: UInt32, for device: DeviceID) { record(.speed(permille, device)) }
    func setCapsLock(_ on: Bool) { record(.capsLock(on)) }
    func dragStarted(_ files: [DraggedFile]) -> UInt32? {
        record(.dragStarted(files))
        return files.isEmpty ? nil : 1
    }
    func dragPromised() { record(.dragPromised) }
    func dragStopped() { record(.dragStopped) }
    func dropFinished(_ dragID: UInt32, _ outcome: DropOutcome) { record(.dropFinished(dragID, outcome)) }

    private func record(_ call: Call) { lock.withLock { recorded.append(call) } }
}

/// Records what input control hands to drag and drop.
@MainActor
final class FakeDrags: DragCoordinating {
    enum Call: Equatable {
        case start
        case stop
        case arrived(DeviceID, UInt32, CGPoint, [ArrivingItem])
        case withdrawn(UInt32)
        case ended(UInt32, DropOutcome)
        case wanted
    }

    private(set) var calls: [Call] = []

    func start() { calls.append(.start) }
    func stop() { calls.append(.stop) }
    func arrived(source: DeviceID, dragID: UInt32, at point: CGPoint, items: [ArrivingItem]) async {
        calls.append(.arrived(source, dragID, point, items))
    }
    func withdrawn(_ dragID: UInt32) { calls.append(.withdrawn(dragID)) }
    func ended(_ dragID: UInt32, _ outcome: DropOutcome) { calls.append(.ended(dragID, outcome)) }
    func wanted() { calls.append(.wanted) }
}

/// Caps Lock in memory; tests flip it as the user would.
final class FakeCapsLock: CapsLockControlling, @unchecked Sendable {
    private let lock = NSLock()
    private var state = false
    private var sets: [Bool] = []

    var isOn: Bool { lock.withLock { state } }
    var setValues: [Bool] { lock.withLock { sets } }

    func set(_ on: Bool) {
        lock.withLock {
            state = on
            sets.append(on)
        }
    }

    /// The user pressed Caps Lock on this Mac.
    func toggleByUser() { lock.withLock { state.toggle() } }
}

/// Stored input settings in memory.
struct FakeInputPreferences: InputPreferencesStoring {
    var input = InputSettings()
    var pointerSpeeds: [DeviceID: UInt32] = [:]
    var hotkeys = CaptureHotkeys()
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
        cursor: FakeCursor = FakeCursor(), capsLock: FakeCapsLock = FakeCapsLock(),
        preferences: FakeInputPreferences = FakeInputPreferences()
    ) -> InputServices {
        InputServices(
            makeCapture: { capture }, makeInjector: { injector }, isTrusted: { trusted },
            cursorLocation: { cursor.location }, doubleClickInterval: { 0.5 }, keyboardLayout: { .ansi },
            capsLock: capsLock, preferences: preferences, makeDrags: { _, _, _ in FakeDrags() })
    }
}

/// No transfer ever reports.
struct NoTransfers: TransferFollowing {
    func watch(
        _ id: UInt64, progress: @escaping @Sendable (TransferProgress) -> Void,
        finished: @escaping @Sendable (Result<ReceivedItem, CoreError>) -> Void
    ) {}
}
