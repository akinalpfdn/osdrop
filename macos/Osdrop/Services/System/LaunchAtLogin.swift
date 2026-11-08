import Foundation
import ServiceManagement

enum LaunchAtLoginError: Error, Equatable, Sendable {
    /// The system refused (it needs the user's approval in Login Items, or the app is not in
    /// Applications).
    case refused(String)
}

/// Opening Osdrop at login (DEVPLAN: `SMAppService`).
@MainActor
protocol LaunchAtLoginManaging: AnyObject {
    var isEnabled: Bool { get }
    /// Waiting for the user to allow it in System Settings › General › Login Items.
    var needsApproval: Bool { get }
    func setEnabled(_ enabled: Bool) throws(LaunchAtLoginError)
}

@MainActor
final class SystemLaunchAtLogin: LaunchAtLoginManaging {
    var isEnabled: Bool { SMAppService.mainApp.status == .enabled }

    var needsApproval: Bool { SMAppService.mainApp.status == .requiresApproval }

    func setEnabled(_ enabled: Bool) throws(LaunchAtLoginError) {
        do {
            if enabled {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            throw .refused(error.localizedDescription)
        }
    }
}
