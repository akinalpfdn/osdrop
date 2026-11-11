import CoreBridge
import CoreGraphics
import Foundation
import Observation

/// Why the cursor came back without being pushed back, shown in the menu until dismissed.
enum ControlNotice: Equatable {
    case refused(name: String, ControlRefusal)
    /// The other computer stopped answering or left while it had the cursor.
    case lost(name: String)
}

/// Motion latency on this Mac while another computer controls it, for the developer tools.
struct InputLatency: Equatable {
    let samples: UInt32
    let median: Duration
    let p99: Duration
}

/// What input control needs from the system, made when the mesh starts.
struct InputServices: Sendable {
    let makeCapture: @MainActor @Sendable () -> any InputCapturing
    let makeInjector: @MainActor @Sendable () -> any InputInjecting
    /// Whether macOS lets Osdrop watch and post input (Accessibility).
    let isTrusted: @MainActor @Sendable () -> Bool
    /// Where the system cursor is, in global display points.
    let cursorLocation: @MainActor @Sendable () -> CGPoint
    let doubleClickInterval: @MainActor @Sendable () -> TimeInterval
    let keyboardLayout: @MainActor @Sendable () -> KeyboardPhysicalLayout
    let capsLock: any CapsLockControlling
    let preferences: any InputPreferencesStoring
    /// Drag and drop across computers, with the input controller's injector and the transfers
    /// list.
    let makeDrags:
        @MainActor @Sendable (any ControlDriving & TransferDriving, any InputInjecting, (any DragTransfersTracking)?)
            -> any DragCoordinating
}

/// Cursor and keyboard across computers on this Mac (DEVPLAN Phase 11). Core decides crossings,
/// refusals and releases; this controller watches the Mac's input and hands all of it to core,
/// captures it while the cursor is elsewhere, injects what a controller sends, and keeps the
/// safety timers: the capture heartbeat, so a hung main thread gives the cursor back, and the
/// cursor reports of a controlled Mac.
@MainActor
@Observable
final class InputController {
    /// The computer this Mac's input goes to; nil while the cursor is home.
    private(set) var capturingFor: DeviceID?
    /// The computer controlling this Mac; nil when none.
    private(set) var controller: DeviceID?
    /// "Lock input here" (⌃⌥⌘L): the cursor stays on the computer it is on.
    private(set) var isLocked = false
    private(set) var notice: ControlNotice?
    private(set) var latency: [DeviceID: InputLatency] = [:]
    /// The event tap runs: a member is connected and macOS allows it (Accessibility).
    private(set) var isWatching = false

    @ObservationIgnored private let mesh: any ControlDriving
    @ObservationIgnored private let services: InputServices
    @ObservationIgnored private let capture: any InputCapturing
    @ObservationIgnored private let injector: any InputInjecting
    @ObservationIgnored private let drags: (any DragCoordinating)?
    @ObservationIgnored private let nameOf: @MainActor (DeviceID) -> String
    @ObservationIgnored private var tasks: [Task<Void, Never>] = []
    @ObservationIgnored private var heartbeat: Task<Void, Never>?
    @ObservationIgnored private var cursorWatch: Task<Void, Never>?
    @ObservationIgnored private var screenLocked = false
    @ObservationIgnored private var capsLockReported: Bool?
    /// Until this `DispatchTime` uptime, a Caps Lock event from the tap is taken for the echo of
    /// Osdrop setting Caps Lock itself, not for the user's key: on a controlled Mac it would end
    /// the control as local input.
    @ObservationIgnored private let capsEchoUntil = Locked<UInt64>(0)
    @ObservationIgnored private var isActive = false
    @ObservationIgnored private var isPermitted = false
    @ObservationIgnored private var unavailable: ControlUnavailability??

    /// `makeDrags` makes drag and drop with this controller's injector; none without it.
    init(
        mesh: any ControlDriving, services: InputServices,
        makeDrags: ((any InputInjecting) -> any DragCoordinating)? = nil,
        nameOf: @escaping @MainActor (DeviceID) -> String
    ) {
        self.mesh = mesh
        self.services = services
        self.capture = services.makeCapture()
        let injector = services.makeInjector()
        self.injector = injector
        self.drags = makeDrags?(injector)
        self.nameOf = nameOf
    }

    func start() {
        mesh.setInputSettings(services.preferences.input)
        for (device, permille) in services.preferences.pointerSpeeds {
            mesh.setPointerSpeed(permille, for: device)
        }
        capture.setHotkeys(services.preferences.hotkeys)
        tasks.append(
            Task { [weak self, control = mesh.control] in
                for await event in control {
                    if case .dragArrived(let source, let dragID, let point, let items) = event {
                        // The input that follows waits until the drag has started.
                        await self?.drags?.arrived(
                            source: source, dragID: dragID, at: ControlCoordinates.point(point), items: items)
                    } else {
                        self?.handle(event)
                    }
                }
            })
        let forwarder = InputForwarder(doubleClickInterval: services.doubleClickInterval())
        // Off the main thread: every mouse movement goes to core, so a busy main thread never
        // delays the cursor on another computer.
        tasks.append(
            Task.detached { [weak self, mesh, events = capture.events, capsEchoUntil] in
                var forwarder = forwarder
                for await event in events {
                    switch event {
                    case .input(let captured):
                        if Self.isCapsEcho(captured, until: capsEchoUntil.withLock { $0 }) {
                            continue
                        }
                        if let input = forwarder.convert(captured) {
                            mesh.input(input, ageMicroseconds: Self.age(of: captured))
                        }
                    case .hotkey(.lockInput):
                        await self?.toggleLock()
                    case .released(let reason):
                        // `.requested` is this controller giving the input back.
                        if reason != .requested {
                            mesh.returnHome(inputLost: reason != .releaseChord)
                        }
                    case .tapReenabled(let reason):
                        Log.input.info("Event tap turned back on: \(String(describing: reason), privacy: .public)")
                    }
                }
            })
        tasks.append(
            Task { [weak self] in
                // Caps Lock goes to core while the cursor is home; while it is away, core follows
                // the presses it sends.
                while !Task.isCancelled, self?.reportCapsLock() != nil {
                    try? await Task.sleep(for: Tunables.Control.capsCheckInterval)
                }
            })
        tasks.append(
            Task { [weak self] in
                // Accessibility can be granted or taken away at any time.
                while !Task.isCancelled, self?.watchIfNeeded() != nil {
                    try? await Task.sleep(for: Tunables.Control.permissionCheckInterval)
                }
            })
    }

    func stop() {
        tasks.forEach { $0.cancel() }
        tasks.removeAll()
        unwatch()
        drags?.stop()
        injector.releaseAll()
    }

    /// Whether a member is connected: this Mac's input and drags are watched only then.
    func setActive(_ active: Bool) {
        isActive = active
        if active {
            watchIfNeeded()
            drags?.start()
        } else {
            unwatch()
            drags?.stop()
        }
    }

    /// A setting changed in Settings: it applies at once (a modifier mapping from the next crossing).
    func apply(_ settings: InputSettings) {
        mesh.setInputSettings(settings)
    }

    func setPointerSpeed(_ permille: UInt32, for device: DeviceID) {
        mesh.setPointerSpeed(permille, for: device)
    }

    func setHotkeys(_ hotkeys: CaptureHotkeys) {
        capture.setHotkeys(hotkeys)
    }

    func screenLockChanged(_ locked: Bool) {
        screenLocked = locked
        updateAvailability()
    }

    func dismissNotice() {
        notice = nil
    }

    // MARK: - Watching this Mac's input

    private func watchIfNeeded() {
        isPermitted = services.isTrusted()
        defer { updateAvailability() }
        guard isActive, isPermitted, !isWatching else { return }
        do throws(CaptureError) {
            try capture.start(layout: services.keyboardLayout())
            isWatching = true
        } catch {
            Log.input.error("Watching input failed: \(String(describing: error), privacy: .public)")
        }
    }

    private func unwatch() {
        guard isWatching else { return }
        stopHeartbeat()
        capture.stop()
        isWatching = false
    }

    private nonisolated static func isCapsEcho(_ captured: CapturedInput, until deadline: UInt64) -> Bool {
        guard case .key(let key, _) = captured.event, key == .keyboard(ModifierKeys.capsLockUsage) else {
            return false
        }
        return DispatchTime.now().uptimeNanoseconds < deadline
    }

    /// Sets Caps Lock on this Mac; the event macOS may post for it is not the user's.
    private func setCapsLock(_ on: Bool) {
        guard on != services.capsLock.isOn else { return }
        let window = UInt64(Tunables.Control.capsEchoWindow.timeInterval * 1e9)
        capsEchoUntil.withLock { $0 = DispatchTime.now().uptimeNanoseconds + window }
        services.capsLock.set(on)
    }

    private nonisolated static func age(of captured: CapturedInput) -> UInt64 {
        let now = DispatchTime.now().uptimeNanoseconds
        guard captured.tapTime != 0, now > captured.tapTime else { return 0 }
        return (now - captured.tapTime) / 1_000
    }

    private func reportCapsLock() {
        guard capturingFor == nil else { return }
        let on = services.capsLock.isOn
        if on != capsLockReported {
            capsLockReported = on
            mesh.setCapsLock(on)
        }
    }

    private func toggleLock() {
        setLocked(!isLocked)
    }

    /// "Lock input here", from the hotkey or the menu.
    func setLocked(_ locked: Bool) {
        isLocked = locked
        mesh.lockInput(locked)
    }

    /// Other computers are refused while this Mac's screen is locked or it cannot inject.
    private func updateAvailability() {
        let reason: ControlUnavailability? =
            if screenLocked {
                .screenLocked
            } else if !isPermitted {
                .notPermitted
            } else {
                nil
            }
        guard unavailable != .some(reason) else { return }
        unavailable = .some(reason)
        mesh.setUnavailable(reason)
    }

    // MARK: - Core's events

    private func handle(_ event: ControlEvent) {
        switch event {
        case .captureBegan(let device, let releaseHere):
            capture.setMode(.capture)
            injector.releaseHere(Injection.keys(releaseHere))
            capturingFor = device
            notice = nil
            startHeartbeat()
        case .captureEnded(let device, let cursor, let reason, let capsLock):
            stopHeartbeat()
            capture.setMode(.observe)
            injector.placeCursor(at: ControlCoordinates.point(cursor))
            setCapsLock(capsLock)
            capsLockReported = capsLock
            capturingFor = nil
            switch reason {
            case .refused(let refusal): notice = .refused(name: nameOf(device), refusal)
            case .peerLost: notice = .lost(name: nameOf(device))
            case .returned, .hotkey, .inputLost, .takeover: break
            }
        case .controlled(let by):
            controller = by
            if by == nil {
                stopCursorWatch()
                // Core released what it pressed; this covers the rest, but not a parked drag.
                injector.releaseAllButDrag()
            } else {
                startCursorWatch()
            }
        case .inject(.capsLock(let on)):
            setCapsLock(on)
        case .inject(let input):
            Injection.apply(input, to: injector)
        case .inputLatency(let device, let samples, let median, let p99):
            latency[device] = InputLatency(
                samples: samples, median: .microseconds(median), p99: .microseconds(p99))
        case .dragArrived:
            break  // awaited where the events are read
        case .dragWithdrawn(let dragID):
            drags?.withdrawn(dragID)
        case .dragEnded(let dragID, let outcome):
            drags?.ended(dragID, outcome)
        case .dragWanted:
            drags?.wanted()
        }
    }

    // MARK: - Safety timers

    /// While captured, the capture stays only as long as the main thread confirms it.
    private func startHeartbeat() {
        heartbeat?.cancel()
        capture.heartbeat()
        heartbeat = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: Tunables.Input.captureHeartbeatInterval)
                self?.capture.heartbeat()
            }
        }
    }

    private func stopHeartbeat() {
        heartbeat?.cancel()
        heartbeat = nil
    }

    /// While controlled, an application that moves the cursor is reported, so the controller's
    /// cursor follows. A position must hold for two checks: posted movement lands a little later.
    private func startCursorWatch() {
        cursorWatch?.cancel()
        cursorWatch = Task { [weak self] in
            var previous: CGPoint?
            var reported: CGPoint?
            while !Task.isCancelled {
                try? await Task.sleep(for: Tunables.Control.cursorCheckInterval)
                guard let self else { return }
                let actual = services.cursorLocation()
                let expected = injector.cursorLocation
                let moved =
                    abs(actual.x - expected.x) > Tunables.Control.cursorReportDistance
                    || abs(actual.y - expected.y) > Tunables.Control.cursorReportDistance
                if moved, actual == previous, actual != reported {
                    mesh.cursorMoved(to: ControlCoordinates.coord(actual))
                    reported = actual
                }
                previous = moved ? actual : nil
            }
        }
    }

    private func stopCursorWatch() {
        cursorWatch?.cancel()
        cursorWatch = nil
    }
}
