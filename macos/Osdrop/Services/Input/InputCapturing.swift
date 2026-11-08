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
