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

    @Test("A private AppKit member counts only with the signature it was written for")
    func checksSignatures() {
        let description = NSSelectorFromString("description")
        #expect(MenuBarIcon.has(description, signature: "@16@0:8", in: NSObject.self))
        #expect(!MenuBarIcon.has(description, signature: "v24@0:8d16", in: NSObject.self))
        #expect(!MenuBarIcon.has(NSSelectorFromString("noSuchMember"), signature: "@16@0:8", in: NSObject.self))
    }

    private func makeOpener() -> MenuOpener {
        let clicks = clicks
        return MenuOpener(settings: AppSettings(defaults: defaults), show: { clicks.count += 1 })
    }
}

@MainActor
private final class ClickCounter {
    var count = 0
}
