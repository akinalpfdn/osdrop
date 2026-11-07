import CoreBridge
import CoreGraphics
import Foundation
import Testing

@testable import Osdrop

/// Input control on the Mac with an event tap, injector and mesh in memory: what goes to core,
/// what core's events do here, and the safety timers.
@MainActor
@Suite("Input control")
struct InputControllerTests {
    private let mesh = FakeControlMesh()
    private let capture = FakeCapture()
    private let injector = FakeInjector()
    private let cursor = FakeCursor()
    private let capsLock = FakeCapsLock()
    private let other = inputDeviceID(0x42)

    private func start(
        trusted: Bool = true, preferences: FakeInputPreferences = FakeInputPreferences()
    ) -> InputController {
        let controller = InputController(
            mesh: mesh,
            services: .fake(
                capture: capture, injector: injector, trusted: trusted, cursor: cursor, capsLock: capsLock,
                preferences: preferences)
        ) { _ in "Other Mac" }
        controller.start()
        controller.setActive(true)
        return controller
    }

    @Test("Captured input goes to core in 1/16 point, with click counts")
    func forwardsInput() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching && capture.isRunning }
        capture.send(
            .input(CapturedInput(event: .pointerMoved(dx: 1, dy: -2), timestamp: 0, location: .init(x: 10, y: 20))))
        capture.send(
            .input(CapturedInput(event: .button(.left, isDown: true), timestamp: 0, location: .init(x: 10, y: 20))))
        capture.send(
            .input(CapturedInput(event: .button(.left, isDown: false), timestamp: 0, location: .init(x: 10, y: 20))))
        capture.send(
            .input(CapturedInput(event: .button(.left, isDown: true), timestamp: 0, location: .init(x: 10, y: 20))))
        capture.send(.input(CapturedInput(event: .key(.keyboard(0x04), .down), timestamp: 0)))
        try await waitUntil { mesh.calls.count(where: { if case .input = $0 { true } else { false } }) == 5 }
        let inputs = mesh.calls.compactMap { call -> ControlInput? in
            if case .input(let input) = call { input } else { nil }
        }
        let at = CoordPoint(x: 160, y: 320)
        #expect(
            inputs == [
                .pointer(position: at, delta: CoordPoint(x: 16, y: -32)),
                .button(0, isDown: true, clickCount: 1, position: at),
                .button(0, isDown: false, clickCount: 1, position: at),
                .button(0, isDown: true, clickCount: 2, position: at),
                .key(page: 7, usage: 4, action: 1),
            ])
        controller.stop()
    }

    @Test("Without Accessibility this Mac is not controlled, and watching starts once it is granted")
    func refusesWithoutPermission() async throws {
        let controller = start(trusted: false)
        try await waitUntil { mesh.calls.contains(.unavailable(.notPermitted)) }
        #expect(!controller.isWatching)
        #expect(!capture.isRunning)
        controller.screenLockChanged(true)
        #expect(mesh.calls.last == .unavailable(.screenLocked))
        controller.stop()
    }

    @Test("Input is watched only while a member is connected")
    func watchesWhileActive() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching && capture.isRunning }
        controller.setActive(false)
        #expect(!controller.isWatching)
        #expect(!capture.isRunning)
        #expect(mesh.calls.last == .unavailable(nil), "still accepts control once a member connects")
        controller.setActive(true)
        #expect(controller.isWatching)
        controller.stop()
    }

    @Test("A locked screen refuses control until it is unlocked")
    func screenLock() async throws {
        let controller = start()
        try await waitUntil { mesh.calls.contains(.unavailable(nil)) }
        controller.screenLockChanged(true)
        #expect(mesh.calls.last == .unavailable(.screenLocked))
        controller.screenLockChanged(false)
        #expect(mesh.calls.last == .unavailable(nil))
        controller.stop()
    }

    @Test("A capture begins and ends as core says, with the cursor back where core puts it")
    func captures() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching }
        mesh.send(.captureBegan(other, releaseHere: [.key(page: 7, usage: 0xE1, action: 2)]))
        try await waitUntil { controller.capturingFor == other }
        #expect(capture.mode == .capture)
        #expect(injector.calls.contains(.releaseHere([.keyboard(0xE1)])))
        try await waitUntil { capture.heartbeats >= 2 }

        mesh.send(
            .captureEnded(other, cursor: CoordPoint(x: 1600, y: 3200), reason: .refused(.locked), capsLock: false))
        try await waitUntil { controller.capturingFor == nil }
        #expect(capture.mode == .observe)
        #expect(injector.calls.last == .place(CGPoint(x: 100, y: 200)))
        #expect(controller.notice == .refused(name: "Other Mac", .locked))
        let beats = capture.heartbeats
        try await Task.sleep(for: .milliseconds(600))
        #expect(capture.heartbeats == beats, "no heartbeat once the capture ended")
        controller.dismissNotice()
        #expect(controller.notice == nil)
        controller.stop()
    }

    @Test("A capture the tap lost brings the cursor home; one this Mac ended does not")
    func lostCapture() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching }
        capture.send(.released(.watchdog))
        capture.send(.released(.releaseChord))
        capture.send(.released(.requested))
        try await waitUntil { mesh.calls.contains(.returnHome(inputLost: false)) }
        let homes = mesh.calls.filter { if case .returnHome = $0 { true } else { false } }
        #expect(homes == [.returnHome(inputLost: true), .returnHome(inputLost: false)])
        controller.stop()
    }

    @Test("The lock hotkey toggles the lock")
    func lockHotkey() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching }
        capture.send(.hotkey(.lockInput))
        try await waitUntil { controller.isLocked }
        capture.send(.hotkey(.lockInput))
        try await waitUntil { !controller.isLocked }
        #expect(mesh.calls.filter { if case .lock = $0 { true } else { false } } == [.lock(true), .lock(false)])
        controller.stop()
    }

    @Test("A controller's input is injected here and the rest released when it ends")
    func injects() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching }
        mesh.send(.controlled(by: other))
        mesh.send(.inject(.pointer(position: CoordPoint(x: 320, y: 480), delta: CoordPoint(x: 0, y: 0))))
        mesh.send(.inject(.key(page: 7, usage: 4, action: 1)))
        mesh.send(.inject(.button(1, isDown: true, clickCount: 2, position: CoordPoint(x: 320, y: 480))))
        mesh.send(
            .inject(
                .scroll(
                    ScrollStroke(unit: 2, phase: 3, momentum: 0, dx: 0, dy: 512), position: CoordPoint(x: 320, y: 480)))
        )
        try await waitUntil { injector.calls.count == 4 }
        #expect(controller.controller == other)
        #expect(
            injector.calls == [
                .move(CGPoint(x: 20, y: 30)),
                .key(.keyboard(4), .down),
                .button(.right, isDown: true, at: CGPoint(x: 20, y: 30), clickCount: 2),
                .scroll(ScrollDelta(x: 0, y: 2, unit: .pixel, phase: .changed, momentum: .none)),
            ])
        mesh.send(.controlled(by: nil))
        try await waitUntil { controller.controller == nil }
        #expect(injector.calls.last == .releaseAll)
        controller.stop()
    }

    @Test("An application moving the cursor of a controlled Mac is reported once")
    func reportsCursor() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching }
        mesh.send(.controlled(by: other))
        mesh.send(.inject(.pointer(position: CoordPoint(x: 160, y: 160), delta: CoordPoint(x: 0, y: 0))))
        cursor.location = CGPoint(x: 10, y: 10)
        try await waitUntil { controller.controller == other }
        try await Task.sleep(for: .milliseconds(300))
        #expect(!mesh.calls.contains { if case .cursorMoved = $0 { true } else { false } })
        cursor.location = CGPoint(x: 400, y: 300)
        try await waitUntil { mesh.calls.contains(.cursorMoved(CoordPoint(x: 6400, y: 4800))) }
        try await Task.sleep(for: .milliseconds(300))
        #expect(mesh.calls.count { if case .cursorMoved = $0 { true } else { false } } == 1)
        controller.stop()
    }

    @Test("Latency reports reach the developer tools")
    func latency() async throws {
        let controller = start()
        mesh.send(.inputLatency(other, samples: 40, medianMicroseconds: 2_500, p99Microseconds: 9_000))
        try await waitUntil { controller.latency[other] != nil }
        #expect(
            controller.latency[other]
                == InputLatency(samples: 40, median: .microseconds(2_500), p99: .microseconds(9_000)))
        controller.stop()
    }

    @Test("Stored input settings and pointer speeds reach core at start")
    func appliesSettings() async throws {
        var preferences = FakeInputPreferences()
        preferences.input = InputSettings(modifierMapping: .positions, pushMilliseconds: 300, immediate: true)
        preferences.pointerSpeeds = [other: 1_500]
        preferences.hotkeys.lockInput = Hotkey(keyCode: 40, modifiers: [.control, .option])
        let controller = start(preferences: preferences)
        #expect(mesh.calls.prefix(2) == [.settings(preferences.input), .speed(1_500, other)])
        #expect(capture.hotkeys == preferences.hotkeys)
        controller.apply(InputSettings(pushMilliseconds: 200))
        #expect(mesh.calls.last == .settings(InputSettings(pushMilliseconds: 200)))
        controller.stop()
    }

    @Test("Caps Lock is reported while the cursor is home and set as the user left it elsewhere")
    func capsLockSync() async throws {
        let controller = start()
        try await waitUntil { mesh.calls.contains(.capsLock(false)) }
        capsLock.toggleByUser()
        try await waitUntil { mesh.calls.contains(.capsLock(true)) }

        mesh.send(.captureBegan(other, releaseHere: []))
        try await waitUntil { controller.capturingFor == other }
        let reported = mesh.calls.count { if case .capsLock = $0 { true } else { false } }
        capsLock.toggleByUser()  // a press swallowed here; core follows what it sent
        try await Task.sleep(for: .milliseconds(400))
        #expect(mesh.calls.count { if case .capsLock = $0 { true } else { false } } == reported)

        mesh.send(.captureEnded(other, cursor: CoordPoint(x: 0, y: 0), reason: .returned, capsLock: true))
        try await waitUntil { controller.capturingFor == nil }
        #expect(capsLock.isOn, "on again, as the user left it on the other computer")
        controller.stop()
    }

    @Test("Caps Lock from a controller is set as a state")
    func capsLockInjected() async throws {
        let controller = start()
        mesh.send(.controlled(by: other))
        mesh.send(.inject(.capsLock(true)))
        try await waitUntil { capsLock.isOn }
        mesh.send(.inject(.capsLock(true)))
        mesh.send(.inject(.capsLock(false)))
        try await waitUntil { !capsLock.isOn }
        #expect(capsLock.setValues == [true, false], "set only when it differs")
        #expect(!injector.calls.contains { if case .key = $0 { true } else { false } })
        controller.stop()
    }

    @Test("Right after Osdrop sets Caps Lock, a Caps Lock event is not taken for the user's key")
    func ignoresTheCapsLockEcho() async throws {
        let controller = start()
        try await waitUntil { controller.isWatching }
        mesh.send(.controlled(by: other))
        mesh.send(.inject(.capsLock(true)))
        try await waitUntil { capsLock.isOn }
        let caps = CapturedInput(event: .key(.keyboard(ModifierKeys.capsLockUsage), .down), timestamp: 0)
        capture.send(.input(caps))
        capture.send(.input(CapturedInput(event: .key(.keyboard(0x04), .down), timestamp: 0)))
        try await waitUntil { mesh.calls.contains(.input(.key(page: 7, usage: 4, action: 1))) }
        #expect(!mesh.calls.contains(.input(.key(page: 7, usage: 0x39, action: 1))), "the echo stays home")
        try await Task.sleep(for: Tunables.Control.capsEchoWindow + .milliseconds(100))
        capture.send(.input(caps))
        try await waitUntil { mesh.calls.contains(.input(.key(page: 7, usage: 0x39, action: 1))) }
        controller.stop()
    }

    @Test("Core's drag events go to drag and drop, which runs while a member is connected")
    func forwardsDrags() async throws {
        let drags = FakeDrags()
        let controller = InputController(
            mesh: mesh, services: .fake(capture: capture, injector: injector, cursor: cursor, capsLock: capsLock),
            makeDrags: { _ in drags }
        ) { _ in "Other Mac" }
        controller.start()
        controller.setActive(true)
        #expect(drags.calls == [.start])
        mesh.send(.dragArrived(source: other, dragID: 7, at: CoordPoint(x: 160, y: 320), items: []))
        mesh.send(.dragWithdrawn(dragID: 7))
        mesh.send(.dragEnded(dragID: 3, .dropped))
        try await waitUntil { drags.calls.count == 4 }
        #expect(
            drags.calls == [.start, .arrived(other, 7, CGPoint(x: 10, y: 20), []), .withdrawn(7), .ended(3, .dropped)])
        controller.setActive(false)
        #expect(drags.calls.last == .stop)
        controller.stop()
    }

    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<200 where !condition() {
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

private func inputDeviceID(_ byte: UInt8) -> DeviceID {
    guard let id = DeviceID(bytes: [UInt8](repeating: byte, count: 16)) else {
        preconditionFailure("16 bytes make a device ID")
    }
    return id
}
