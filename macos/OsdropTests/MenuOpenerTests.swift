import Foundation
import Testing

@testable import Osdrop

/// The menu opens by itself once at the first launch, and whenever Osdrop is opened again.
@MainActor
@Suite("Menu opener")
struct MenuOpenerTests {
    private let defaults: UserDefaults
    private let clicks = ClickCounter()

    init() throws {
        defaults = try #require(UserDefaults(suiteName: "osdrop-tests-\(UUID())"))
    }

    @Test("Only the first launch opens the menu, also after a restart")
    func firstLaunchOnly() {
        let opener = makeOpener()
        opener.openAtFirstLaunch()
        opener.openAtFirstLaunch()
        #expect(clicks.count == 1)

        makeOpener().openAtFirstLaunch()
        #expect(clicks.count == 1, "the next launch remembers it")
    }

    @Test("Opening Osdrop again opens the menu every time")
    func reopenAlwaysOpens() {
        let opener = makeOpener()
        opener.openAtFirstLaunch()
        opener.open()
        opener.open()
        #expect(clicks.count == 3)
    }

    @Test("The app knows it hosts these tests, so it leaves the user's menu alone")
    func knowsItHostsTests() {
        #expect(TestHost.isRunning)
    }

    private func makeOpener() -> MenuOpener {
        let clicks = clicks
        return MenuOpener(settings: AppSettings(defaults: defaults), click: { clicks.count += 1 })
    }
}

@MainActor
private final class ClickCounter {
    var count = 0
}
