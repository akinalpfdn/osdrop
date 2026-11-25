import SwiftUI
import UIKit

@main
struct OsdropPhoneApp: App {
    @State private var mesh = PhoneMesh(
        keyStore: KeychainKeyStore(), discovery: DNSServiceDiscovery(), handoff: .shared,
        name: UIDevice.current.name, receivedFolder: .documentsDirectory)
    @State private var background = BackgroundTime()
    @Environment(\.scenePhase) private var phase

    init() {
        StagedFile.removeLeftovers()
    }

    var body: some Scene {
        WindowGroup {
            HomeView(mesh: mesh)
        }
        .onChange(of: phase, initial: true) { _, phase in
            switch phase {
            case .active:
                background.cancel()
                mesh.start()
            case .background:
                background.finish(mesh)
            default:
                break  // inactive (Control Center, a call): the sessions stay
            }
        }
    }
}

/// Lets files on their way finish when the app leaves the front, as long as iOS gives time; then
/// the sessions end cleanly before iOS suspends the app.
@MainActor
final class BackgroundTime {
    private var task: UIBackgroundTaskIdentifier = .invalid
    private var waiting: Task<Void, Never>?

    func finish(_ mesh: PhoneMesh) {
        guard mesh.hasRunningTransfers else {
            mesh.stop()
            return
        }
        task = UIApplication.shared.beginBackgroundTask(withName: "Osdrop transfers") { [weak self] in
            MainActor.assumeIsolated { self?.end(mesh) }
        }
        waiting = Task { [weak self] in
            while mesh.hasRunningTransfers && !Task.isCancelled {
                try? await Task.sleep(for: .seconds(1))
            }
            if !Task.isCancelled {
                self?.end(mesh)
            }
        }
    }

    /// The app came back before the time ran out: the sessions go on.
    func cancel() {
        waiting?.cancel()
        waiting = nil
        endTask()
    }

    private func end(_ mesh: PhoneMesh) {
        waiting?.cancel()
        waiting = nil
        mesh.stop()
        endTask()
    }

    private func endTask() {
        if task != .invalid {
            UIApplication.shared.endBackgroundTask(task)
            task = .invalid
        }
    }
}
