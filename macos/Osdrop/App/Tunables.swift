import Carbon.HIToolbox
import Foundation

/// The macOS app's tunable values. Protocol constants live in `core`, not here.
enum Tunables {
    enum Input {
        /// Escape, pressed with Control, Option and Command, ends a capture at once.
        static let releaseChordKeyCode = kVK_Escape
        /// Written into the source user data of every event Osdrop posts ("OSDROP" in ASCII), so
        /// its own event tap never mistakes injected input for the user's.
        static let injectedEventTag: Int64 = 0x4F53_4452_4F50
        /// Presses within this distance (points) of the previous one continue a double click.
        static let clickRadius = 4.0
        /// Capture ends by itself when its owner stops confirming it for this long, so a hung
        /// main thread never keeps the cursor frozen (DEVPLAN, input safety invariants).
        static let captureHeartbeatTimeout: Duration = .seconds(1)
        /// How often the owner of a capture confirms it.
        static let captureHeartbeatInterval: Duration = .milliseconds(250)
        /// How often the capture watchdog checks the last confirmation.
        static let watchdogCheckInterval: Duration = .milliseconds(100)
        /// Captured events buffered for a slow consumer before the oldest are dropped.
        static let eventBufferLimit = 4096
        /// How long `stop` waits for the event tap thread to finish.
        static let tapThreadStopTimeout: Duration = .seconds(2)
    }

    enum Permissions {
        /// macOS posts no reliable notification when Accessibility access changes, so it is
        /// re-read on this interval while the app runs.
        static let accessibilityPollInterval: Duration = .seconds(2)
    }

    #if DEBUG
        enum Developer {
            static let captureDurations: [Int] = [3, 5, 10]
            static let defaultCaptureSeconds = 5
            /// Gaps between recorded events longer than this are shortened during replay.
            static let maximumReplayGap: Duration = .milliseconds(500)
            /// Seconds between pressing Capture and the capture starting.
            static let captureCountdownSeconds = 3
        }
    #endif
}
