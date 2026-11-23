import CoreGraphics

enum CaptureMode: Sendable, Equatable {
    /// Input reaches this Mac's apps as usual; Osdrop only watches it.
    case observe
    /// Input is swallowed and reported only to Osdrop; the cursor is frozen and hidden.
    case capture
}

enum CaptureReleaseReason: Sendable, Equatable {
    case requested
    /// The user pressed the return chord (⌃⌥⌘⎋ unless changed), which always works while input is
    /// captured.
    case releaseChord
    /// The owner stopped confirming the capture (DEVPLAN, input safety invariants).
    case watchdog
    /// macOS turned the event tap off and it did not come back.
    case tapLost
    /// Capture was refused because the capture guard is not running.
    case guardUnavailable
}

enum TapDisableReason: Sendable, Equatable {
    /// macOS disabled the tap because a callback took too long.
    case timeout
    /// macOS disabled the tap on a user action (Secure Event Input and similar).
    case userInput
}

/// Key chords Osdrop keeps for itself; apps and other computers never see them.
enum CaptureHotkey: Sendable, Equatable {
    /// Control, Option and Command held with L: keep the cursor on the computer it is on, or let
    /// it cross again.
    case lockInput
}

enum CaptureEvent: Sendable, Equatable {
    case input(CapturedInput)
    case hotkey(CaptureHotkey)
    /// macOS disabled the event tap and Osdrop turned it back on.
    case tapReenabled(TapDisableReason)
    /// Capture ended and normal input is back.
    case released(CaptureReleaseReason)
}

enum CaptureError: Error, Equatable {
    /// The event tap could not be created; Accessibility access is missing.
    case tapUnavailable
    /// The event tap thread did not start in time.
    case threadDidNotStart
    /// The process that gives the cursor back if Osdrop hangs could not be started.
    case guardUnavailable
}

/// Watches this Mac's physical input and, when asked, takes it over (DEVPLAN Phase 1).
protocol InputCapturing: AnyObject, Sendable {
    /// Every physical input event while running, and changes of the tap and capture state.
    var events: AsyncStream<CaptureEvent> { get }
    var mode: CaptureMode { get }
    var isRunning: Bool { get }

    func start(layout: KeyboardPhysicalLayout) throws(CaptureError)
    func stop()
    func setMode(_ mode: CaptureMode)
    /// Confirms that capture should continue; without it, capture ends after
    /// `Tunables.Input.captureHeartbeatTimeout`.
    func heartbeat()
    /// The chords the tap keeps for Osdrop.
    func setHotkeys(_ hotkeys: CaptureHotkeys)
}

/// Whether the event tap lets an event through.
enum TapPolicy {
    enum Decision: Equatable {
        case pass
        case swallow
    }

    static func decide(mode: CaptureMode, isInjected: Bool) -> Decision {
        // Osdrop's own injected events always pass, so an endpoint can be driven while its own
        // input is observed.
        if isInjected {
            return .pass
        }
        return mode == .capture ? .swallow : .pass
    }

}

/// Whether the cursor stayed where it was frozen while this Mac's input is used elsewhere.
enum CursorFreeze {
    enum Check: Equatable {
        case still
        /// It finished the movement already on its way when it was frozen: it is held here now.
        case settled(CGPoint)
        /// It follows the mouse again.
        case loose
    }

    /// What the cursor at `location` says, `frozenFor` after it was frozen at `frozen`.
    static func check(location: CGPoint, frozen: CGPoint, frozenFor: Duration) -> Check {
        guard isLoose(location: location, frozen: frozen) else { return .still }
        return frozenFor < Tunables.Input.freezeSettleTime ? .settled(location) : .loose
    }

    /// Whether the cursor at `location` is away from `frozen`, where it should stand still.
    static func isLoose(location: CGPoint, frozen: CGPoint) -> Bool {
        abs(location.x - frozen.x) > Tunables.Input.looseCursorDistance
            || abs(location.y - frozen.y) > Tunables.Input.looseCursorDistance
    }

    /// `inputs` with the delta of the first pointer movement dropped: macOS adds a warp's distance
    /// to the next movement, and the cursor was just warped to where it came home. `dropped` says
    /// whether there was a movement to drop it from.
    static func droppingWarp(from inputs: [InputEvent]) -> (inputs: [InputEvent], dropped: Bool) {
        guard let index = inputs.firstIndex(where: \.isPointerMotion) else { return (inputs, false) }
        var inputs = inputs
        inputs[index] = .pointerMoved(dx: 0, dy: 0)
        return (inputs, true)
    }
}
