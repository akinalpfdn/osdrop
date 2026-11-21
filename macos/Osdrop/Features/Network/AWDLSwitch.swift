import Foundation
import Observation

/// The setting that keeps AWDL off while Osdrop runs (PHASE-024-NET-01): it registers the network
/// helper, waits for the user's one-time approval, and keeps one connection asking the helper to
/// hold AWDL off. Turning it off releases AWDL and unregisters the helper; quitting releases it too.
@MainActor
@Observable
final class AWDLSwitch {
    enum State: Equatable, Sendable {
        case off
        /// Waiting for "Allow in the Background" in System Settings › General › Login Items.
        case needsApproval
        case starting
        /// AWDL is off.
        case holding
        case failed
    }

    private(set) var state: State = .off

    @ObservationIgnored private let settings: AppSettings
    @ObservationIgnored private let registration: any NetworkHelperRegistering
    @ObservationIgnored private let helper: any NetworkHelperConnecting
    @ObservationIgnored private let approvalPoll: Duration
    @ObservationIgnored private let retryDelay: Duration
    /// Newer requests make older ones that are still waiting for the helper moot.
    @ObservationIgnored private var generation = 0
    @ObservationIgnored private var approvalWatch: Task<Void, Never>?

    init(
        settings: AppSettings, registration: any NetworkHelperRegistering, helper: any NetworkHelperConnecting,
        approvalPoll: Duration = Tunables.Network.approvalPoll, retryDelay: Duration = Tunables.Network.helperRetry
    ) {
        self.settings = settings
        self.registration = registration
        self.helper = helper
        self.approvalPoll = approvalPoll
        self.retryDelay = retryDelay
    }

    var isEnabled: Bool { settings.keepsAWDLOff }

    func start() {
        helper.onBroken = { [weak self] in self?.broken() }
        if settings.keepsAWDLOff {
            Task { await apply() }
        }
    }

    func setEnabled(_ enabled: Bool) {
        settings.keepsAWDLOff = enabled
        Task { await apply() }
    }

    func openApproval() {
        registration.openApproval()
    }

    /// Settings appeared: the approval may have been given meanwhile.
    func refresh() {
        if state == .needsApproval || state == .failed {
            Task { await apply() }
        }
    }

    func apply() async {
        generation += 1
        let current = generation
        approvalWatch?.cancel()
        approvalWatch = nil
        guard settings.keepsAWDLOff else {
            if state == .holding || state == .starting {
                _ = await helper.keepAWDLOff(false)
            }
            helper.disconnect()
            registration.unregister()
            if current == generation {
                state = .off
            }
            return
        }
        if registration.status != .enabled {
            do {
                try registration.register()
            } catch {
                // The first registration asks for approval and reports it as an error.
                Log.app.info("Registering the network helper: \(error.localizedDescription, privacy: .public)")
            }
        }
        switch registration.status {
        case .enabled:
            break
        case .requiresApproval:
            state = .needsApproval
            watchApproval()
            return
        case .notRegistered, .notFound:
            state = .failed
            return
        }
        state = .starting
        let isOff = await helper.keepAWDLOff(true)
        guard current == generation else { return }
        state = isOff == true ? .holding : .failed
    }

    private func watchApproval() {
        approvalWatch = Task { [weak self, approvalPoll] in
            while !Task.isCancelled {
                try? await Task.sleep(for: approvalPoll)
                guard let self, !Task.isCancelled else { return }
                if registration.status == .enabled {
                    await apply()
                    return
                }
            }
        }
    }

    /// The helper ended while it held AWDL off: ask again, which starts it again.
    private func broken() {
        guard settings.keepsAWDLOff, state == .holding || state == .starting else { return }
        state = .starting
        Task { [weak self, retryDelay] in
            try? await Task.sleep(for: retryDelay)
            await self?.apply()
        }
    }
}
