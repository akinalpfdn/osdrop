import AppKit
import ApplicationServices
import Observation

/// Accessibility access, which an active event tap and posting events require (DEVPLAN, macOS
/// constraints).
@MainActor
protocol AccessibilityPermissionChecking: AnyObject {
    var isTrusted: Bool { get }
    /// Shows macOS's own prompt, which also adds Osdrop to the list in System Settings.
    func requestAccess()
    func openSystemSettings()
}

/// Reads the current Accessibility state and keeps it current: macOS posts no reliable change
/// notification, so it is re-read on an interval.
@MainActor
@Observable
final class AccessibilityPermission: AccessibilityPermissionChecking {
    private static let settingsURL = URL(
        string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")

    private(set) var isTrusted: Bool
    @ObservationIgnored private let check: () -> Bool
    @ObservationIgnored nonisolated(unsafe) private var pollTask: Task<Void, Never>?

    init(check: @escaping () -> Bool = { AXIsProcessTrusted() }) {
        self.check = check
        isTrusted = check()
    }

    deinit {
        pollTask?.cancel()
    }

    /// Starts following changes; revocation while the app runs is reported like any other change.
    func startMonitoring() {
        guard pollTask == nil else { return }
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: Tunables.Permissions.accessibilityPollInterval)  // cancelled on deinit
                self?.refresh()
            }
        }
    }

    func refresh() {
        let trusted = check()
        guard trusted != isTrusted else { return }
        isTrusted = trusted
        Log.permissions.info("Accessibility access \(trusted ? "granted" : "revoked", privacy: .public)")
    }

    func requestAccess() {
        let options = ["AXTrustedCheckOptionPrompt": true] as CFDictionary
        isTrusted = AXIsProcessTrustedWithOptions(options)
    }

    func openSystemSettings() {
        guard let url = Self.settingsURL else { return }
        NSWorkspace.shared.open(url)
    }
}
