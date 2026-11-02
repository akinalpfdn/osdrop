import AppKit
import Carbon.HIToolbox
import IOKit

struct SecureInputStatus: Equatable, Sendable {
    let isEnabled: Bool
    /// The app that turned Secure Event Input on, when macOS reports it.
    let ownerName: String?
    let ownerProcessID: pid_t?

    static let off = SecureInputStatus(isEnabled: false, ownerName: nil, ownerProcessID: nil)
}

/// While any app has Secure Event Input on, event taps see no keystrokes (DEVPLAN, macOS
/// constraints). The UI names the app so the user can close its password field.
@MainActor
protocol SecureInputChecking {
    func current() -> SecureInputStatus
}

struct SystemSecureInput: SecureInputChecking {
    func current() -> SecureInputStatus {
        guard IsSecureEventInputEnabled() else { return .off }
        let processID = Self.ownerProcessID()
        let name = processID.flatMap { NSRunningApplication(processIdentifier: $0)?.localizedName }
        return SecureInputStatus(isEnabled: true, ownerName: name, ownerProcessID: processID)
    }

    /// The window server records the owner in the console user's session dictionary.
    private static func ownerProcessID() -> pid_t? {
        let root = IORegistryGetRootEntry(kIOMainPortDefault)
        defer { IOObjectRelease(root) }
        guard
            let users = IORegistryEntryCreateCFProperty(root, "IOConsoleUsers" as CFString, kCFAllocatorDefault, 0)?
                .takeRetainedValue() as? [[String: Any]]
        else {
            return nil
        }
        let processID = users.lazy.compactMap { $0["kCGSSessionSecureInputPID"] as? Int }.first
        return processID.map { pid_t($0) }
    }
}
