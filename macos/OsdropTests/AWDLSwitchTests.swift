import Foundation
import Testing

@testable import Osdrop

/// The AWDL setting with the helper's registration and connection in memory: approval, holding,
/// letting go, and a helper that ended.
@MainActor
@Suite("The AWDL switch")
struct AWDLSwitchTests {
    private let defaults: UserDefaults
    private let registration = FakeHelperRegistration()
    private let helper = FakeHelper()

    init() throws {
        defaults = try #require(UserDefaults(suiteName: "osdrop-awdl-tests-\(UUID().uuidString)"))
    }

    private func make() -> (AWDLSwitch, AppSettings) {
        let settings = AppSettings(defaults: defaults)
        let awdl = AWDLSwitch(
            settings: settings, registration: registration, helper: helper, approvalPoll: .milliseconds(5),
            retryDelay: .milliseconds(5))
        awdl.start()
        return (awdl, settings)
    }

    @Test("Off by default: nothing is registered and the helper is never asked")
    func offByDefault() {
        let (awdl, settings) = make()
        #expect(!settings.keepsAWDLOff)
        #expect(awdl.state == .off)
        #expect(registration.registrations == 0)
        #expect(helper.requests.isEmpty)
    }

    @Test("Turned on with approval given, the helper holds AWDL off; turned off, it lets go")
    func holdsAndLetsGo() async throws {
        registration.status = .enabled
        let (awdl, settings) = make()
        awdl.setEnabled(true)
        try await waitUntil { awdl.state == .holding }
        #expect(settings.keepsAWDLOff)
        #expect(helper.requests == [true])

        awdl.setEnabled(false)
        try await waitUntil { awdl.state == .off }
        #expect(helper.requests == [true, false])
        #expect(helper.disconnects == 1)
        #expect(registration.unregistrations == 1)
    }

    @Test("The first time, it waits for the approval, then holds AWDL off by itself")
    func waitsForApproval() async throws {
        registration.status = .notRegistered
        registration.statusAfterRegistering = .requiresApproval
        let (awdl, _) = make()
        awdl.setEnabled(true)
        try await waitUntil { awdl.state == .needsApproval }
        #expect(helper.requests.isEmpty)
        awdl.openApproval()
        #expect(registration.approvalsOpened == 1)

        registration.status = .enabled  // the user allowed it in System Settings
        try await waitUntil { awdl.state == .holding }
        #expect(helper.requests == [true])
    }

    @Test("A helper that ended is asked again; one that does not answer shows the failure")
    func asksAgainAfterAnEnd() async throws {
        registration.status = .enabled
        let (awdl, _) = make()
        awdl.setEnabled(true)
        try await waitUntil { awdl.state == .holding }
        helper.onBroken?()
        try await waitUntil { helper.requests == [true, true] }
        try await waitUntil { awdl.state == .holding }

        helper.answer = nil
        helper.onBroken?()
        try await waitUntil { awdl.state == .failed }
    }

    @Test("A build that cannot run the helper shows the failure")
    func failsWithoutHelper() async throws {
        registration.status = .notFound
        registration.statusAfterRegistering = .notFound
        let (awdl, _) = make()
        awdl.setEnabled(true)
        try await waitUntil { awdl.state == .failed }
        #expect(helper.requests.isEmpty)
    }

    @Test("Turned on at the last launch, it holds AWDL off again at start")
    func resumesAtStart() async throws {
        registration.status = .enabled
        defaults.set(true, forKey: "keepAWDLOff")
        let (awdl, _) = make()
        try await waitUntil { awdl.state == .holding }
    }

    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<200 where !condition() {
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

@MainActor
final class FakeHelperRegistration: NetworkHelperRegistering {
    var status: NetworkHelperStatus = .notRegistered
    var statusAfterRegistering: NetworkHelperStatus?
    private(set) var registrations = 0
    private(set) var unregistrations = 0
    private(set) var approvalsOpened = 0

    func register() throws {
        registrations += 1
        if let statusAfterRegistering {
            status = statusAfterRegistering
        } else {
            status = .enabled
        }
    }

    func unregister() {
        unregistrations += 1
        status = .notRegistered
    }

    func openApproval() {
        approvalsOpened += 1
    }
}

@MainActor
final class FakeHelper: NetworkHelperConnecting {
    var onBroken: (() -> Void)?
    /// What the helper answers; nil: it does not.
    var answer: Bool? = true
    private(set) var requests: [Bool] = []
    private(set) var disconnects = 0

    func keepAWDLOff(_ off: Bool) async -> Bool? {
        requests.append(off)
        return answer.map { $0 && off }
    }

    func disconnect() {
        disconnects += 1
    }
}
