import Foundation
import UserNotifications

/// Posts transfer notifications through Notification Center; macOS asks the user once whether
/// Osdrop may show them.
struct UserNotificationNotifier: TransferNotifying {
    func notify(title: String, body: String) {
        Task {
            let center = UNUserNotificationCenter.current()
            do {
                guard try await center.requestAuthorization(options: [.alert]) else { return }
                let content = UNMutableNotificationContent()
                content.title = title
                content.body = body
                try await center.add(
                    UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil))
            } catch {
                Log.drag.error("Showing a notification failed: \(error.localizedDescription, privacy: .public)")
            }
        }
    }
}
