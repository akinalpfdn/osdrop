import Carbon.HIToolbox
import Foundation

/// The macOS app's tunable values. Protocol constants live in `core`, not here.
enum Tunables {
    enum Input {
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
        /// How often the capture watchdog checks the last confirmation. Each healthy check also
        /// sends the capture guard a heartbeat.
        static let watchdogCheckInterval: Duration = .milliseconds(100)
        /// Watchdog checks in a row that may find the event tap disabled before capture ends;
        /// the tap callback normally turns it back on within one.
        static let tapLostChecks = 3
        /// Captured events buffered for a slow consumer before the oldest are dropped.
        static let eventBufferLimit = 4096
        /// How long `stop` waits for the event tap thread to finish.
        static let tapThreadStopTimeout: Duration = .seconds(2)
    }

    enum Control {
        /// This Mac's own pointer speed, in thousandths.
        static let normalSpeed: UInt32 = 1_000
        /// The pointer speeds Settings offers, as a share of this Mac's own.
        static let speedRange = 0.25...4.0
        /// The push times Settings offers, in seconds.
        static let pushSecondsRange = 0.2...3.0
        /// How often Osdrop checks whether Accessibility was granted, until it can watch input.
        static let permissionCheckInterval: Duration = .seconds(1)
        /// How often a controlled Mac compares its cursor with where the controller put it.
        static let cursorCheckInterval: Duration = .milliseconds(100)
        /// A cursor further than this (points) from where the controller put it was moved by an
        /// application, and the controller is told.
        static let cursorReportDistance = 1.0
        /// How often Caps Lock is read while the cursor is home.
        static let capsCheckInterval: Duration = .milliseconds(250)
        /// Pointer speeds core takes, in thousandths of this Mac's own.
        static let pointerSpeeds = 250...4_000
    }

    enum Drag {
        /// How often the drag monitor looks at the mouse button and the drag pasteboard.
        static let monitorInterval: Duration = .milliseconds(33)
        /// Items up to this size are also offered as a staged file URL, for apps that do not take
        /// file promises. The copy is made synchronously when the target asks for it.
        static let stagedURLLimit: UInt64 = 64 * 1024 * 1024
        /// How long a capture waits for the source app to write promised files. Photos may first
        /// download originals from iCloud.
        static let promiseTimeout: Duration = .seconds(120)
    }

    enum Logs {
        /// Copy diagnostics includes this many of the newest log lines.
        static let diagnosticsLines = 300
        /// How far back an exported log reaches (the current run only: the store is per process).
        static let exportWindow: TimeInterval = 24 * 60 * 60
    }

    enum Updates {
        static let latestReleaseURL = URL(string: "https://api.github.com/repos/akinalpfdn/osdrop/releases/latest")
        static let releasesPage = URL(string: "https://github.com/akinalpfdn/osdrop/releases")
        /// The automatic check runs at most this often.
        static let interval: TimeInterval = 24 * 60 * 60
        static let timeout: TimeInterval = 15
    }

    enum Files {
        /// The agent named in the quarantine mark of received files.
        static let quarantineAgent = "Osdrop"
    }

    enum Clipboard {
        /// macOS has no notification for pasteboard changes; its change count is read this often.
        static let pollInterval: Duration = .milliseconds(500)
        /// The largest text, RTF or HTML copy shared, either way.
        static let textLimit: UInt64 = 16 * 1024 * 1024
        static let imageLimit: UInt64 = 64 * 1024 * 1024
        /// Files copied on another computer are fetched up to this total (user decision, Phase 15;
        /// a setting in Phase 16). Larger copies are moved by drag and drop.
        static let filesLimit: UInt64 = 512 * 1024 * 1024
        /// The limits Settings offers for files copied on another computer.
        static let filesLimitChoices: [UInt64] = [128, 512, 1024, 4096].map { $0 * 1024 * 1024 }
    }

    enum KeyStore {
        static let keychainService = "com.akinalpfdn.osdrop"
        static let identityAccount = "identity"
        static let groupAccount = "group"
        /// Shown in Keychain Access.
        static let itemLabel = "Osdrop"
    }

    enum Settings {
        static let layoutStateKey = "layoutState"
        static let listenPortKey = "listenPort"
    }

    enum Permissions {
        /// System Settings › Privacy & Security › Local Network.
        static let localNetworkSettings = URL(
            string: "x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork")
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
            /// The loopback drag starts this far inside the primary display's left edge.
            static let dragOriginInset = 40.0
            /// The loopback drag moves in this many steps, this far apart.
            static let dragSteps = 24
            static let dragStepDelay: Duration = .milliseconds(25)
            /// Pause on each small movement over the drop target before releasing.
            static let dropHover: Duration = .milliseconds(150)
            /// How long the loopback waits for macOS to report the end of the drag.
            static let dragEndTimeout: Duration = .seconds(5)
        }
    #endif
}
