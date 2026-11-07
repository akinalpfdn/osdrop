import Foundation
import Testing

@testable import Osdrop

@Suite("Release versions")
struct ReleaseVersionTests {
    @Test("Tags and short versions parse; anything else does not")
    func parsing() {
        #expect(ReleaseVersion("v1.2.3")?.description == "1.2.3")
        #expect(ReleaseVersion("1.2")?.description == "1.2.0")
        #expect(ReleaseVersion("1.2.3.4")?.description == "1.2.3")
        for bad in ["", "v", "x", "1.x", "1.-1", "1..2"] {
            #expect(ReleaseVersion(bad) == nil, "\(bad)")
        }
    }

    @Test("Versions compare part by part")
    func comparison() throws {
        let older = try #require(ReleaseVersion("0.9.9"))
        let newer = try #require(ReleaseVersion("0.10.0"))
        #expect(older < newer)
        #expect(!(newer < newer))
    }

    @Test("GitHub's latest release becomes an update only when it is newer")
    func outcome() throws {
        let current = try #require(ReleaseVersion("0.1.0"))
        let release = Data(
            #"{"tag_name":"v0.2.0","html_url":"https://github.com/akinalpfdn/osdrop/releases/tag/v0.2.0"}"#.utf8)
        let page = try #require(URL(string: "https://github.com/akinalpfdn/osdrop/releases/tag/v0.2.0"))
        let expected = UpdateCheckOutcome.available(
            AvailableUpdate(version: try #require(ReleaseVersion("0.2.0")), page: page))
        #expect(GitHubUpdateChecker.outcome(from: release, current: current) == expected)

        let same = Data(#"{"tag_name":"v0.1.0","html_url":"https://github.com/a/b"}"#.utf8)
        #expect(GitHubUpdateChecker.outcome(from: same, current: current) == .upToDate)
        let odd = Data(#"{"tag_name":"nightly","html_url":"https://github.com/a/b"}"#.utf8)
        #expect(GitHubUpdateChecker.outcome(from: odd, current: current) == .failed)
        #expect(GitHubUpdateChecker.outcome(from: Data("{}".utf8), current: current) == .failed)
    }
}

/// Answers every check the same way and counts them.
private final class FakeUpdateChecker: UpdateChecking, @unchecked Sendable {
    private let lock = NSLock()
    private var calls = 0
    let answer: UpdateCheckOutcome

    init(answer: UpdateCheckOutcome) {
        self.answer = answer
    }

    var checks: Int { lock.withLock { calls } }

    func check(current: ReleaseVersion) async -> UpdateCheckOutcome {
        lock.withLock { calls += 1 }
        return answer
    }
}

@MainActor
@Suite("Update checks")
struct UpdatesViewModelTests {
    private let now = Date(timeIntervalSince1970: 1_800_000_000)

    private func settings() throws -> AppSettings {
        let name = "osdrop-tests-\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: name))
        defaults.removePersistentDomain(forName: name)
        return AppSettings(defaults: defaults)
    }

    private func update() throws -> AvailableUpdate {
        AvailableUpdate(
            version: try #require(ReleaseVersion("0.2.0")),
            page: try #require(URL(string: "https://github.com/akinalpfdn/osdrop/releases/tag/v0.2.0")))
    }

    @Test("Checking is on by default and runs when it is due")
    func dailyCheck() async throws {
        let settings = try settings()
        #expect(settings.checksForUpdates)
        let checker = FakeUpdateChecker(answer: .available(try update()))
        let updates = UpdatesViewModel(
            settings: settings, checker: checker, currentVersion: try #require(ReleaseVersion("0.1.0")),
            now: { [now] in now })

        await updates.checkIfDue()

        #expect(checker.checks == 1)
        #expect(updates.available == (try update()))
        #expect(settings.lastUpdateCheck == now)
    }

    @Test("No automatic check when it is off or ran within a day")
    func notDue() async throws {
        let settings = try settings()
        let checker = FakeUpdateChecker(answer: .upToDate)
        let updates = UpdatesViewModel(
            settings: settings, checker: checker, currentVersion: try #require(ReleaseVersion("0.1.0")),
            now: { [now] in now })

        settings.lastUpdateCheck = now.addingTimeInterval(-60 * 60)
        await updates.checkIfDue()
        settings.checksForUpdates = false
        settings.lastUpdateCheck = nil
        await updates.checkIfDue()

        #expect(checker.checks == 0)
        await updates.checkNow()
        #expect(checker.checks == 1, "Check Now works even with the daily check off")
        #expect(updates.result == .upToDate)
    }

    @Test("A failed check is tried again at the next launch")
    func failure() async throws {
        let settings = try settings()
        let updates = UpdatesViewModel(
            settings: settings, checker: FakeUpdateChecker(answer: .failed),
            currentVersion: try #require(ReleaseVersion("0.1.0")), now: { [now] in now })

        await updates.checkNow()

        #expect(updates.result == .failed)
        #expect(settings.lastUpdateCheck == nil)
        #expect(updates.available == nil)
    }
}
