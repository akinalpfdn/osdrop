// Compiled into the Osdrop app and into OsdropCaptureGuard, so both sides share one definition.

/// One byte Osdrop writes to the capture guard's standard input.
enum CaptureGuardMessage: UInt8, Sendable {
    /// Input capture began; the guard starts expecting heartbeats.
    case arm = 0x41  // "A"
    /// Capture is still healthy.
    case heartbeat = 0x48  // "H"
    /// Capture ended; the guard stops expecting heartbeats.
    case disarm = 0x44  // "D"
}

enum CaptureGuardProtocol {
    /// How long the guard waits for a heartbeat while capture is on before it ends Osdrop. Longer
    /// than the in-app watchdog's timeout, so a hung main thread is handled in the app first.
    static let timeout: Duration = .seconds(2)
    static let executableName = "OsdropCaptureGuard"
}

/// The guard's decision, kept free of I/O so it can be tested.
struct CaptureGuardState {
    let timeout: Duration
    private(set) var isArmed = false
    private var lastSignal: ContinuousClock.Instant?

    init(timeout: Duration) {
        self.timeout = timeout
    }

    mutating func receive(_ byte: UInt8, at now: ContinuousClock.Instant) {
        switch CaptureGuardMessage(rawValue: byte) {
        case .arm:
            isArmed = true
            lastSignal = now
        case .heartbeat where isArmed:
            lastSignal = now
        case .disarm:
            isArmed = false
            lastSignal = nil
        case .heartbeat, nil:
            break
        }
    }

    /// True when capture is on and Osdrop has been silent for longer than the timeout.
    func shouldTerminate(at now: ContinuousClock.Instant) -> Bool {
        guard isArmed, let lastSignal else { return false }
        return now - lastSignal > timeout
    }
}
