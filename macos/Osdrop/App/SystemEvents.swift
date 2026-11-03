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
    }

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
