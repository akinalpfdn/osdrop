import AppKit

/// Sleep, wake and quit, as the group needs them: sessions end with the right Bye, and members are
/// found again after waking.
@MainActor
final class SystemEvents {
    nonisolated(unsafe) private var observers: [(NotificationCenter, NSObjectProtocol)] = []

    init(group: GroupController) {
        let workspace = NSWorkspace.shared.notificationCenter
        observe(workspace, NSWorkspace.willSleepNotification) { [weak group] in group?.closeAll(sleeping: true) }
        observe(workspace, NSWorkspace.didWakeNotification) { [weak group] in group?.resumeAfterSleep() }
        observe(.default, NSApplication.willTerminateNotification) { [weak group] in
            group?.closeAll(sleeping: false)
        }
        // Other computers cannot control a Mac whose screen is locked.
        let distributed = DistributedNotificationCenter.default()
        observe(distributed, Self.screenLocked) { [weak group] in group?.input?.screenLockChanged(true) }
        observe(distributed, Self.screenUnlocked) { [weak group] in group?.input?.screenLockChanged(false) }
    }

    private static let screenLocked = Notification.Name("com.apple.screenIsLocked")
    private static let screenUnlocked = Notification.Name("com.apple.screenIsUnlocked")

    private func observe(
        _ center: NotificationCenter, _ name: Notification.Name, _ action: @escaping @MainActor () -> Void
    ) {
        let token = center.addObserver(forName: name, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { action() }
        }
        observers.append((center, token))
    }

    deinit {
        for (center, token) in observers {
            center.removeObserver(token)
        }
    }
}
