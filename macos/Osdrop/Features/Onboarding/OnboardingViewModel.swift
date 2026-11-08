import AppKit
import Foundation
import Observation

/// The permissions Osdrop needs, with their live status (DEVPLAN Phase 16 onboarding):
/// Accessibility to watch and post input, Local Network to find and reach other computers.
@MainActor
@Observable
final class OnboardingViewModel {
    private(set) var localNetwork: LocalNetworkAccess = .unknown

    @ObservationIgnored private let accessibility: any AccessibilityPermissionChecking
    @ObservationIgnored private let localNetworkCheck: any LocalNetworkChecking
    @ObservationIgnored private let settings: AppSettings
    @ObservationIgnored private var tasks: [Task<Void, Never>] = []

    init(
        accessibility: any AccessibilityPermissionChecking, localNetworkCheck: any LocalNetworkChecking,
        settings: AppSettings
    ) {
        self.accessibility = accessibility
        self.localNetworkCheck = localNetworkCheck
        self.settings = settings
    }

    var hasAccessibility: Bool { accessibility.isTrusted }

    /// Everything granted (local network counts once macOS reports it allowed).
    var isComplete: Bool { hasAccessibility && localNetwork == .allowed }

    /// Whether the window should open at launch.
    var needsAttention: Bool { !settings.hasCompletedOnboarding || !accessibility.isTrusted }

    /// Starts watching; System Settings changes show up within a second.
    func start() {
        stop()
        let updates = localNetworkCheck.accessUpdates()
        tasks = [
            Task { [weak self] in
                for await access in updates {
                    self?.localNetwork = access
                }
            }
        ]
    }

    func stop() {
        tasks.forEach { $0.cancel() }
        tasks = []
    }

    func allowAccessibility() {
        accessibility.requestAccess()
        accessibility.openSystemSettings()
    }

    func openLocalNetworkSettings() {
        guard let url = Tunables.Permissions.localNetworkSettings else { return }
        NSWorkspace.shared.open(url)
    }

    func finish() {
        settings.hasCompletedOnboarding = true
        stop()
    }
}
