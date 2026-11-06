import AppKit
import Foundation
import Observation

/// Update checks, like the Windows shell's: once a day at launch when enabled, and on request.
/// A newer version shows in the menu and in Settings with a link to its release page.
@MainActor
@Observable
final class UpdatesViewModel {
    enum CheckResult: Equatable {
        case checking
        case upToDate
        case available(ReleaseVersion)
        case failed
    }

    private(set) var available: AvailableUpdate?
    private(set) var result: CheckResult?
    let currentVersion: ReleaseVersion

    private let settings: AppSettings
    private let checker: any UpdateChecking
    private let now: () -> Date

    init(
        settings: AppSettings, checker: any UpdateChecking, currentVersion: ReleaseVersion = .current,
        now: @escaping () -> Date = Date.init
    ) {
        self.settings = settings
        self.checker = checker
        self.currentVersion = currentVersion
        self.now = now
    }

    var isChecking: Bool { result == .checking }

    /// The daily automatic check; does nothing when turned off or done recently.
    func checkIfDue() async {
        guard settings.checksForUpdates else { return }
        if let last = settings.lastUpdateCheck, now().timeIntervalSince(last) < Tunables.Updates.interval { return }
        await checkNow()
    }

    func checkNow() async {
        guard !isChecking else { return }
        result = .checking
        switch await checker.check(current: currentVersion) {
        case .available(let update):
            available = update
            result = .available(update.version)
            settings.lastUpdateCheck = now()
        case .upToDate:
            available = nil
            result = .upToDate
            settings.lastUpdateCheck = now()
        case .failed:
            result = .failed
        }
    }

    /// The newer release's page, or the list of releases.
    func openDownload() {
        guard let page = available?.page ?? Tunables.Updates.releasesPage else { return }
        NSWorkspace.shared.open(page)
    }
}
