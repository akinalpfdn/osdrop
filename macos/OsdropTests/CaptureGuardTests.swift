import Foundation
import Testing

@testable import Osdrop

@Suite("Capture guard state")
struct CaptureGuardStateTests {
    private let start = ContinuousClock.now
    private let timeout = Duration.seconds(2)

    @Test("Ends Osdrop only when armed and silent for longer than the timeout")
    func silenceWhileArmed() {
        var state = CaptureGuardState(timeout: timeout)
        #expect(!state.shouldTerminate(at: start + .seconds(60)))
        state.receive(CaptureGuardMessage.arm.rawValue, at: start)
        #expect(!state.shouldTerminate(at: start + .seconds(2)))
        #expect(state.shouldTerminate(at: start + .milliseconds(2001)))
    }

    @Test("Heartbeats keep Osdrop alive")
    func heartbeats() {
        var state = CaptureGuardState(timeout: timeout)
        state.receive(CaptureGuardMessage.arm.rawValue, at: start)
        state.receive(CaptureGuardMessage.heartbeat.rawValue, at: start + .seconds(1.5))
        #expect(!state.shouldTerminate(at: start + .seconds(3)))
        #expect(state.shouldTerminate(at: start + .seconds(3.6)))
    }

    @Test("Disarming stops the wait; heartbeats and unknown bytes do not arm it")
    func disarmAndNoise() {
        var state = CaptureGuardState(timeout: timeout)
        state.receive(CaptureGuardMessage.heartbeat.rawValue, at: start)
        state.receive(0x00, at: start)
        #expect(!state.isArmed)
        state.receive(CaptureGuardMessage.arm.rawValue, at: start)
        state.receive(CaptureGuardMessage.disarm.rawValue, at: start + .seconds(1))
        #expect(!state.shouldTerminate(at: start + .seconds(60)))
    }
}

/// Runs the real guard executable from the app bundle against a stand-in process, so no input is
/// captured and Osdrop itself is never ended. The test host is a windowless menu bar app, so App
/// Nap is held off as Osdrop does while its event tap runs.
@Suite("Capture guard process", .serialized)
final class CaptureGuardProcessTests {
    private let activity = ProcessInfo.processInfo.beginActivity(
        options: [.userInitiatedAllowingIdleSystemSleep, .latencyCritical], reason: "Capture guard tests")

    deinit {
        ProcessInfo.processInfo.endActivity(activity)
    }

    private func standIn() throws -> Process {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sleep")
        process.arguments = ["30"]
        try process.run()
        return process
    }

    private func waitPast(_ timeout: Duration) async throws {
        try await Task.sleep(for: timeout + .milliseconds(800))
    }

    @Test("The guard ships inside the app bundle")
    func bundled() {
        #expect(Bundle.main.url(forAuxiliaryExecutable: CaptureGuardProtocol.executableName) != nil)
    }

    @Test("An armed guard ends a silent process")
    func endsSilentProcess() async throws {
        let target = try standIn()
        defer { target.terminate() }
        let captureGuard = ProcessCaptureGuard(target: target.processIdentifier)
        #expect(captureGuard.ensureRunning())
        captureGuard.send(.arm)
        try await waitPast(CaptureGuardProtocol.timeout)
        #expect(!target.isRunning)
    }

    @Test("A disarmed guard leaves the process alone")
    func disarmedLeavesProcess() async throws {
        let target = try standIn()
        defer { target.terminate() }
        let captureGuard = ProcessCaptureGuard(target: target.processIdentifier)
        #expect(captureGuard.ensureRunning())
        captureGuard.send(.arm)
        captureGuard.send(.disarm)
        try await waitPast(CaptureGuardProtocol.timeout)
        #expect(target.isRunning)
    }
}
